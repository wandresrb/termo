-- Plugins: a directory with a termo.json manifest ({"name": ..., "main":
-- "lua/name.lua"}) and Lua under lua/. setup() clones the ones that are
-- missing with git and loads the rest; each plugin runs in its own
-- environment over a read-only _G, so it cannot leak globals.
--
--   termo.pack.setup{ "someone/termo-git", { url = "...", name = "x" } }
local api = termo.api
local M = { loaded = {}, errors = {} }

local data_home = os.getenv("XDG_DATA_HOME")
if data_home == nil or data_home == "" then
	data_home = (os.getenv("HOME") or "") .. "/.local/share"
end
M.dir = data_home .. "/termo/pack"

local function read_file(path)
	local f = io.open(path, "r")
	if f == nil then
		return nil
	end
	local s = f:read("*a")
	f:close()
	return s
end

local function fail(msg)
	M.errors[#M.errors + 1] = msg
	print("pack: " .. msg)
	return nil, msg
end

-- load(dir) reads dir/termo.json and runs its main chunk sandboxed; the
-- chunk receives the manifest and its return value is kept.
function M.load(dir)
	local text = read_file(dir .. "/termo.json")
	if text == nil then
		return fail(dir .. "/termo.json: not found")
	end
	local ok, manifest = pcall(termo.json.decode, text)
	if not ok then
		return fail(dir .. "/termo.json: " .. tostring(manifest))
	end
	if type(manifest) ~= "table" or type(manifest.name) ~= "string" then
		return fail(dir .. "/termo.json: needs a name")
	end
	local main = dir .. "/" .. (manifest.main or "lua/" .. manifest.name .. ".lua")
	package.path = dir .. "/lua/?.lua;" .. dir .. "/lua/?/init.lua;" ..
	    package.path

	local chunk, err = loadfile(main)
	if chunk == nil then
		return fail(err)
	end
	local env = setmetatable({}, {
		__index = _G,
		__newindex = function(_, key)
			error(manifest.name .. " tried to set global " .. tostring(key), 2)
		end,
	})
	setfenv(chunk, env)
	local ran, result = pcall(chunk, manifest)
	if not ran then
		return fail(manifest.name .. ": " .. tostring(result))
	end
	M.loaded[manifest.name] = { dir = dir, manifest = manifest, module = result }
	return result
end

local function spec_of(item)
	if type(item) == "string" then
		return {
			url = "https://github.com/" .. item .. ".git",
			name = item:match("[^/]+$"),
		}
	end
	local name = item.name or item.url:match("([^/]+)%.git$") or
	    item.url:match("[^/]+$")
	return { url = item.url, name = name }
end

-- setup(list, {done = fn}) loads every plugin, cloning the missing ones.
function M.setup(list, opts)
	opts = opts or {}
	local pending = 0
	local function finished()
		if pending == 0 and opts.done then
			opts.done(M.errors)
		end
	end
	for _, item in ipairs(list) do
		local spec = spec_of(item)
		local dir = M.dir .. "/" .. spec.name
		if read_file(dir .. "/termo.json") ~= nil then
			M.load(dir)
		else
			pending = pending + 1
			api.system({ "sh", "-c", 'mkdir -p "$1" && git clone --quiet ' ..
			    '--depth 1 "$2" "$3"', "sh", M.dir, spec.url, dir }, {
				on_exit = function(status)
					if status == 0 then
						M.load(dir)
					else
						fail(spec.name .. ": git clone failed (" ..
						    status .. ")")
					end
					pending = pending - 1
					finished()
				end,
			})
		end
	end
	finished()
end

-- update() pulls every loaded plugin; the new code runs after a restart.
function M.update(done)
	local pending = 0
	for name, p in pairs(M.loaded) do
		pending = pending + 1
		api.system({ "git", "-C", p.dir, "pull", "--quiet", "--ff-only" }, {
			on_exit = function(status)
				if status ~= 0 then
					fail(name .. ": git pull failed (" .. status .. ")")
				end
				pending = pending - 1
				if pending == 0 and done then
					done(M.errors)
				end
			end,
		})
	end
	if pending == 0 and done then
		done(M.errors)
	end
end

function M.list()
	local names = {}
	for name in pairs(M.loaded) do
		names[#names + 1] = name
	end
	table.sort(names)
	return names
end

return M
