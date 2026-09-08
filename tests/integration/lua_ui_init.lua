-- Fixture for test_lua_ui.py: bindings that open each kind of UI and
-- record what happened in user options the test reads back.
local termo = termo
local api = termo.api

termo.keymap.set("F5", function(ev)
	api.set_option("@key", ev.key .. "/" .. ev.table .. "/" ..
	    tostring(ev.client ~= nil))
end)

termo.keymap.set("F6", function()
	termo.ui.menu{
		title = "Pick one",
		items = {
			{ "First", "a", function()
				api.set_option("@picked", "one")
			end },
			{ "Second", "b", function(index, key)
				api.set_option("@picked", "two/" .. index .. "/" .. key)
			end },
			{},
			{ "Third", "c", "set -g @picked three" },
		},
		on_close = function()
			api.set_option("@menu_closed", "yes")
		end,
	}
end)

termo.keymap.set("F7", function()
	termo.ui.prompt("Name:", function(text, done)
		api.set_option("@prompt", tostring(text) .. "/" .. tostring(done))
	end)
end)

termo.keymap.set("F8", function()
	termo.ui.popup{
		cmd = "printf popup-ok; sleep 0.3",
		w = 40,
		h = 8,
		on_close = function(status)
			api.set_option("@popup", tostring(status))
		end,
	}
end)

termo.keymap.set("F9", function()
	termo.ui.message("hello from lua")
end)

termo.keymap.set("F10", function()
	termo.palette.open()
end)
termo.palette.setup{ commands = false }
termo.palette.add("Mark spot", "set -g @palette marked")

termo.format.add("lua_status", function()
	return "lua-status-ok"
end)
api.set_option("status-right", "#{lua_status}")

termo.on("@ui_ready", function()
	api.set_option("@ready", "yes")
end)
termo.emit("@ui_ready")
