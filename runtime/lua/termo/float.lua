-- Floating panes, which the core already has (new-pane, break-pane -W,
-- move-pane -P): a small vocabulary and optional default bindings.
--
--   termo.float.setup{ new = "C-f", toggle = "F", front = "M-f" }
local api = termo.api
local M = {}

local function quote(s)
	return "'" .. s:gsub("'", "'\\''") .. "'"
end

-- new{cmd, w, h, x, y}: a floating pane, sizes and positions as new-pane
-- takes them (columns and lines, or percentages with %).
function M.new(spec)
	spec = spec or {}
	if type(spec) == "string" then
		spec = { cmd = spec }
	end
	local parts = { "new-pane" }
	if spec.w then
		parts[#parts + 1] = "-x " .. spec.w
	end
	if spec.h then
		parts[#parts + 1] = "-y " .. spec.h
	end
	if spec.x then
		parts[#parts + 1] = "-X " .. spec.x
	end
	if spec.y then
		parts[#parts + 1] = "-Y " .. spec.y
	end
	if spec.cmd then
		parts[#parts + 1] = quote(spec.cmd)
	end
	return api.cmd(table.concat(parts, " "))
end

function M.is_floating(pane)
	return api.eval("#{pane_floating_flag}", pane) == "1"
end

-- toggle(pane): a tiled pane floats, a floating pane joins the layout
-- next to the window's active tiled pane.
function M.toggle(pane)
	pane = pane or api.eval("#{pane_id}")
	if not M.is_floating(pane) then
		return api.cmd("break-pane -W -s " .. pane)
	end
	local window = api.eval("#{window_id}", pane)
	for _, p in ipairs(api.list_panes(window)) do
		if not p.floating then
			return api.cmd("join-pane -h -s " .. pane .. " -t " .. p.id)
		end
	end
end

-- move(position[, pane]): a move-pane -P position such as centre,
-- top-right, front or back.
function M.move(position, pane)
	pane = pane or api.eval("#{pane_id}")
	return api.cmd("move-pane -P " .. position .. " -s " .. pane)
end

-- setup{new = key, toggle = key, front = key, back = key, centre = key}
-- binds the keys given after the prefix.
function M.setup(keys)
	keys = keys or {}
	if keys.new then
		api.keymap_set("prefix", keys.new, "new-pane", { note = "new floating pane" })
	end
	if keys.toggle then
		api.keymap_set("prefix", keys.toggle, function()
			M.toggle()
		end, { note = "float or tile the pane" })
	end
	for _, position in ipairs({ "front", "back", "centre" }) do
		if keys[position] then
			api.keymap_set("prefix", keys[position],
			    "move-pane -P " .. position, { note = "floating pane " .. position })
		end
	end
end

return M
