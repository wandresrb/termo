-- termo.api.menu/popup/message/prompt need an attached client, which the
-- spec server does not have: this checks the arguments and the error. The
-- integration test drives them through a real terminal.
local api = termo.api

describe("ui without a client", function()
	it("menu validates its spec, then wants a client", function()
		eq(fails(api.menu, "x"):match("table expected") ~= nil, true)
		eq(fails(api.menu, { items = { { "a", "a", "display x" } } }),
		    "no client")
		eq(fails(api.menu, { items = { { "a", "a", "x" } }, client = "nope" }),
		    "no such client: nope")
	end)
	it("popup wants a client", function()
		eq(fails(api.popup, { cmd = "true" }), "no client")
	end)
	it("message wants a client", function()
		eq(fails(api.message, "hello"), "no client")
		eq(fails(api.message):match("string expected") ~= nil, true)
	end)
	it("prompt wants a function and a client", function()
		eq(fails(api.prompt, "label", "x"):match("function expected") ~= nil,
		    true)
		eq(fails(api.prompt, "label", function() end), "no client")
	end)
end)
