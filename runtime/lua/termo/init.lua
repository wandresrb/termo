-- The Lua side of termo. termo.api holds the C functions; this module adds
-- the ergonomics on top of them and is loaded once when the server starts.
local termo = termo
local api = termo.api

termo.version = api.version
termo.cmd = api.cmd
termo.cmd_async = api.cmd_async
termo.eval = api.eval
termo.on = api.on
termo.off = api.off
termo.emit = api.emit
termo.defer = api.defer
termo.timer = api.timer
termo.system = api.system

termo.format = {
	add = api.format_add,
	del = function(name)
		api.format_add(name, nil)
	end,
}

termo.json = require("termo.json")
termo.keymap = require("termo.keymap")
termo.opt = require("termo.opt")
termo.ui = require("termo.ui")
termo.layout = require("termo.layout")
termo.hints = require("termo.hints")
termo.palette = require("termo.palette")
termo.float = require("termo.float")
termo.pack = require("termo.pack")

return termo
