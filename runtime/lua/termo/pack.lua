local api = termo.api
local M = { plugins = {}, loaded = {}, errors = {}, lock = {} }

local function env_dir(var, fallback)
	local v = os.getenv(var)
	if v == nil or v == "" then
		v = (os.getenv("HOME") or "") .. fallback
	end
	return v
end

M.dir = env_dir("XDG_DATA_HOME", "/.local/share") .. "/termo/pack"
M.lockfile = env_dir("XDG_CONFIG_HOME", "/.config") .. "/termo/termo-pack-lock.json"

local function read_file(path)
	local f = io.open(path, "r")
	if f == nil then
		return nil
	end
	local s = f:read("*a")
	f:close()
	return s
end

local function write_file(path, text)
	local f = io.open(path, "w")
	if f == nil then
		return false
	end
	f:write(text)
	f:close()
	return true
end

local function fail(msg)
	M.errors[#M.errors + 1] = msg
	print("pack: " .. msg)
	return nil, msg
end

local function run(argv, cb)
	local lines = {}
	api.system(argv, {
		on_stdout = function(line)
			lines[#lines + 1] = line
		end,
		on_exit = function(status)
			cb(status, lines)
		end,
	})
end

local function git(dir, args, cb)
	local argv = { "git", "-C", dir }
	for _, a in ipairs(args) do
		argv[#argv + 1] = a
	end
	run(argv, cb)
end

local function each(list, fn, done)
	local i = 0
	local function step()
		i = i + 1
		if list[i] == nil then
			return done()
		end
		fn(list[i], step)
	end
	step()
end

local function sorted_names(t)
	local names = {}
	for name in pairs(t) do
		names[#names + 1] = name
	end
	table.sort(names)
	return names
end

local function spec_of(item)
	if type(item) == "string" then
		item = { src = item }
	end
	local src = item.src or item.url
	if type(src) ~= "string" then
		error("pack: a plugin spec needs src", 3)
	end
	if src:match("^[%w_.-]+/[%w_.-]+$") then
		src = "https://github.com/" .. src .. ".git"
	end
	return {
		src = src,
		name = item.name or src:match("([^/]+)%.git$") or src:match("([^/]+)/*$"),
		version = item.version,
		data = item.data,
		dependencies = item.dependencies,
		event = item.event,
		keys = item.keys,
		format = item.format,
		cmd = item.cmd,
		mode = item.mode,
		cond = item.cond,
		lazy = item.lazy,
	}
end

local function version_text(version)
	if type(version) == "table" then
		return version.range
	end
	return version
end

local function parse_version(s)
	local a, b, c = s:match("^v?(%d+)%.?(%d*)%.?(%d*)$")
	if a == nil then
		return nil
	end
	return { tonumber(a), tonumber(b) or 0, tonumber(c) or 0 }
end

local function newer(a, b)
	for i = 1, 3 do
		if a[i] ~= b[i] then
			return a[i] > b[i]
		end
	end
	return false
end

local function in_range(range, v)
	local i = 0
	for part in range:gmatch("[^.]+") do
		i = i + 1
		if part ~= "x" and part ~= "*" and tonumber(part) ~= v[i] then
			return false
		end
	end
	return true
end

local function best_tag(tags, range)
	local best, bestv
	for _, tag in ipairs(tags) do
		local v = parse_version(tag)
		if v ~= nil and in_range(range, v) and (bestv == nil or newer(v, bestv)) then
			best, bestv = tag, v
		end
	end
	return best
end

local function resolve(p, cb)
	local version = p.spec.version
	local function sha_of(ref)
		git(p.dir, { "rev-parse", "--verify", "--quiet", ref .. "^{commit}" },
		    function(status, lines)
			if status ~= 0 then
				return cb(nil, "no such version " .. ref)
			end
			cb(lines[1])
		end)
	end
	if version == nil then
		return sha_of("origin/HEAD")
	end
	if type(version) == "table" then
		return git(p.dir, { "tag" }, function(status, tags)
			local tag = status == 0 and best_tag(tags, version.range)
			if not tag then
				return cb(nil, "no tag matches " .. tostring(version.range))
			end
			sha_of(tag)
		end)
	end
	git(p.dir, { "rev-parse", "--verify", "--quiet", "origin/" .. version .. "^{commit}" },
	    function(status, lines)
		if status == 0 then
			return cb(lines[1])
		end
		sha_of(version)
	end)
end

local function read_lock()
	local lock = {}
	local text = read_file(M.lockfile)
	if text == nil then
		return lock
	end
	local ok, data = pcall(termo.json.decode, text)
	if ok and type(data) == "table" then
		for _, e in ipairs(data.plugins or {}) do
			lock[e.name] = e
		end
	end
	return lock
end

local function write_lock()
	local entries = {}
	for _, name in ipairs(sorted_names(M.plugins)) do
		local p = M.plugins[name]
		if p.rev ~= nil then
			entries[#entries + 1] = {
				name = name,
				src = p.spec.src,
				rev = p.rev,
				version = version_text(p.spec.version),
			}
		end
	end
	if not write_file(M.lockfile, termo.json.encode({ plugins = entries })) then
		fail("cannot write " .. M.lockfile)
	end
end

local function emit(name, kind, p)
	api.emit(name, {
		name = p.name,
		kind = kind,
		src = p.spec.src,
		path = p.dir,
		rev = p.rev or "",
	})
end

local function register(spec)
	local p = M.plugins[spec.name]
	if p == nil then
		p = { name = spec.name, dir = M.dir .. "/" .. spec.name }
		M.plugins[spec.name] = p
	end
	p.spec = spec
	local l = M.lock[spec.name]
	if l ~= nil and l.src == spec.src then
		p.locked = l.rev
	end
	return p
end

local function read_manifest(dir)
	local text = read_file(dir .. "/termo.json")
	if text == nil then
		return nil, dir .. "/termo.json: not found"
	end
	local ok, manifest = pcall(termo.json.decode, text)
	if not ok then
		return nil, dir .. "/termo.json: " .. tostring(manifest)
	end
	if type(manifest) ~= "table" or type(manifest.name) ~= "string" then
		return nil, dir .. "/termo.json: needs a name"
	end
	return manifest
end

local function run_main(dir, manifest, spec)
	local main = dir .. "/" .. (manifest.main or "lua/" .. manifest.name .. ".lua")
	package.path = dir .. "/lua/?.lua;" .. dir .. "/lua/?/init.lua;" .. package.path
	local chunk, err = loadfile(main)
	if chunk == nil then
		return nil, err
	end
	local plugin = { name = manifest.name, dir = dir, manifest = manifest, spec = spec }
	if manifest.lib ~= nil then
		local ok, lib = pcall(require("ffi").load, dir .. "/" .. manifest.lib)
		if not ok then
			return nil, manifest.name .. ": " .. tostring(lib)
		end
		plugin.lib = lib
	end
	local env = setmetatable({}, {
		__index = _G,
		__newindex = function(_, key)
			error(manifest.name .. " tried to set global " .. tostring(key), 2)
		end,
	})
	setfenv(chunk, env)
	local started = os.clock()
	local ran, result = pcall(chunk, plugin)
	if not ran then
		return nil, manifest.name .. ": " .. tostring(result)
	end
	M.loaded[manifest.name] = {
		dir = dir,
		manifest = manifest,
		module = result,
		lib = plugin.lib,
		load_ms = (os.clock() - started) * 1000,
	}
	return result
end

local function load_plugin(p)
	if M.loaded[p.name] ~= nil then
		return M.loaded[p.name].module
	end
	if p.failed or p.manifest == nil then
		return nil
	end
	if p.visiting then
		return fail(p.name .. ": dependency cycle")
	end
	p.visiting = true
	for _, dep in ipairs(p.deps) do
		local dp = M.plugins[dep]
		if dp == nil or (M.loaded[dep] == nil and load_plugin(dp) == nil and
		    M.loaded[dep] == nil) then
			p.visiting = false
			p.failed = true
			return fail(p.name .. ": needs " .. dep)
		end
	end
	p.visiting = false
	local mod, err = run_main(p.dir, p.manifest, p.spec)
	if mod == nil and err ~= nil then
		p.failed = true
		return fail(err)
	end
	return mod
end

local function as_list(v)
	if v == nil then
		return {}
	end
	if type(v) == "table" then
		return v
	end
	return { v }
end

local function key_entry(k)
	if type(k) == "string" then
		return { table = "prefix", key = k }
	end
	return {
		table = k[1] or k.table or "prefix",
		key = k[2] or k.key,
		action = k[3] or k.action or k.command,
		note = k.note or k.desc,
	}
end

local function triggers_of(p)
	local s, m = p.spec, p.manifest
	if s.lazy == false then
		return nil
	end
	local t = { events = as_list(s.event), keys = {}, formats = as_list(s.format),
	    cmds = {}, modes = as_list(s.mode) }
	for _, c in ipairs(as_list(s.cmd)) do
		t.cmds[#t.cmds + 1] = { name = c }
	end
	for _, c in ipairs(m.commands or {}) do
		t.cmds[#t.cmds + 1] = type(c) == "table" and c or { name = c }
	end
	for _, k in ipairs(s.keys or {}) do
		t.keys[#t.keys + 1] = key_entry(k)
	end
	for _, k in ipairs(m.keys or {}) do
		t.keys[#t.keys + 1] = key_entry(k)
	end
	for _, f in ipairs(m.formats or {}) do
		t.formats[#t.formats + 1] = type(f) == "table" and f.name or f
	end
	if s.lazy or #t.events + #t.keys + #t.formats + #t.cmds + #t.modes > 0 then
		return t
	end
	return nil
end

local function quote(s)
	return "'" .. s:gsub("'", "'\\''") .. "'"
end

local function run_action(action, ev)
	if type(action) == "function" then
		return action(ev)
	end
	if type(action) == "string" then
		api.cmd(action)
	end
end

local function arm(p, t)
	for _, name in ipairs(t.events) do
		local id
		id = api.on(name, function()
			api.off(id)
			M.load(p.name)
		end)
	end
	for _, k in ipairs(t.keys) do
		api.keymap_set(k.table, k.key, function(ev)
			M.load(p.name)
			run_action(k.action, ev)
		end, { note = k.note })
	end
	for _, c in ipairs(t.cmds) do
		local stub
		stub = termo.command.define(c.name, function(args)
			M.load(p.name)
			if termo.command.commands[c.name] == stub then
				return fail(p.name .. " did not define " .. c.name)
			end
			local line = { c.name }
			for _, a in ipairs(args) do
				line[#line + 1] = quote(a)
			end
			api.cmd(table.concat(line, " "))
		end, { desc = c.desc })
	end
	for _, m in ipairs(t.modes) do
		local id
		id = api.on("client-key-table-changed", function(ev)
			if ev.key_table == "mode-" .. m then
				api.off(id)
				M.load(p.name)
			end
		end)
	end
	for _, f in ipairs(t.formats) do
		api.format_add(f, function()
			if M.loaded[p.name] ~= nil then
				return ""
			end
			M.load(p.name)
			return api.eval("#{" .. f .. "}")
		end)
	end
end

local function allowed(p)
	local c = p.spec.cond
	if type(c) == "function" then
		c = c()
	end
	return c ~= false
end

local function activate(p, load)
	if p.manifest == nil or p.failed then
		return
	end
	if type(load) == "function" then
		return load({ name = p.name, spec = p.spec, path = p.dir, manifest = p.manifest })
	end
	if load == false then
		return
	end
	if not allowed(p) then
		p.inactive = true
		return
	end
	local t = triggers_of(p)
	if t == nil then
		return load_plugin(p)
	end
	p.lazy = true
	arm(p, t)
end

local function prepare(p, pending, cb)
	if read_file(p.dir .. "/termo.json") == nil then
		if p.installed then
			p.failed = true
			fail(p.name .. ": no termo.json after install")
		else
			p.pending = true
			pending[#pending + 1] = p
		end
		return cb()
	end
	local manifest, err = read_manifest(p.dir)
	if manifest == nil then
		p.failed = true
		fail(err)
		return cb()
	end
	p.manifest = manifest
	p.prepared = true
	p.deps = {}
	for _, list in ipairs({ p.spec.dependencies or {}, manifest.dependencies or {} }) do
		for _, d in ipairs(list) do
			p.deps[#p.deps + 1] = register(spec_of(d)).name
		end
	end
	git(p.dir, { "rev-parse", "--verify", "--quiet", "HEAD" }, function(status, lines)
		if status == 0 then
			p.rev = lines[1]
		end
		cb()
	end)
end

local function install(p, cb)
	emit("@pack-changed-pre", "install", p)
	run({ "git", "clone", "--quiet", "--filter=blob:none", p.spec.src, p.dir },
	    function(status)
		if status ~= 0 then
			return cb("git clone failed (" .. status .. ")")
		end
		local function checkout(sha, err)
			if sha == nil then
				return cb(err)
			end
			git(p.dir, { "checkout", "--quiet", "--detach", sha }, function(st)
				if st ~= 0 then
					return cb("git checkout failed (" .. st .. ")")
				end
				p.rev = sha
				write_lock()
				emit("@pack-changed", "install", p)
				cb()
			end)
		end
		if p.locked ~= nil then
			return checkout(p.locked)
		end
		resolve(p, checkout)
	end)
end

local function with_client(fn)
	local ok, err = pcall(fn, nil)
	if ok or not tostring(err):find("no client", 1, true) then
		return ok, err
	end
	local id
	id = api.on("client-attached", function(ev)
		api.off(id)
		fn(ev.client)
	end)
	return true
end

local function ask(question, cb)
	local ok, err = with_client(function(client)
		termo.ui.confirm(question, cb, { client = client })
	end)
	if not ok then
		error(err, 0)
	end
end

function M.setup(list, opts)
	opts = opts or {}
	M.errors = {}
	M.plugins = {}
	M.lock = read_lock()
	local order = {}
	for _, item in ipairs(list) do
		order[#order + 1] = register(spec_of(item))
	end
	local function finish()
		for _, p in ipairs(order) do
			activate(p, opts.load)
		end
		if opts.done then
			opts.done(M.errors)
		end
	end
	local function pass()
		local pending = {}
		local settle
		local function sweep()
			local todo = {}
			for _, name in ipairs(sorted_names(M.plugins)) do
				local p = M.plugins[name]
				if not (p.prepared or p.failed or p.pending) then
					todo[#todo + 1] = p
				end
			end
			if #todo == 0 then
				return settle()
			end
			each(todo, function(p, next)
				prepare(p, pending, next)
			end, sweep)
		end
		settle = function()
			if #pending == 0 then
				return finish()
			end
			local names = {}
			for _, p in ipairs(pending) do
				names[#names + 1] = p.name
			end
			local function go(yes)
				if not yes then
					for _, p in ipairs(pending) do
						p.failed = true
						fail(p.name .. ": not installed")
					end
					return finish()
				end
				each(pending, function(p, next)
					install(p, function(err)
						p.pending = false
						p.installed = true
						if err ~= nil then
							p.failed = true
							fail(p.name .. ": " .. err)
						end
						next()
					end)
				end, pass)
			end
			if opts.confirm == false then
				return go(true)
			end
			ask("Install " .. table.concat(names, ", "), go)
		end
		sweep()
	end
	run({ "mkdir", "-p", M.dir, M.lockfile:match("^(.*)/[^/]*$") }, pass)
end

function M.load(what)
	local p = M.plugins[what]
	if p ~= nil then
		return load_plugin(p)
	end
	local manifest, err = read_manifest(what)
	if manifest == nil then
		return fail(err)
	end
	local mod
	mod, err = run_main(what, manifest)
	if mod == nil and err ~= nil then
		return fail(err)
	end
	return mod
end

local function offer(p, entry, apply)
	local decided = false
	local function decide(yes)
		if not decided then
			decided = true
			apply(yes)
		end
	end
	local items = {}
	for _, line in ipairs(entry.log) do
		items[#items + 1] = { line }
	end
	items[#items + 1] = {}
	items[#items + 1] = { "Update", "u", function() decide(true) end }
	items[#items + 1] = { "Skip", "s", function() decide(false) end }
	local ok, err = with_client(function(client)
		termo.ui.menu({
			title = p.name .. " " .. entry.old:sub(1, 7) .. ".." .. entry.new:sub(1, 7),
			items = items,
			client = client,
			on_close = function() decide(false) end,
		})
	end)
	if not ok then
		fail(p.name .. ": " .. tostring(err))
		decide(false)
	end
end

function M.update(names, opts)
	opts = opts or {}
	local report = {}
	each(names or sorted_names(M.plugins), function(name, next)
		local p = M.plugins[name]
		if p == nil or p.rev == nil then
			fail(name .. ": not installed")
			return next()
		end
		local function fetched(status)
			if status ~= 0 then
				fail(name .. ": git fetch failed (" .. status .. ")")
				return next()
			end
			resolve(p, function(sha, err)
				if sha == nil then
					fail(name .. ": " .. err)
					return next()
				end
				if sha == p.rev then
					return next()
				end
				git(p.dir, { "log", "--oneline", p.rev .. ".." .. sha }, function(_, log)
					local entry = { name = name, old = p.rev, new = sha, log = log }
					report[#report + 1] = entry
					local function apply(yes)
						if not yes then
							return next()
						end
						emit("@pack-changed-pre", "update", p)
						git(p.dir, { "checkout", "--quiet", "--detach", sha }, function(st)
							if st ~= 0 then
								fail(name .. ": git checkout failed (" .. st .. ")")
								return next()
							end
							p.rev = sha
							entry.applied = true
							write_lock()
							emit("@pack-changed", "update", p)
							next()
						end)
					end
					if opts.force then
						return apply(true)
					end
					offer(p, entry, apply)
				end)
			end)
		end
		if opts.offline then
			return fetched(0)
		end
		git(p.dir, { "fetch", "--quiet", "--tags", "origin" }, fetched)
	end, function()
		if opts.done then
			opts.done(report, M.errors)
		end
	end)
end

function M.del(names, opts)
	opts = opts or {}
	each(names, function(name, next)
		local p = M.plugins[name] or
		    { name = name, spec = { src = "" }, dir = M.dir .. "/" .. name }
		emit("@pack-changed-pre", "delete", p)
		run({ "rm", "-rf", p.dir }, function(status)
			if status ~= 0 then
				fail(name .. ": rm failed (" .. status .. ")")
				return next()
			end
			M.plugins[name] = nil
			M.loaded[name] = nil
			write_lock()
			emit("@pack-changed", "delete", p)
			next()
		end)
	end, function()
		if opts.done then
			opts.done(M.errors)
		end
	end)
end

function M.clean(opts)
	run({ "ls", "-1", M.dir }, function(_, names)
		local orphans = {}
		for _, name in ipairs(names) do
			if M.plugins[name] == nil then
				orphans[#orphans + 1] = name
			end
		end
		M.del(orphans, opts)
	end)
end

function M.get(names)
	local out = {}
	for _, name in ipairs(names or sorted_names(M.plugins)) do
		local p = M.plugins[name]
		if p ~= nil then
			local l = M.loaded[name]
			out[#out + 1] = {
				name = name,
				spec = p.spec,
				path = p.dir,
				rev = p.rev,
				active = l ~= nil,
				lazy = p.lazy == true,
				load_ms = l and l.load_ms,
			}
		end
	end
	return out
end

function M.list()
	return sorted_names(M.loaded)
end

return M
