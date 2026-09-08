-- termo.api.format_add: Lua variables in #{} formats, evaluated lazily.
local api = termo.api

describe("format variables", function()
	it("expand through eval and in conditionals", function()
		api.format_add("lua_spec", function(name)
			return "value of " .. name
		end)
		eq(api.eval("#{lua_spec}"), "value of lua_spec")
		eq(api.eval("#{?lua_spec,set,unset}"), "set")
		eq(api.eval("#{lua_spec}", "%0"), "value of lua_spec")
	end)
	it("are only called when referenced", function()
		local calls = 0
		api.format_add("lua_counted", function()
			calls = calls + 1
			return calls
		end)
		api.eval("#{pane_width}")
		api.eval("#{session_name} #{window_name}")
		eq(calls, 0)
		eq(api.eval("#{lua_counted}"), "1")
		eq(calls, 1)
	end)
	it("see the target being expanded through eval", function()
		api.format_add("lua_width", function()
			return api.eval("#{pane_width}") .. "x" .. api.eval("#{pane_height}")
		end)
		eq(api.eval("#{lua_width}", "%0"), "80x24")
	end)
	it("render in options like status-right", function()
		api.set_option("status-right", "[#{lua_spec}]")
		eq(api.eval("#{T:status-right}"), "[value of lua_spec]")
		api.set_option("status-right", "")
	end)
	it("turn errors and nil into empty strings", function()
		api.format_add("lua_bad", function()
			error("broken variable")
		end)
		api.format_add("lua_nil", function()
			return nil
		end)
		eq(api.eval("<#{lua_bad}><#{lua_nil}>"), "<><>")
	end)
	it("cannot recurse into themselves forever", function()
		api.format_add("lua_loop", function()
			return "(" .. api.eval("#{lua_loop}") .. ")"
		end)
		eq(api.eval("#{lua_loop}"), "(((())))")
	end)
	it("can be removed and validate names", function()
		api.format_add("lua_spec", nil)
		eq(api.eval("[#{lua_spec}]"), "[]")
		eq(fails(api.format_add, "bad-name", print), "bad variable name: bad-name")
		eq(fails(api.format_add, "", print), "empty variable name")
		eq(fails(api.format_add, "x", "nope"):match("function expected") ~= nil,
		    true)
		for _, name in ipairs({ "lua_counted", "lua_width", "lua_bad", "lua_nil",
		    "lua_loop" }) do
			api.format_add(name, nil)
		end
	end)
end)
