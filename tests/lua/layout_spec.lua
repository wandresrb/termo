local api = termo.api
local layout = termo.layout

local function geometry(s)
	s = s:gsub("^%x+,", "")
	return (s:gsub("(%d+x%d+,%d+,%d+),%d+", "%1"))
end

describe("termo.layout", function()
	local root = os.tmpname()
	os.remove(root)
	layout.dir = root .. "/layouts"

	it_async("splits a window from a spec", function(done)
		local win = api.cmd("new-window -d -P -F '#{window_id}'")
		local pane = api.eval("#{pane_id}", win)
		layout.apply({ dir = "h",
			{},
			{ dir = "v", { cmd = "sleep 30" }, {} },
		}, {
			target = pane,
			done = function(err)
				if not check(done, err == nil, tostring(err)) then
					return
				end
				local panes = api.list_panes(win)
				api.cmd("kill-window -t " .. win)
				if check(done, #panes == 3, "panes " .. #panes) then
					done()
				end
			end,
		})
	end)

	it_async("dump and apply round-trip the window layout", function(done)
		local win = api.cmd("new-window -d -P -F '#{window_id}'")
		api.cmd("split-window -d -h -t " .. win)
		api.cmd("split-window -d -v -t " .. win)
		local spec = layout.dump(win)
		local want = geometry(api.eval("#{window_layout}", win))
		if not check(done, #spec.panes == 3, "panes " .. #spec.panes) or
		    not check(done, spec.layout ~= nil and spec.layout:find("{") ~= nil,
		    "layout " .. tostring(spec.layout)) then
			return
		end
		local other = api.cmd("new-window -d -P -F '#{window_id}'")
		layout.apply(spec, {
			target = other,
			done = function(err)
				local got = geometry(api.eval("#{window_layout}", other))
				api.cmd("kill-window -t " .. win)
				api.cmd("kill-window -t " .. other)
				if check(done, err == nil, tostring(err)) and
				    check(done, got == want, got .. " vs " .. want) then
					done()
				end
			end,
		})
	end)

	it_async("save, load and swap cycle the layouts that fit", function(done)
		local win = api.cmd("new-window -d -P -F '#{window_id}'")
		api.cmd("split-window -d -h -t " .. win)
		local wide = geometry(api.eval("#{window_layout}", win))
		layout.save("two-h", win, {
			done = function(err)
				if not check(done, err == nil, tostring(err)) then
					return
				end
				api.cmd("select-layout -t " .. win .. " even-vertical")
				local tall = geometry(api.eval("#{window_layout}", win))
				layout.save("two-v", win, {
					done = function()
						local extra = api.cmd("split-window -d -t " .. win .. " -P -F '#{pane_id}'")
						layout.save("three", win, {
							done = function()
								api.cmd("kill-pane -t " .. extra)
								local loaded = layout.load("two-h")
								if not check(done, loaded and #loaded.panes == 2, "load") then
									return
								end
								layout.swap({
									target = win,
									done = function(e, name)
										local first = geometry(api.eval("#{window_layout}", win))
										layout.swap({
											target = win,
											done = function(e2, name2)
												local second = geometry(api.eval("#{window_layout}", win))
												api.cmd("kill-window -t " .. win)
												api.system({ "rm", "-rf", root })
												if check(done, e == nil and e2 == nil, tostring(e or e2)) and
												    check(done, name == "two-h" and name2 == "two-v",
												    tostring(name) .. "," .. tostring(name2)) and
												    check(done, first == wide, "first " .. first) and
												    check(done, second == tall, "second " .. second) then
													done()
												end
											end,
										})
									end,
								})
							end,
						})
					end,
				})
			end,
		})
	end)

	it("load reports a missing layout", function()
		local r, err = layout.load("nope")
		eq(r, nil)
		eq(err:match("not found") ~= nil, true)
	end)
end)
