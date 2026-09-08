-- A reference ~/.config/termo/init.lua: one of everything. It is loaded
-- after termo.conf, so commands and Lua mix freely; the integration test
-- runs it from new-session to kill-server under the sanitizers.

-- Options, like set -g but typed.
termo.opt.history_limit = 100000
termo.opt.mouse = true

-- A key bound to a function. ev has client, key, table and mouse.
termo.keymap.set("C-t", function(ev)
	termo.ui.message("you pressed %s in %s", ev.key, ev.table)
end)

-- A key bound to commands, exactly like bind-key.
termo.keymap.set("|", "split-window -h -c '#{pane_current_path}'")

-- A hook: every event set-hook knows, with its payload as a table.
termo.on("window-renamed", function(ev)
	termo.cmd("display-message 'window " .. ev.window .. " renamed'")
end)

-- A status line variable computed in Lua; eval() sees the pane being drawn.
termo.format.add("branch", function()
	local dir = termo.eval("#{pane_current_path}")
	local f = io.open(dir .. "/.git/HEAD")
	if f == nil then
		return ""
	end
	local head = f:read("*l") or ""
	f:close()
	return " " .. (head:match("refs/heads/(.*)") or head:sub(1, 7))
end)
termo.opt.status_right = "#{branch} %H:%M"

-- A menu and a popup on keys.
termo.keymap.set("m", function()
	termo.ui.menu{
		title = "Panes",
		items = {
			{ "Split right", "r", "split-window -h" },
			{ "Split down", "d", "split-window -v" },
			{},
			{ "Kill", "x", function()
				termo.ui.confirm("Kill this pane?", function(yes)
					if yes then
						termo.cmd("kill-pane")
					end
				end)
			end },
		},
	}
end)
termo.keymap.set("g", function()
	termo.ui.popup{ cmd = "git log --oneline -20", w = 80, h = 24, close = "any" }
end)

-- A modal key table with a hint bar, and a command palette.
termo.hints.setup()
termo.hints.mode("resize", {
	key = "R",
	keys = {
		h = { "resize-pane -L 5", "left" },
		j = { "resize-pane -D 5", "down" },
		k = { "resize-pane -U 5", "up" },
		l = { "resize-pane -R 5", "right" },
	},
})
termo.palette.setup{ key = "p" }
termo.palette.add("Rename window", "command-prompt -I '#W' 'rename-window %%'")

-- A process, without blocking the server.
termo.system({ "uname", "-s" }, {
	on_stdout = function(line)
		termo.api.set_option("@os", line)
	end,
})
