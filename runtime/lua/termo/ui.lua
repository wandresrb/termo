-- Menus, popups, messages and prompts with friendlier argument forms.
local api = termo.api
local M = {}

-- menu{title = ..., items = {{"Name", "k", fn or "command"}, ...}}
function M.menu(spec)
	return api.menu(spec)
end

-- popup("command") or popup{cmd = ..., w = ..., h = ..., on_close = fn}
function M.popup(spec)
	if type(spec) == "string" then
		spec = { cmd = spec }
	end
	return api.popup(spec)
end

-- message("fmt", ...) with string.format when there are arguments.
function M.message(fmt, ...)
	if select("#", ...) > 0 then
		fmt = string.format(fmt, ...)
	end
	return api.message(fmt)
end

-- prompt("label", fn(text, done), opts); fn(nil) when cancelled.
function M.prompt(label, fn, opts)
	return api.prompt(label, fn, opts)
end

-- confirm("question", fn(yes)): one key, y or n.
function M.confirm(question, fn)
	return api.prompt(question .. " (y/n)", function(text)
		fn(text ~= nil and text:lower() == "y")
	end, { single = true })
end

return M
