-- Modal key tables with a hint bar, Zellij style. A mode is a key table
-- whose bindings return to the same table, so keys repeat until Escape;
-- while a client is in a mode, the second status line shows its keys.
--
--   termo.hints.mode("pane", { key = "p", keys = {
--       h = { "select-pane -L", "left" },
--       l = { "select-pane -R", "right" },
--       x = { "kill-pane", "kill", once = true },
--   } })
--   termo.hints.setup()
local api = termo.api
local M = { modes = {} }

local function table_name(name)
	return "mode-" .. name
end

-- mode(name, {key = prefix key, keys = {k = {rhs, label, once}}})
function M.mode(name, spec)
	local tname = table_name(name)
	spec.name = name
	M.modes[name] = spec
	for key, entry in pairs(spec.keys) do
		local rhs, label = entry[1], entry[2]
		if not entry.once then
			if type(rhs) == "string" then
				rhs = rhs .. " ; switch-client -T " .. tname
			else
				local fn = rhs
				rhs = function(ev)
					fn(ev)
					api.cmd("switch-client -T " .. tname)
				end
			end
		elseif type(rhs) == "string" then
			rhs = rhs .. " ; set -g status " .. M.saved_status
		end
		api.keymap_set(tname, key, rhs, { note = label })
	end
	api.keymap_set(tname, "Escape", "switch-client -T root ; set -g status " ..
	    M.saved_status, { note = "leave " .. name .. " mode" })
	if spec.key then
		api.keymap_set("prefix", spec.key, "switch-client -T " .. tname ..
		    " ; set -g status 2", { note = name .. " mode" })
	end
end

function M.enter(name)
	api.cmd("switch-client -T " .. table_name(name) .. " ; set -g status 2")
end

-- The hint bar text for a key table name; empty outside a mode.
function M.line(tname)
	local spec = M.modes[(tname or ""):match("^mode%-(.*)$") or ""]
	if spec == nil then
		return ""
	end
	local keys = {}
	for key in pairs(spec.keys) do
		keys[#keys + 1] = key
	end
	table.sort(keys)
	local parts = { "#[bold]" .. spec.name .. "#[nobold]" }
	for _, key in ipairs(keys) do
		parts[#parts + 1] = "#[reverse] " .. key .. " #[noreverse] " ..
		    (spec.keys[key][2] or "")
	end
	parts[#parts + 1] = "#[reverse] Esc #[noreverse] leave"
	return table.concat(parts, "  ")
end

-- setup() puts the bar in status-format[1]; status becomes 2 lines while
-- a mode is active.
function M.setup()
	M.saved_status = tostring(api.get_option("status") or "on")
	if M.saved_status == "true" then
		M.saved_status = "on"
	elseif M.saved_status == "false" then
		M.saved_status = "off"
	end
	api.format_add("hints", function()
		return M.line(api.eval("#{client_key_table}"))
	end)
	api.cmd("set -g 'status-format[1]' '#[align=centre]#{hints}'")
end

return M
