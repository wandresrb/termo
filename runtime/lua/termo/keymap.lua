-- Key bindings. set(key, rhs) binds after the prefix; set(table, key, rhs)
-- binds in any key table (root, copy-mode-vi, or one of your own). rhs is
-- a command string or a function called with {client, key, table, mouse}.
local api = termo.api
local M = {}

local function bindable(v)
	return type(v) == "string" or type(v) == "function"
end

function M.set(a, b, c, d)
	if bindable(c) then
		return api.keymap_set(a, b, c, d)
	end
	return api.keymap_set("prefix", a, b, c)
end

function M.del(a, b)
	if b ~= nil then
		return api.keymap_del(a, b)
	end
	return api.keymap_del("prefix", a)
end

-- A binding that works without the prefix.
function M.root(key, rhs, opts)
	return api.keymap_set("root", key, rhs, opts)
end

return M
