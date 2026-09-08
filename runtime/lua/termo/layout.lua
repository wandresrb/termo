-- Declarative layouts. A node is a leaf {cmd = "..."} or a container
-- {dir = "h" | "v", child, child, ...}; every child may carry size, a
-- split-window -l value used when it is split off its previous sibling
-- (equal shares when omitted). apply() runs on the target pane and calls
-- done(err) when the last split has happened, since each split needs the
-- id the previous one printed.
--
--   termo.layout.apply({ dir = "h",
--       { cmd = "vim" },
--       { dir = "v", size = "35%", { cmd = "htop" }, {} },
--   })
local api = termo.api
local M = {}

local function quote(s)
	return "'" .. s:gsub("'", "'\\''") .. "'"
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

-- apply(spec, {target = pane or window handle, done = fn(err)})
function M.apply(spec, opts)
	opts = opts or {}
	local done = opts.done or function(err)
		if err then
			print("layout: " .. err)
		end
	end
	local target = opts.target
	if target == nil then
		target = api.eval("#{pane_id}")
	end
	apply_node(spec, target, done)
end

return M
