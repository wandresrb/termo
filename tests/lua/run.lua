-- Spec runner: dofile()s each *_spec.lua, collects TAP lines numbered in
-- the order specs finish, and spec_report() returns them all. Synchronous
-- specs finish inside this run-lua command; it_async specs run one at a
-- time from timers after it returns, and run.sh waits for the last one
-- through wait-for before asking for the report.
local dir = debug.getinfo(1, "S").source:match("^@(.*)/[^/]*$") or "."
local api = termo.api

local n, bad = 0, 0
local lines = {}
local current
local pending = 0
local covered = {}

-- Every api function is wrapped so the run knows which ones a spec called.
for _, f in ipairs(api.list()) do
	local orig = api[f.name]
	api[f.name] = function(...)
		covered[f.name] = true
		return orig(...)
	end
end
covered.list = true

-- Stray output must not break the TAP stream.
local raw_print = print
print = function(...)
	local parts = {}
	for i = 1, select("#", ...) do
		parts[i] = tostring((select(i, ...)))
	end
	raw_print("# " .. table.concat(parts, "\t"))
end

function describe(name, fn)
	current = name
	fn()
	current = nil
end

local function label(name)
	return (current and (current .. " ") or "") .. name
end

local function record(text, err)
	n = n + 1
	if err == nil then
		lines[#lines + 1] = "ok " .. n .. " - " .. text
	else
		bad = bad + 1
		err = tostring(err):gsub("\n", " "):gsub("^.*/tests/lua/", "")
		lines[#lines + 1] = "not ok " .. n .. " - " .. text .. " # " .. err
	end
end

function it(name, fn)
	local ok, err = pcall(fn)
	record(label(name), not ok and err or nil)
end

-- fn(done) runs later from a timer, after every earlier async spec has
-- called done() or done(err); one spec at a time keeps the command queue
-- free for the sync cmd() calls the specs make.
local queue = {}

local function run_next()
	local spec = table.remove(queue, 1)
	if spec == nil then
		api.cmd("wait-for -S lua-specs")
		return
	end
	api.defer(1, function()
		local ok, err = pcall(spec.fn, spec.done)
		if not ok then
			spec.done(err)
		end
	end)
end

function it_async(name, fn)
	local text = label(name)
	pending = pending + 1
	local finished = false
	local function done(err)
		if finished then
			return
		end
		finished = true
		pending = pending - 1
		record(text, err)
		run_next()
	end
	queue[#queue + 1] = { fn = fn, done = done }
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

-- Async specs use these instead of eq/error so a failure reaches done().
function check(done, cond, msg)
	if not cond then
		done(msg or "check failed")
		return false
	end
	return true
end

function spec_report()
	if pending > 0 then
		record(pending .. " async spec(s) never finished", "timed out")
	end
	local missing = {}
	for _, f in ipairs(api.list()) do
		if not covered[f.name] then
			missing[#missing + 1] = f.name
		end
	end
	if #missing == 0 then
		record("every termo.api function has a spec")
	else
		record("every termo.api function has a spec",
		    "no spec for: " .. table.concat(missing, ", "))
	end
	local out = {}
	for _, line in ipairs(lines) do
		out[#out + 1] = line
	end
	out[#out + 1] = "1.." .. n
	if bad > 0 then
		out[#out + 1] = "# " .. bad .. " spec(s) failed"
	end
	return table.concat(out, "\n")
end

for _, spec in ipairs({
	"api_spec",
	"cmd_spec",
	"events_spec",
	"keymap_spec",
	"timer_spec",
	"format_spec",
	"ui_spec",
	"runtime_spec",
}) do
	dofile(dir .. "/" .. spec .. ".lua")
end

-- A watchdog so a spec that never calls done() cannot hang the run.
api.defer(15000, function()
	api.cmd("wait-for -S lua-specs")
end)
run_next()
