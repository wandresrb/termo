-- Spec runner: dofile()s each *_spec.lua and prints TAP through print(),
-- which run-lua sends back to the client. Fails the command if any spec fails.
local dir = debug.getinfo(1, "S").source:match("^@(.*)/[^/]*$") or "."

local n, bad = 0, 0
local current

function describe(name, fn)
	current = name
	fn()
	current = nil
end

function it(name, fn)
	n = n + 1
	local label = (current and (current .. " ") or "") .. name
	local ok, err = pcall(fn)
	if ok then
		print("ok " .. n .. " - " .. label)
	else
		bad = bad + 1
		print("not ok " .. n .. " - " .. label .. " # " .. tostring(err))
	end
end

function eq(got, want)
	if got ~= want then
		error(string.format("got %s, want %s", tostring(got), tostring(want)), 2)
	end
end

function fails(fn, ...)
	local ok, err = pcall(fn, ...)
	if ok then
		error("expected an error", 2)
	end
	return tostring(err)
end

for _, spec in ipairs({ "api_spec" }) do
	dofile(dir .. "/" .. spec .. ".lua")
end

print("1.." .. n)
if bad > 0 then
	error(bad .. " spec(s) failed")
end
