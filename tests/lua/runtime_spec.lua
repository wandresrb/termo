-- The Lua side: runtime/lua/termo/*.lua over termo.api.
local api = termo.api

describe("termo.json", function()
	local json = termo.json
	it("encodes and decodes round trips", function()
		local v = { a = { 1, 2.5, "x", true }, b = { c = "q\"\n\t" }, n = -3 }
		local d = json.decode(json.encode(v))
		eq(#d.a, 4)
		eq(d.a[2], 2.5)
		eq(d.a[4], true)
		eq(d.b.c, "q\"\n\t")
		eq(d.n, -3)
	end)
	it("has stable output", function()
		eq(json.encode({ b = 1, a = "x" }), '{"a":"x","b":1}')
		eq(json.encode({ 1, "two", false }), '[1,"two",false]')
		eq(json.encode({}), "{}")
		eq(json.encode(json.array()), "[]")
		eq(json.encode(nil), "null")
	end)
	it("decodes escapes, nulls and nesting", function()
		local d = json.decode('{"s":"\\u00e9\\n","n":null,"l":[[]],"e":{}}')
		eq(d.s, "é\n")
		eq(d.n, nil)
		eq(#d.l, 1)
		eq(type(d.e), "table")
	end)
	it("reports errors with a position", function()
		eq(fails(json.decode, "{"):match("expected a key at 2") ~= nil, true)
		eq(fails(json.decode, "[1,]"):match("unexpected character") ~= nil, true)
		eq(fails(json.decode, "1 2"):match("trailing data") ~= nil, true)
		local loop = {}
		loop.self = loop
		eq(fails(json.encode, loop):match("recursive") ~= nil, true)
	end)
end)

describe("termo.opt", function()
	it("reads and writes with underscores for dashes", function()
		termo.opt.history_limit = 777
		eq(termo.opt.history_limit, 777)
		eq(api.get_option("history-limit"), 777)
		eq(termo.opt["renumber-windows"], false)
	end)
	it("targets a window or session", function()
		termo.opt.of("@0").automatic_rename = false
		eq(termo.opt.of("@0").automatic_rename, false)
		eq(api.get_option("automatic-rename", "@0"), false)
		termo.opt.of("@0").automatic_rename = true
	end)
end)

describe("termo.keymap", function()
	it("binds with and without a table name", function()
		termo.keymap.set("F10", "display-message ten")
		termo.keymap.set("copy-mode-vi", "F10", function() end)
		termo.keymap.root("F9", "display-message nine", { note = "n" })
	end)
	it_async("and they show up", function(done)
		local a = api.cmd("list-keys -T prefix F10") or ""
		local b = api.cmd("list-keys -T copy-mode-vi F10") or ""
		local c = api.cmd("list-keys -T root F9") or ""
		termo.keymap.del("F10")
		termo.keymap.del("copy-mode-vi", "F10")
		termo.keymap.del("root", "F9")
		if check(done, a:match("ten") ~= nil, a) and
		    check(done, b:match("run%-lua") ~= nil, b) and
		    check(done, c:match("nine") ~= nil, c) then
			done()
		end
	end)
end)

describe("termo.hints", function()
	it("creates a key table with sticky bindings", function()
		termo.hints.setup()
		termo.hints.mode("spec", {
			key = "F8",
			keys = {
				h = { "select-pane -L", "left" },
				x = { "kill-pane", "kill", once = true },
			},
		})
		local line = termo.hints.line("mode-spec")
		eq(line:match("spec") ~= nil, true)
		eq(line:match(" h ") ~= nil, true)
		eq(line:match("left") ~= nil, true)
		eq(termo.hints.line("root"), "")
	end)
	it_async("and the bindings return to the table", function(done)
		local h = api.cmd("list-keys -T mode-spec h") or ""
		local x = api.cmd("list-keys -T mode-spec x") or ""
		local esc = api.cmd("list-keys -T mode-spec Escape") or ""
		local enter = api.cmd("list-keys -T prefix F8") or ""
		api.cmd("unbind -T mode-spec -a")
		api.keymap_del("prefix", "F8")
		if check(done, h:match("switch%-client %-T mode%-spec") ~= nil, h) and
		    check(done, x:match("switch%-client") == nil, x) and
		    check(done, esc:match("%-T root") ~= nil, esc) and
		    check(done, enter:match("mode%-spec") ~= nil, enter) then
			done()
		end
	end)
end)

describe("termo.palette", function()
	it("ranks entries with fuzzy matching", function()
		termo.palette.entries = {}
		termo.palette.add("Split right", "split-window -h")
		termo.palette.add("Split down", "split-window -v")
		termo.palette.add("New window", "new-window")
		local r = termo.palette.rank("spr")
		eq(#r, 1)
		eq(r[1].entry.name, "Split right")
		eq(#termo.palette.rank(""), 3)
		eq(#termo.palette.rank("zzz"), 0)
		termo.palette.matches = termo.palette.rank("split")
		termo.palette.selected = 2
		eq(termo.palette.line():match("#%[reverse%] Split down") ~= nil, true)
	end)
	it("adds every command on setup", function()
		termo.palette.setup({ commands = true })
	end)
	it_async("once list-commands has run", function(done)
		local found = false
		for _, e in ipairs(termo.palette.entries) do
			if e.name == "new-window" then
				found = true
			end
		end
		if check(done, found, "new-window not in the palette") then
			done()
		end
	end)
end)

describe("termo.float", function()
	it_async("floats and tiles a pane", function(done)
		local win = api.cmd("new-window -d -P -F '#{window_id}'")
		local pane = api.eval("#{pane_id}", win)
		local _, err = api.cmd("split-window -d -t " .. pane)
		if not check(done, err == nil, tostring(err)) then
			return
		end
		termo.float.toggle(pane)
		local floating = termo.float.is_floating(pane)
		termo.float.move("centre", pane)
		termo.float.toggle(pane)
		local tiled = not termo.float.is_floating(pane)
		local _, err2 = termo.float.new({ cmd = "sleep 30", w = "50%", h = 5 })
		api.cmd("kill-window -t " .. win)
		if check(done, floating, "did not float") and
		    check(done, tiled, "did not tile") and
		    check(done, err2 == nil, tostring(err2)) then
			done()
		end
	end)
end)
