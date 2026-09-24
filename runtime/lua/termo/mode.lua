local api = termo.api
local M = { modes = {}, hints = "mode", armed = false, previous = {} }

local function tname(name)
	return "mode-" .. name
end

local function mode_of(table_name)
	return M.modes[(table_name or ""):match("^mode%-(.*)$") or ""]
end

local function status_value()
	local s = tostring(api.get_option("status") or "on")
	if s == "true" then
		return "on"
	elseif s == "false" then
		return "off"
	end
	return s
end

local function set_status(value)
	if status_value() ~= value then
		api.set_option("status", value)
	end
end

local function on_table(ev)
	local now = mode_of(ev.key_table)
	local was = M.previous[ev.client]
	M.previous[ev.client] = now
	if was ~= nil and was ~= now and was.on_leave then
		was.on_leave(ev)
	end
	if now ~= nil and now ~= was and now.on_enter then
		now.on_enter(ev)
	end
	if M.hints == "mode" then
		set_status(now and "2" or M.saved_status)
	end
end

function M.define(name, spec)
	local t = tname(name)
	spec.name = name
	M.modes[name] = spec
	for key, entry in pairs(spec.keys or {}) do
		local rhs, label = entry[1], entry[2]
		if not entry.once then
			if type(rhs) == "string" then
				rhs = rhs .. " ; switch-client -T " .. t
			else
				local fn = rhs
				rhs = function(ev)
					fn(ev)
					api.cmd("switch-client -T " .. t)
				end
			end
		end
		api.keymap_set(t, key, rhs, { note = label })
	end
	api.keymap_set(t, "Any", "switch-client -T " .. t, { note = "stay in " .. name })
	for _, key in ipairs(spec.leave or { "Escape" }) do
		api.keymap_set(t, key, "switch-client -T root", { note = "leave " .. name })
	end
	if spec.key then
		api.keymap_set(spec.table or "prefix", spec.key, "switch-client -T " .. t,
		    { note = name .. " mode" })
	end
	return t
end

function M.enter(name)
	api.cmd("switch-client -T " .. tname(name))
end

function M.leave()
	api.cmd("switch-client -T root")
end

function M.current()
	local spec = mode_of(api.eval("#{client_key_table}"))
	return spec and spec.name
end

local function key_list(spec)
	local keys = {}
	for key in pairs(spec.keys or {}) do
		keys[#keys + 1] = key
	end
	table.sort(keys)
	return keys
end

function M.line(table_name)
	local spec = mode_of(table_name)
	if spec == nil then
		if M.hints ~= "always" then
			return ""
		end
		local names, parts = {}, {}
		for name, s in pairs(M.modes) do
			if s.key then
				names[#names + 1] = name
			end
		end
		table.sort(names)
		for _, name in ipairs(names) do
			parts[#parts + 1] = "#[reverse] " .. M.modes[name].key .. " #[noreverse] " .. name
		end
		return table.concat(parts, "  ")
	end
	local parts = { "#[bold]" .. spec.name .. "#[nobold]" }
	for _, key in ipairs(key_list(spec)) do
		parts[#parts + 1] = "#[reverse] " .. key .. " #[noreverse] " ..
		    (spec.keys[key][2] or "")
	end
	parts[#parts + 1] = "#[reverse] " .. (spec.leave or { "Escape" })[1] ..
	    " #[noreverse] leave"
	return table.concat(parts, "  ")
end

function M.setup(opts)
	opts = opts or {}
	M.hints = opts.hints or api.get_option("@hints") or "mode"
	if M.saved_status == nil then
		M.saved_status = status_value()
	end
	api.format_add("hints", function()
		if termo.palette and termo.palette.is_open then
			return ""
		end
		return M.line(api.eval("#{client_key_table}"))
	end)
	api.cmd("set -g 'status-format[1]' '#[align=centre]#{palette}#{hints}'")
	if M.hints == "always" then
		set_status("2")
	end
	if not M.armed then
		M.armed = true
		api.on("client-key-table-changed", on_table)
	end
end

function M.lock(unlock_key)
	local session = api.eval("#{session_id}")
	M.locked = { key = unlock_key, session = session,
	    prefix = api.get_option("prefix", session) }
	api.set_option("prefix", "None", session)
	api.keymap_set("root", unlock_key, function()
		M.unlock()
	end, { note = "unlock" })
end

function M.unlock()
	local l = M.locked
	if l == nil then
		return
	end
	M.locked = nil
	api.set_option("prefix", l.prefix, l.session)
	api.keymap_del("root", l.key)
end

return M
