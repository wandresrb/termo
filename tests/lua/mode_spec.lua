local api = termo.api
local mode = termo.mode

describe("termo.mode", function()
	it_async("binds sticky keys, Any and the leave key in the mode table", function(done)
		mode.define("specm", {
			key = "M",
			keys = {
				h = { "select-pane -L", "left" },
				x = { "kill-pane", "kill", once = true },
			},
		})
		local keys = api.cmd("list-keys -T mode-specm")
		eq(keys:find("select%-pane %-L \\; switch%-client %-T mode%-specm") ~= nil, true)
		eq(keys:find("Any%s+switch%-client %-T mode%-specm") ~= nil, true)
		eq(keys:find("Escape%s+switch%-client %-T root") ~= nil, true)
		eq(keys:find("kill%-pane \\;") == nil, true)
		local prefix = api.cmd("list-keys -T prefix M")
		eq(prefix:find("switch%-client %-T mode%-specm") ~= nil, true)
		mode.hints = "mode"
		local line = mode.line("mode-specm")
		eq(line:find("specm") ~= nil, true)
		eq(line:find(" h #%[noreverse%] left") ~= nil, true)
		eq(line:find("Escape") ~= nil, true)
		eq(mode.line("root"), "")
		mode.hints = "always"
		eq(mode.line("root"):find(" M #%[noreverse%] specm") ~= nil, true)
		mode.hints = "mode"
		done()
	end)

	it_async("locks and unlocks the session prefix", function(done)
		local session = api.eval("#{session_id}")
		local before = api.get_option("prefix", session)
		mode.lock("F12")
		eq(api.get_option("prefix", session), "None")
		eq(api.cmd("list-keys -T root F12"):find("run%-lua %-r") ~= nil, true)
		mode.unlock()
		eq(api.get_option("prefix", session), before)
		eq(select(2, api.cmd("list-keys -T root F12")) ~= nil, true)
		done()
	end)
end)
