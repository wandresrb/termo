-- termo.api.defer/timer/system: everything here waits on the event loop.
local api = termo.api

describe("defer", function()
	it_async("calls once after the delay", function(done)
		local n = 0
		local t0 = os.clock()
		api.defer(20, function()
			n = n + 1
		end)
		api.defer(60, function()
			if check(done, n == 1, "called " .. n) then
				done()
			end
		end)
	end)
	it_async("can be stopped", function(done)
		local fired = false
		local t = api.defer(10, function()
			fired = true
		end)
		t:stop()
		t:stop()
		api.defer(40, function()
			if check(done, not fired, "stopped timer fired") then
				done()
			end
		end)
	end)
	it("rejects bad arguments", function()
		eq(fails(api.defer, -1, print):match("negative delay") ~= nil, true)
		eq(fails(api.defer, 1, "x"):match("function expected") ~= nil, true)
	end)
end)

describe("timer", function()
	it_async("repeats until stopped", function(done)
		local n = 0
		local t
		t = api.timer(5, function()
			n = n + 1
			if n == 3 then
				t:stop()
			end
		end)
		api.defer(80, function()
			if check(done, n == 3, "ran " .. n .. " times") then
				done()
			end
		end)
	end)
	it_async("survives an error in its callback", function(done)
		local n = 0
		local t
		t = api.timer(5, function()
			n = n + 1
			if n == 2 then
				t:stop()
			end
			error("boom")
		end)
		api.defer(60, function()
			if check(done, n == 2, "ran " .. n .. " times") then
				done()
			end
		end)
	end)
end)

describe("system", function()
	it_async("delivers stdout lines and the exit status", function(done)
		local lines = {}
		api.system({ "printf", "one\\ntwo\\nlast" }, {
			on_stdout = function(line)
				lines[#lines + 1] = line
			end,
			on_exit = function(status)
				if check(done, status == 0, "status " .. status) and
				    check(done, #lines == 3, "lines " .. #lines) and
				    check(done, lines[3] == "last", lines[3] or "nil") then
					done()
				end
			end,
		})
	end)
	it_async("runs a string through the shell", function(done)
		api.system("exit 3", {
			on_exit = function(status)
				if check(done, status == 3, "status " .. status) then
					done()
				end
			end,
		})
	end)
	it_async("honours cwd", function(done)
		api.system({ "pwd" }, {
			cwd = "/",
			on_stdout = function(line)
				if check(done, line == "/", line) then
					done()
				end
			end,
		})
	end)
	it("rejects bad argv", function()
		eq(fails(api.system, {}):match("empty argv") ~= nil, true)
		eq(fails(api.system, { 1 }):match("must be strings") ~= nil, true)
	end)
end)
