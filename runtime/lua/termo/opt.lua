-- Options as a table: termo.opt.history_limit = 50000 is
-- set_option("history-limit", 50000); underscores become dashes so names
-- work without quoting. termo.opt.of("@1") is the same view of a window,
-- session or pane.
local api = termo.api

local function name(key)
	return (key:gsub("_", "-"))
end

local function proxy(target)
	return setmetatable({}, {
		__index = function(_, key)
			return api.get_option(name(key), target)
		end,
		__newindex = function(_, key, value)
			api.set_option(name(key), value, target)
		end,
	})
end

local M = proxy(nil)
rawset(M, "of", proxy)
return M
