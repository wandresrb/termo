-- A command palette: a prompt that ranks entries with termo.api.fuzzy as
-- you type, shows the best ones in the second status line, and runs the
-- selected one on Enter. Entries are yours (add) plus every command.
--
--   termo.palette.add("Split right", "split-window -h")
--   termo.palette.add("Say hi", function() termo.ui.message("hi") end)
--   termo.palette.setup{ key = "p" }
local api = termo.api
local M = { entries = {}, matches = {}, selected = 1, shown = 6 }

function M.add(name, action, opts)
	M.entries[#M.entries + 1] = {
		name = name,
		action = action,
		desc = opts and opts.desc,
	}
end

-- Rank entries for a query, best first; empty query keeps insertion order.
function M.rank(query)
	local out = {}
	for i, e in ipairs(M.entries) do
		local score = api.fuzzy(query, e.name)
		if score ~= nil then
			out[#out + 1] = { entry = e, score = score, order = i }
		end
	end
	table.sort(out, function(a, b)
		if a.score ~= b.score then
			return a.score > b.score
		end
		return a.order < b.order
	end)
	return out
end

-- The status line text for the current matches.
function M.line()
	if #M.matches == 0 then
		return "#[dim]no match"
	end
	local parts = {}
	local first = math.max(1, M.selected - M.shown + 1)
	for i = first, math.min(#M.matches, first + M.shown - 1) do
		local name = M.matches[i].entry.name
		if i == M.selected then
			parts[#parts + 1] = "#[reverse] " .. name .. " #[noreverse]"
		else
			parts[#parts + 1] = " " .. name .. " "
		end
	end
	return table.concat(parts, " ")
end

local function run(match)
	if match == nil then
		return
	end
	local action = match.entry.action
	if type(action) == "function" then
		action()
	else
		api.cmd(action)
	end
end

local function close()
	api.cmd("set -g status " .. M.saved_status)
end

function M.open()
	M.matches = M.rank("")
	M.selected = 1
	M.saved_status = tostring(api.get_option("status"))
	if M.saved_status == "true" then
		M.saved_status = "on"
	elseif M.saved_status == "false" then
		M.saved_status = "off"
	end
	api.set_option("status", "2")
	api.prompt("> ", function(text, done)
		if text == nil then
			return close()
		end
		if done == "move" then
			if #M.matches > 0 then
				M.selected = M.selected % #M.matches + 1
			end
			return
		end
		if done then
			close()
			return run(M.matches[M.selected])
		end
		M.matches = M.rank(text)
		M.selected = 1
	end, { incremental = true })
end

-- Every command becomes an entry that opens a command prompt with its
-- name typed, so arguments can be added.
local function add_commands()
	api.cmd_async("list-commands", function(out)
		for line in (out or ""):gmatch("[^\n]+") do
			local name = line:match("^(%S+)")
			if name then
				M.add(name, "command-prompt -I '" .. name .. " '",
				    { desc = line })
			end
		end
	end)
end

-- setup{key = "p", commands = true}
function M.setup(opts)
	opts = opts or {}
	api.format_add("palette", M.line)
	api.cmd("set -g 'status-format[1]' '#[align=centre]#{palette}'")
	if opts.commands ~= false then
		add_commands()
	end
	if opts.key then
		api.keymap_set("prefix", opts.key, function()
			M.open()
		end, { note = "command palette" })
	end
end

return M
