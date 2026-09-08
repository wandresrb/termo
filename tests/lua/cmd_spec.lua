-- termo.api.cmd and cmd_async in the three contexts: inside the run-lua
-- command that runs this file, from a timer, and inside an event sink.
local api = termo.api

describe("cmd inside a command", function()
	it("queues after the command and returns nothing", function()
		local out, err = api.cmd("set -g @cmd_spec inside")
		eq(out, nil)
		eq(err, nil)
		-- Not run yet: this command is still executing.
		eq(api.get_option("@cmd_spec"), nil)
	end)
	it("keeps consecutive commands in order", function()
		api.cmd("set -g @cmd_order a")
		api.cmd("set -ag @cmd_order b")
		api.cmd_async("set -ag @cmd_order c", function() end)
		api.cmd("set -ag @cmd_order d")
	end)
	it("rejects unparseable commands at once", function()
		eq(fails(api.cmd, "kill-window -Q"):match("unknown flag") ~= nil, true)
		eq(fails(api.cmd, "no-such-command x"):match("unknown command") ~= nil,
		    true)
	end)
end)

describe("cmd from a timer", function()
	it_async("ran the command queued from inside the command", function(done)
		if check(done, api.get_option("@cmd_spec") == "inside") and
		    check(done, api.get_option("@cmd_order") == "abcd",
		    "order " .. tostring(api.get_option("@cmd_order"))) then
			done()
		end
	end)
	it_async("returns captured output", function(done)
		local out, err = api.cmd("display -p hello")
		if check(done, out == "hello", "out=" .. tostring(out)) and
		    check(done, err == nil, "err=" .. tostring(err)) then
			done()
		end
	end)
	it_async("joins multi-line output with newlines", function(done)
		local out = api.cmd("display -p one ; display -p two")
		if check(done, out == "one\ntwo", "out=" .. tostring(out)) then
			done()
		end
	end)
	it_async("returns errors instead of raising", function(done)
		local out, err = api.cmd("kill-window -t no-such-window")
		if check(done, out == nil) and
		    check(done, err ~= nil and err:match("can't find") ~= nil,
		    "err=" .. tostring(err)) then
			done()
		end
	end)
	it_async("reports a waiting command as asynchronous", function(done)
		local out, err = api.cmd("run-shell 'true'")
		if check(done, out == nil) and
		    check(done, err == "asynchronous command, use cmd_async",
		    "err=" .. tostring(err)) then
			-- Let the job finish so the queue is free for the next spec.
			api.cmd_async("display -p drained", function()
				done()
			end)
		end
	end)
end)

describe("cmd_async", function()
	it_async("delivers output when the command finishes", function(done)
		api.cmd_async("run-shell 'printf async-out'", function(out, err)
			if check(done, out == "async-out", "out=" .. tostring(out)) and
			    check(done, err == nil, "err=" .. tostring(err)) then
				done()
			end
		end)
	end)
	it_async("delivers errors", function(done)
		api.cmd_async("select-window -t no-such", function(out, err)
			if check(done, out == nil) and
			    check(done, err ~= nil and err:match("can't find") ~= nil,
			    "err=" .. tostring(err)) then
				done()
			end
		end)
	end)
	it("works inside a command too", function()
		api.cmd_async("display -p later", function(out)
			api.set_option("@cmd_async_inside", tostring(out))
		end)
		eq(api.get_option("@cmd_async_inside"), nil)
	end)
	it_async("and its callback ran after the command", function(done)
		if check(done, api.get_option("@cmd_async_inside") == "later",
		    "got " .. tostring(api.get_option("@cmd_async_inside"))) then
			done()
		end
	end)
	it("wants a function", function()
		eq(fails(api.cmd_async, "display x", "nope"):match("function expected") ~=
		    nil, true)
	end)
end)
