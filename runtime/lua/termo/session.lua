local api = termo.api
local M = {
	argv = {},
	delay = 30000,
	pending = false,
	exiting = false,
	dirty = {},
	snaps = {},
	written = {},
	names = {},
	reviving = {},
}

local function env_dir(var, fallback)
	local v = os.getenv(var)
	if v == nil or v == "" then
		v = (os.getenv("HOME") or "") .. fallback
	end
	return v
end

M.dir = env_dir("XDG_DATA_HOME", "/.local/share") .. "/termo/sessions"

local LEVEL = { off = 0, on = 1, layout = 1, commands = 2, screen = 3 }

local function quote(s)
	return "'" .. tostring(s):gsub("'", "'\\''") .. "'"
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

local function file_of(name)
	return M.dir .. "/" .. name .. ".json"
end

local function screen_of(name, window, n)
	return M.dir .. "/" .. name .. "." .. window .. "." .. n .. ".screen"
end

local function level(target)
	return LEVEL[tostring(api.get_option("resurrect", target) or "off")] or 0
end

local function checksum(s)
	local sum = 0
	for i = 1, #s do
		sum = (math.floor(sum / 2) + (sum % 2) * 32768 + s:byte(i)) % 65536
	end
	return string.format("%04x", sum)
end

local function tiled_layout(layout)
	local body = layout:match("^%x+,(.*)$")
	if body == nil or not body:find("<", 1, true) then
		return layout
	end
	body = body:gsub("<.*>$", "")
	return checksum(body) .. "," .. body
end

local function leaf_ids(layout)
	local ids = {}
	for id in layout:gsub("^%x+,", ""):gmatch("%d+x%d+,%d+,%d+,(%d+)") do
		ids[#ids + 1] = "%" .. id
	end
	return ids
end

local function allowed(cmd, list)
	local prog = (cmd:match("^(%S+)") or ""):match("([^/]+)$")
	for word in (list or ""):gmatch("%S+") do
		if word == prog then
			return true
		end
	end
	return false
end

local function snapshot(session, capture)
	local lvl = level(session)
	if lvl == 0 then
		return nil
	end
	local name = api.eval("#{session_name}", session)
	local list = api.get_option("resurrect-commands", session)
	local out = {
		version = 1,
		name = name,
		mode = tostring(api.get_option("resurrect", session)),
		cwd = api.eval("#{session_path}", session),
		windows = {},
	}
	for _, w in ipairs(api.list_windows(session)) do
		local layout = tiled_layout(api.eval("#{window_layout}", w.id))
		local win = {
			index = w.index,
			name = w.name,
			active = w.active,
			zoomed = api.eval("#{window_zoomed_flag}", w.id) == "1",
			width = tonumber(api.eval("#{window_width}", w.id)),
			height = tonumber(api.eval("#{window_height}", w.id)),
			layout = layout,
			panes = {},
		}
		for n, id in ipairs(leaf_ids(layout)) do
			local pane = {
				cwd = api.eval("#{pane_current_path}", id),
				active = api.eval("#{pane_active}", id) == "1",
			}
			if lvl >= LEVEL.commands then
				local cmd = M.argv[api.eval("#{pane_pid}", id)]
				if cmd and allowed(cmd, list) then
					pane.cmd = cmd
				end
			end
			if lvl >= LEVEL.screen then
				pane.screen = screen_of(name, w.index, n)
				if capture then
					api.cmd_async(string.format(
					    "capture-pane -e -J -S - -b termo-resurrect -t %s ; " ..
					    "save-buffer -b termo-resurrect %s ; " ..
					    "delete-buffer -b termo-resurrect",
					    id, quote(pane.screen)), function() end)
				end
			end
			win.panes[#win.panes + 1] = pane
		end
		out.windows[#out.windows + 1] = win
	end
	return out
end

local function read_index()
	local names = {}
	for name in (read_file(M.dir .. "/.index") or ""):gmatch("[^\n]+") do
		names[#names + 1] = name
	end
	return names
end

local function write_index(names)
	table.sort(names)
	local text = table.concat(names, "\n") .. (#names > 0 and "\n" or "")
	if text ~= read_file(M.dir .. "/.index") then
		api.write_file(M.dir .. "/.index", text)
	end
end

local function index_set(add, remove)
	local seen, names = {}, {}
	for _, name in ipairs(read_index()) do
		if name ~= remove and not seen[name] then
			seen[name] = true
			names[#names + 1] = name
		end
	end
	if add and not seen[add] then
		names[#names + 1] = add
	end
	write_index(names)
end

local function forget(name)
	local ok, snap = pcall(termo.json.decode, read_file(file_of(name)) or "")
	if ok and type(snap) == "table" then
		for _, win in ipairs(snap.windows or {}) do
			for _, pane in ipairs(win.panes or {}) do
				if pane.screen then
					os.remove(pane.screen)
				end
			end
		end
	end
	os.remove(file_of(name))
	M.written[name] = nil
	index_set(nil, name)
end

local function live_ids()
	local ids = {}
	for _, s in ipairs(api.list_sessions()) do
		ids[s.id] = s.name
	end
	return ids
end

local function store(id, snap)
	local old = M.names[id]
	if old and old ~= snap.name then
		forget(old)
	end
	M.names[id] = snap.name
	local text = termo.json.encode(snap)
	if M.written[snap.name] == text then
		return false
	end
	local ok, err = api.write_file(file_of(snap.name), text)
	if not ok then
		print("session: " .. tostring(err))
		return false
	end
	M.written[snap.name] = text
	index_set(snap.name)
	return true
end

local function flush(capture)
	local live = live_ids()
	local wrote = {}
	for id in pairs(M.dirty) do
		local snap
		if live[id] then
			snap = snapshot(id, capture)
		else
			snap = M.snaps[id]
		end
		if snap and store(id, snap) then
			wrote[#wrote + 1] = snap.name
		end
	end
	M.dirty = {}
	M.snaps = {}
	return wrote
end

local function refresh_argv(done)
	local by_parent = {}
	api.system({ "ps", "-ao", "ppid=,args=" }, {
		on_stdout = function(line)
			local ppid, args = line:match("^%s*(%d+)%s+(.*)$")
			if ppid and by_parent[ppid] == nil then
				by_parent[ppid] = args
			end
		end,
		on_exit = function()
			M.argv = by_parent
			done()
		end,
	})
end

function M.save_all(done)
	for id in pairs(live_ids()) do
		M.dirty[id] = true
	end
	refresh_argv(function()
		local wrote = flush(true)
		if done then
			done(wrote)
		end
	end)
end

local function mark(ids)
	local live = live_ids()
	for _, id in ipairs(ids) do
		if live[id] then
			M.dirty[id] = true
			local snap = snapshot(id, false)
			if snap then
				M.snaps[id] = snap
			end
		end
	end
	if M.pending or M.exiting then
		return
	end
	M.pending = true
	api.defer(M.delay, function()
		M.pending = false
		refresh_argv(function()
			flush(true)
		end)
	end)
end

local function affected(ev)
	if ev.session then
		return { ev.session }
	end
	local target = ev.window or ev.pane
	if target == nil then
		return {}
	end
	local by_name = {}
	for id, name in pairs(live_ids()) do
		by_name[name] = id
	end
	local ids = {}
	for name in api.eval("#{window_linked_sessions_list}", target):gmatch("[^,]+") do
		if by_name[name] then
			ids[#ids + 1] = by_name[name]
		end
	end
	return ids
end

local function exists(name)
	for _, s in ipairs(api.list_sessions()) do
		if s.name == name then
			return true
		end
	end
	return false
end

function M.list()
	local out = {}
	for _, name in ipairs(read_index()) do
		out[#out + 1] = { name = name, alive = exists(name) }
	end
	return out
end

function M.del(name)
	if read_file(file_of(name)) == nil then
		return false
	end
	forget(name)
	return true
end

function M.clean()
	local removed = {}
	for _, s in ipairs(M.list()) do
		if not s.alive then
			forget(s.name)
			removed[#removed + 1] = s.name
		end
	end
	return removed
end

local function shell()
	local cmd = api.get_option("default-command") or ""
	if cmd ~= "" then
		return cmd
	end
	return api.get_option("default-shell")
end

local function pane_command(pane)
	if pane.screen == nil or read_file(pane.screen) == nil then
		return ""
	end
	return " " .. quote("cat " .. quote(pane.screen) .. "; exec " .. shell())
end

function M.revive(name)
	if M.reviving[name] then
		return nil, name .. ": already reviving"
	end
	if exists(name) then
		return nil, name .. ": exists"
	end
	local text = read_file(file_of(name))
	if text == nil then
		return nil, file_of(name) .. ": not found"
	end
	local ok, snap = pcall(termo.json.decode, text)
	if not ok or type(snap) ~= "table" or snap.version ~= 1 then
		return nil, file_of(name) .. ": not a session"
	end
	if exists(snap.name) then
		return nil, snap.name .. ": exists"
	end
	M.reviving[name] = true
	api.defer(5000, function()
		M.reviving[name] = nil
	end)
	local pane_base = tonumber(api.get_option("pane-base-index")) or 0
	local base = tonumber(api.get_option("base-index")) or 0
	local active_window
	for w, win in ipairs(snap.windows) do
		local target = quote("=" .. snap.name .. ":" .. win.index)
		local first = win.panes[1] or { cwd = snap.cwd }
		if w == 1 then
			api.cmd(string.format("new-session -d -s %s -c %s -x %d -y %d -n %s%s",
			    quote(snap.name), quote(first.cwd), win.width or 80,
			    win.height or 24, quote(win.name), pane_command(first)))
			if win.index ~= base then
				api.cmd(string.format("move-window -d -s %s -t %s",
				    quote("=" .. snap.name .. ":" .. base), target))
			end
		else
			api.cmd(string.format("new-window -d -t %s -c %s -n %s%s", target,
			    quote(first.cwd), quote(win.name), pane_command(first)))
		end
		for n = 2, #win.panes do
			local pane = win.panes[n]
			api.cmd(string.format("split-window -d -t %s -c %s%s",
			    quote("=" .. snap.name .. ":" .. win.index .. "." .. (pane_base + n - 2)),
			    quote(pane.cwd), pane_command(pane)))
			api.cmd("select-layout -t " .. target .. " tiled")
		end
		api.cmd("select-layout -t " .. target .. " " .. quote(win.layout))
		for n, pane in ipairs(win.panes) do
			local ptarget = quote("=" .. snap.name .. ":" .. win.index .. "." ..
			    (pane_base + n - 1))
			if pane.cmd then
				api.cmd("send-keys -t " .. ptarget .. " -l " .. quote(pane.cmd))
			end
			if pane.active then
				api.cmd("select-pane -t " .. ptarget)
			end
		end
		if win.zoomed then
			api.cmd("resize-pane -Z -t " .. target)
		end
		if win.active then
			active_window = target
		end
	end
	if active_window then
		api.cmd("select-window -t " .. active_window)
	end
	M.written[snap.name] = text
	return snap.name
end

function M.menu()
	local items = {}
	for _, s in ipairs(M.list()) do
		if not s.alive then
			local name = s.name
			items[#items + 1] = { name, nil, function()
				if M.revive(name) then
					api.cmd("switch-client -t " .. quote("=" .. name))
				end
			end }
		end
	end
	if #items == 0 then
		return termo.ui.message("no saved sessions")
	end
	termo.ui.menu({ title = "Saved sessions", items = items })
end

local EVENTS = {
	"session-created", "session-renamed", "session-window-changed",
	"window-linked", "window-unlinked", "window-renamed", "window-layout-changed",
	"window-pane-changed", "pane-exited", "pane-died", "pane-shell-prompt",
	"pane-command-finished",
}

for _, name in ipairs(EVENTS) do
	api.on(name, function(ev)
		mark(affected(ev))
	end)
end

api.on("server-exit", function()
	M.exiting = true
	for id in pairs(live_ids()) do
		M.dirty[id] = true
	end
	flush(false)
end)

if termo.palette then
	termo.palette.add("Saved sessions", function()
		M.menu()
	end)
end

return M
