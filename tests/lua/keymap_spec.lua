-- termo.api.keymap_set/keymap_del: functions become "run-lua -r" bindings,
-- strings are parsed like bind-key. A real key press needs a client, which
-- the integration test provides; here the binding is run through its
-- command form.
local api = termo.api

describe("keymap", function()
	it("binds a command string", function()
		api.keymap_set("prefix", "F11", "display-message hi",
		    { note = "spec note" })
	end)
	it("binds a function", function()
		api.keymap_set("prefix", "F12", function(ev)
			api.set_option("@keymap_ev", tostring(ev.key) .. "/" ..
			    tostring(ev.table))
		end)
	end)
	it("rejects unknown keys and bad handlers", function()
		eq(fails(api.keymap_set, "prefix", "NoSuchKey", "x"),
		    "unknown key: NoSuchKey")
		eq(fails(api.keymap_set, "prefix", "F1", 42):match("function expected") ~=
		    nil, true)
		eq(fails(api.keymap_set, "prefix", "F1", "no-such-cmd"):match(
		    "unknown command") ~= nil, true)
	end)
	it_async("shows both in list-keys", function(done)
		local keys = api.cmd("list-keys -T prefix F11") or ""
		local fn = api.cmd("list-keys -T prefix F12") or ""
		local note = api.cmd("list-keys -N -T prefix F11") or ""
		if check(done, keys:match("display%-message hi") ~= nil, keys) and
		    check(done, fn:match("run%-lua %-r %d+") ~= nil, fn) and
		    check(done, note:match("spec note") ~= nil, note) then
			done()
		end
	end)
	it_async("runs the function with the key event", function(done)
		local fn = api.cmd("list-keys -T prefix F12") or ""
		local ref = fn:match("run%-lua %-r (%d+)")
		if not check(done, ref ~= nil, fn) then
			return
		end
		local _, err = api.cmd("run-lua -r " .. ref)
		if check(done, err == nil, tostring(err)) and
		    check(done, api.get_option("@keymap_ev") == "nil/prefix",
		    tostring(api.get_option("@keymap_ev"))) then
			done()
		end
	end)
	it_async("removes bindings and rejects stale refs", function(done)
		local fn = api.cmd("list-keys -T prefix F12") or ""
		local ref = fn:match("run%-lua %-r (%d+)")
		api.keymap_del("prefix", "F12")
		api.keymap_del("prefix", "F11")
		local _, err = api.cmd("list-keys -T prefix F12")
		local _, stale = api.cmd("run-lua -r " .. ref)
		if check(done, err ~= nil, "F12 still bound") and
		    check(done, stale ~= nil and stale:match("no such key handler") ~= nil,
		    tostring(stale)) then
			done()
		end
	end)
end)
