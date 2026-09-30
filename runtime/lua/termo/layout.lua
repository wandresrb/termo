local api = termo.api
local M = { last = {} }

local function env_dir(var, fallback)
	local v = os.getenv(var)
	if v == nil or v == "" then
		v = (os.getenv("HOME") or "") .. fallback
	end
	return v
end

M.dir = env_dir("XDG_CONFIG_HOME", "/.config") .. "/termo/layouts"

local function quote(s)
	return "'" .. s:gsub("'", "'\\''") .. "'"
end

local function read_file(path)
	local f = io.open(path, "r")
	if f == nil then
		return nil
	end
	local s = f:read("*a")
	f:close()
	return s
end

local function apply_node(node, pane, next)
	if #node == 0 then
		if node.cmd == nil then
			return next()
		end
		return api.cmd_async(string.format("respawn-pane -k -t %s %s", pane,
		    quote(node.cmd)), function(_, err)
			next(err)
		end)
	end

	local flag = (node.dir == "v" or node.dir == "vertical") and "-v" or "-h"
	local panes = { pane }
	local i = 2

	local function children()
		local j = 0
		local function child_next(err)
			if err then
				return next(err)
			end
			j = j + 1
			if j > #node then
				return next()
			end
			apply_node(node[j], panes[j], child_next)
		end
		child_next()
	end

	local function split_next()
		if i > #node then
			return children()
		end
		local size = node[i].size
		if size == nil then
			size = math.floor(100 * (#node - i + 1) / (#node - i + 2)) .. "%"
		end
		api.cmd_async(string.format(
		    "split-window %s -d -t %s -l %s -P -F '#{pane_id}'", flag,
		    panes[i - 1], size), function(out, err)
			if err then
				return next(err)
			end
			panes[i] = out
			i = i + 1
			split_next()
		end)
	end
	split_next()
end

local function pane_args(entry)
	local parts = {}
	if entry.cwd then
		parts[#parts + 1] = "-c " .. quote(entry.cwd)
	end
	if entry.cmd then
		parts[#parts + 1] = quote(entry.cmd)
	end
	return table.concat(parts, " ")
end

local function apply_window(spec, window, next)
	local panes = api.list_panes(window)
	local tiled = {}
	for _, p in ipairs(panes) do
		if not p.floating then
			tiled[#tiled + 1] = p
		end
	end
	local wanted = spec.panes or {}
	local i = #tiled
	local function finish(err)
		if err then
			return next(err)
		end
		api.cmd_async(string.format("select-layout -t %s %s", window,
		    quote(spec.layout)), function(_, e)
			next(e)
		end)
	end
	local function split_next()
		if i >= #wanted then
			return finish()
		end
		i = i + 1
		api.cmd_async(string.format("split-window -d -t %s %s", window,
		    pane_args(wanted[i])), function(_, err)
			if err then
				return next(err)
			end
			split_next()
		end)
	end
	if spec.panes and spec.panes[1] and spec.panes[1].cmd and #tiled == 1 then
		return api.cmd_async(string.format("respawn-pane -k -t %s %s",
		    tiled[1].id, quote(spec.panes[1].cmd)), function(_, err)
			if err then
				return next(err)
			end
			split_next()
		end)
	end
	split_next()
end

function M.apply(spec, opts)
	opts = opts or {}
	local done = opts.done or function(err)
		if err then
			print("layout: " .. err)
		end
	end
	local target = opts.target
	if type(spec.layout) == "string" then
		if target == nil then
			target = api.eval("#{window_id}")
		end
		return apply_window(spec, target, done)
	end
	if target == nil then
		target = api.eval("#{pane_id}")
	end
	apply_node(spec, target, done)
end

function M.dump(window)
	window = window or api.eval("#{window_id}")
	local out = {
		name = api.eval("#{window_name}", window),
		layout = api.eval("#{window_layout}", window),
		panes = {},
	}
	for _, p in ipairs(api.list_panes(window)) do
		if not p.floating then
			out.panes[#out.panes + 1] = {
				cwd = api.eval("#{pane_current_path}", p.id),
				cmd = api.eval("#{pane_current_command}", p.id),
				active = p.active,
			}
		end
	end
	return out
end

local function path_of(name)
	return M.dir .. "/" .. name .. ".json"
end

function M.save(name, window, opts)
	opts = opts or {}
	local spec = M.dump(window)
	api.system({ "mkdir", "-p", M.dir }, {
		on_exit = function(status)
			local f = status == 0 and io.open(path_of(name), "w")
			if not f then
				return opts.done and opts.done("cannot write " .. path_of(name))
			end
			f:write(termo.json.encode({ version = 1, window = spec }))
			f:close()
			if opts.done then
				opts.done(nil, spec)
			end
		end,
	})
end

function M.load(name)
	local text = read_file(path_of(name))
	if text == nil then
		return nil, path_of(name) .. ": not found"
	end
	local ok, data = pcall(termo.json.decode, text)
	if not ok or type(data) ~= "table" or type(data.window) ~= "table" then
		return nil, path_of(name) .. ": not a layout"
	end
	return data.window
end

function M.list(cb)
	local names = {}
	api.system({ "ls", "-1", M.dir }, {
		on_stdout = function(line)
			local name = line:match("^(.*)%.json$")
			if name then
				names[#names + 1] = name
			end
		end,
		on_exit = function()
			table.sort(names)
			cb(names)
		end,
	})
end

local function tiled_count(window)
	local n = 0
	for _, p in ipairs(api.list_panes(window)) do
		if not p.floating then
			n = n + 1
		end
	end
	return n
end

function M.swap(opts)
	opts = opts or {}
	local window = opts.target or api.eval("#{window_id}")
	local count = tiled_count(window)
	M.list(function(names)
		local fitting = {}
		for _, name in ipairs(names) do
			local spec = M.load(name)
			if spec and #(spec.panes or {}) == count then
				fitting[#fitting + 1] = { name = name, spec = spec }
			end
		end
		if #fitting == 0 then
			return opts.done and opts.done("no saved layout has " .. count .. " panes")
		end
		local index = 1
		for i, f in ipairs(fitting) do
			if f.name == M.last[window] then
				index = i % #fitting + 1
			end
		end
		M.last[window] = fitting[index].name
		api.cmd_async(string.format("select-layout -t %s %s", window,
		    quote(fitting[index].spec.layout)), function(_, err)
			if opts.done then
				opts.done(err, fitting[index].name)
			end
		end)
	end)
end

return M
