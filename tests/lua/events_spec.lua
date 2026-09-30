-- termo.api.on/off/emit: custom events round-trip synchronously; built-in
-- events fire from commands, which run after this run-lua command, so
-- those checks are asynchronous.
local api = termo.api

describe("custom events", function()
	it("round-trip a payload of strings, numbers and booleans", function()
		local got
		local id = api.on("@spec", function(p)
			got = p
		end)
		api.emit("@spec", { s = "text", n = 42, f = 1.5, b = true })
		eq(type(id), "number")
		eq(got.event, "@spec")
		eq(got.s, "text")
		eq(got.n, 42)
		eq(got.f, "1.5")
		eq(got.b, 1)
		eq(api.off(id), true)
		eq(api.off(id), false)
	end)
	it("call every handler even when one throws", function()
		local calls = 0
		local a = api.on("@spec2", function()
			calls = calls + 1
			error("first handler fails")
		end)
		local b = api.on("@spec2", function()
			calls = calls + 1
		end)
		api.emit("@spec2")
		eq(calls, 2)
		api.off(a)
		api.off(b)
	end)
	it("stop calling a handler removed from inside another", function()
		local seen = {}
		local ids = {}
		ids[1] = api.on("@spec3", function()
			seen[#seen + 1] = 1
			api.off(ids[2])
		end)
		ids[2] = api.on("@spec3", function()
			seen[#seen + 1] = 2
		end)
		api.emit("@spec3")
		api.emit("@spec3")
		eq(#seen, 2)
		eq(seen[1], 1)
		eq(seen[2], 1)
		api.off(ids[1])
	end)
	it("reject unknown event names and bad payloads", function()
		eq(fails(api.on, "no-such-event", print), "no such event: no-such-event")
		eq(fails(api.emit, "@x", { [1] = "a" }), "payload keys must be strings")
		eq(fails(api.emit, "@x", { t = {} }):match("must be a string") ~= nil,
		    true)
	end)
	it("fire hooks set with set-hook too", function()
		api.cmd("set-hook -g @spec_hook 'set -g @hooked yes'")
		api.cmd("run-lua 'termo.api.emit(\"@spec_hook\")'")
	end)
	it_async("and the hook ran", function(done)
		if check(done, api.get_option("@hooked") == "yes") then
			done()
		end
	end)
end)

describe("built-in events", function()
	local renamed, created, closed
	it("register handlers", function()
		renamed = nil
		api.on("window-renamed", function(p)
			renamed = p
		end)
		api.on("window-created", function(p)
			created = p
		end)
		api.on("window-closed", function(p)
			closed = p
		end)
		api.on("session-renamed", function(p)
			api.set_option("@session_renamed", p.session)
		end)
	end)
	it_async("window-renamed carries the window handle", function(done)
		local out, err = api.cmd("rename-window -t @0 spec-name")
		if check(done, err == nil, tostring(err)) and
		    check(done, renamed ~= nil, "no event") and
		    check(done, renamed.window == "@0", tostring(renamed.window)) and
		    check(done, renamed.event == "window-renamed") then
			done()
		end
	end)
	it_async("window-created and window-closed fire once each", function(done)
		created, closed = nil, nil
		local id = api.cmd("new-window -d -P -F '#{window_id}'")
		if not check(done, id ~= nil and id:match("^@%d+$") ~= nil,
		    "new-window gave " .. tostring(id)) then
			return
		end
		if not check(done, created ~= nil and created.window == id,
		    "created " .. tostring(created and created.window)) then
			return
		end
		api.cmd("kill-window -t " .. id)
		if check(done, closed ~= nil and closed.window == id,
		    "closed " .. tostring(closed and closed.window)) then
			done()
		end
	end)
	it_async("session-renamed reaches a handler that sets an option",
	    function(done)
		api.cmd("rename-session spec-session")
		api.cmd("rename-session 0")
		if check(done, api.get_option("@session_renamed") == "$0") then
			done()
		end
	end)
end)

describe("commands from a sink", function()
	it_async("run after the operation that fired the event", function(done)
		local a = api.cmd("new-window -d -P -F '#{window_id}'")
		local b = api.cmd("new-window -d -P -F '#{window_id}'")
		local fired, exists_during = 0, nil
		local id
		id = api.on("window-layout-changed", function(p)
			if p.window ~= a then
				return
			end
			fired = fired + 1
			api.off(id)
			-- Queued, not run: a must still exist when the sink returns.
			api.cmd("kill-window -t " .. a)
			exists_during = api.eval("#{window_id}", a)
		end)
		local _, err = api.cmd("join-pane -d -s " .. b .. " -t " .. a)
		if not check(done, err == nil, tostring(err)) then
			return
		end
		if not check(done, fired == 1, "fired " .. fired) then
			return
		end
		if not check(done, exists_during == a, "gone during sink") then
			return
		end
		-- The kill ran when the queue drained after join-pane.
		local gone = select(2, pcall(api.eval, "#{window_id}", a))
		if check(done, gone ~= nil and gone:match("no such target") ~= nil,
		    "window still there") then
			done()
		end
	end)
end)
