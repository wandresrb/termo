local api = termo.api
local M = { commands = {} }

local function arity_ok(nargs, n)
	if nargs == nil or nargs == "*" then
		return true
	elseif nargs == "?" then
		return n <= 1
	elseif nargs == "+" then
		return n >= 1
	end
	return n == nargs
end

function M.define(name, fn, opts)
	opts = opts or {}
	local entry = { fn = fn, desc = opts.desc, nargs = opts.nargs }
	api.command_set(name, function(ev)
		if not arity_ok(opts.nargs, #ev.args) then
			error(name .. ": expected " .. tostring(opts.nargs) ..
			    " argument(s), got " .. #ev.args, 0)
		end
		fn(ev.args, ev)
	end)
	M.commands[name] = entry
	if opts.desc and termo.palette then
		for _, e in ipairs(termo.palette.entries) do
			if e.action == name then
				return entry
			end
		end
		termo.palette.add(opts.desc, name, { desc = name })
	end
	return entry
end

function M.del(name)
	M.commands[name] = nil
	return api.command_del(name)
end

function M.list()
	local names = {}
	for name in pairs(M.commands) do
		names[#names + 1] = name
	end
	table.sort(names)
	return names
end

return M
