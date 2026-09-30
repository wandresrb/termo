local api = termo.api
local command = termo.command

describe("termo.command", function()
	it("rejects names that clash or cannot parse", function()
		eq(fails(api.command_set, "new-window", function() end):match("command exists") ~= nil, true)
		eq(fails(api.command_set, "a b", function() end):match("bad command name") ~= nil, true)
		eq(fails(api.command_set, "9x", function() end):match("bad command name") ~= nil, true)
		eq(fails(api.command_set, "x"):match("function expected") ~= nil, true)
	end)

	it_async("runs a defined command with its arguments", function(done)
		command.define("specc2p", function(args, ev)
			api.set_option("@specc2p", table.concat(args, ",") .. "/" .. ev.table)
		end, { nargs = "+" })
		local _, err = api.cmd("specc2p 2 'two words'")
		if check(done, err == nil, tostring(err)) and
		    check(done, api.get_option("@specc2p") == "2,two words/command",
		    tostring(api.get_option("@specc2p"))) then
			done()
		end
	end)

	it_async("checks the number of arguments", function(done)
		command.define("specone", function() end, { nargs = 1 })
		local _, err = api.cmd("specone")
		if check(done, err ~= nil and err:find("expected 1") ~= nil, tostring(err)) then
			done()
		end
	end)

	it_async("redefines in place and deletes", function(done)
		command.define("specre", function()
			api.set_option("@specre", "one")
		end)
		command.define("specre", function()
			api.set_option("@specre", "two")
		end)
		api.cmd("specre")
		local aliases = api.cmd("show -g command-alias")
		local count = select(2, aliases:gsub("specre=", ""))
		eq(command.del("specre"), true)
		eq(api.command_del("specre"), false)
		local ok, err = pcall(api.cmd, "specre")
		if check(done, not ok, "specre still runs") and
		    check(done, api.get_option("@specre") == "two", tostring(api.get_option("@specre"))) and
		    check(done, count == 1, "aliases " .. count) and
		    check(done, err ~= nil and err:find("unknown command") ~= nil, tostring(err)) and
		    check(done, command.commands.specre == nil, "still listed") then
			done()
		end
	end)

	it("adds commands with a description to the palette", function()
		command.define("specpal", function() end, { desc = "Spec palette entry" })
		local found = false
		for _, e in ipairs(termo.palette.entries) do
			if e.name == "Spec palette entry" and e.action == "specpal" then
				found = true
			end
		end
		eq(found, true)
		command.del("specpal")
	end)
end)
