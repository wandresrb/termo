local api = termo.api
local pack = termo.pack

describe("termo.pack", function()
	local root = os.tmpname()
	os.remove(root)
	pack.dir = root .. "/pack"
	pack.lockfile = root .. "/config/termo-pack-lock.json"
	local repo = function(name)
		return root .. "/src/" .. name .. ".git"
	end
	local kinds = {}
	api.on("@pack-changed", function(ev)
		kinds[#kinds + 1] = ev.kind .. ":" .. ev.name
	end)

	local function read(path)
		local f = io.open(path, "r")
		if f == nil then
			return nil
		end
		local s = f:read("*a")
		f:close()
		return s
	end

	local function sha(name, tag)
		return (read(root .. "/" .. name .. "." .. tag) or ""):gsub("%s+$", "")
	end

	local function exists(path)
		return read(path) ~= nil
	end

	local function fresh()
		pack.plugins = {}
		pack.loaded = {}
		api.set_option("@pack_order", "")
	end

	it_async("installs at the best tag in range and writes the lock", function(done)
		api.system({ "sh", "-c", [[
set -e
root=$1
mkdir -p "$root/src" "$root/config"
main='local plugin = ...
termo.api.set_option("@pack_order", (termo.api.get_option("@pack_order") or "") .. plugin.name .. ",")
return { name = plugin.name }'
mk() {
	w="$root/work/$1"
	mkdir -p "$w/lua"
	printf '{"name":"%s","main":"lua/%s.lua"%s}' "$1" "$1" "$2" > "$w/termo.json"
	printf '%s' "${3:-$main}" > "$w/lua/$1.lua"
	git -C "$w" init -q
	git -C "$w" add -A
	git -C "$w" -c user.name=t -c user.email=t@t commit -q -m "first"
	git -C "$w" tag v1.2.0
	printf '\n' >> "$w/lua/$1.lua"
	git -C "$w" add -A
	git -C "$w" -c user.name=t -c user.email=t@t commit -q -m "second"
	git -C "$w" tag v2.0.0
	git clone -q --bare "$w" "$root/src/$1.git"
	git -C "$w" rev-parse 'v1.2.0^{commit}' > "$root/$1.v1"
	git -C "$w" rev-parse 'v2.0.0^{commit}' > "$root/$1.v2"
}
mk a ''
mk b ",\"dependencies\":[\"$root/src/a.git\"]"
mk c ",\"dependencies\":[\"$root/src/d.git\"]"
mk d ",\"dependencies\":[\"$root/src/c.git\"]"
main_e='local plugin = ...
termo.api.set_option("@pack_order", (termo.api.get_option("@pack_order") or "") .. plugin.name .. ",")
termo.api.format_add("lazyfmt", function() return "yes" end)
return { name = plugin.name }'
mk e ',"keys":[{"table":"root","key":"F11","command":"set -g @lazy_manifest hit"}]' "$main_e"
main_f='local plugin = ...
termo.command.define("specfcmd", function(args) termo.api.set_option("@specfcmd", table.concat(args, ",")) end)
return { name = plugin.name }'
mk f '' "$main_f"
]], "sh", root }, {
			on_exit = function(status)
				if not check(done, status == 0, "repo setup failed " .. status) then
					return
				end
				fresh()
				pack.setup({ { src = repo("a"), version = { range = "1.x" } } }, {
					confirm = false,
					done = function(errs)
						local got = pack.get({ "a" })[1]
						if check(done, #errs == 0, table.concat(errs, "; ")) and
						    check(done, got ~= nil and got.rev == sha("a", "v1"),
						    "rev " .. tostring(got and got.rev)) and
						    check(done, got.active and type(got.load_ms) == "number",
						    "not active") and
						    check(done, api.get_option("@pack_order") == "a,",
						    tostring(api.get_option("@pack_order"))) and
						    check(done, pack.list()[1] == "a", "not listed") and
						    check(done, (read(pack.lockfile) or ""):find(sha("a", "v1"), 1, true) ~= nil,
						    "lock: " .. tostring(read(pack.lockfile))) and
						    check(done, kinds[#kinds] == "install:a", tostring(kinds[#kinds])) then
							done()
						end
					end,
				})
			end,
		})
	end)

	it_async("installs the locked revision on a fresh data dir", function(done)
		fresh()
		pack.dir = root .. "/pack2"
		pack.setup({ { src = repo("a"), version = { range = "2.x" } } }, {
			confirm = false,
			done = function(errs)
				local got = pack.get({ "a" })[1]
				if check(done, #errs == 0, table.concat(errs, "; ")) and
				    check(done, got.rev == sha("a", "v1"), "rev " .. tostring(got.rev)) then
					done()
				end
			end,
		})
	end)

	it_async("update lists the pending commits and applies them", function(done)
		pack.update({ "a" }, {
			force = true,
			done = function(report, errs)
				local e = report[1]
				if check(done, #errs == 0, table.concat(errs, "; ")) and
				    check(done, e ~= nil and e.applied, "nothing applied") and
				    check(done, e.new == sha("a", "v2"), "new " .. tostring(e and e.new)) and
				    check(done, (e.log[1] or ""):find("second") ~= nil,
				    "log " .. table.concat(e.log, "|")) and
				    check(done, pack.get({ "a" })[1].rev == sha("a", "v2"), "rev not moved") and
				    check(done, (read(pack.lockfile) or ""):find(sha("a", "v2"), 1, true) ~= nil,
				    "lock not updated") and
				    check(done, kinds[#kinds] == "update:a", tostring(kinds[#kinds])) then
					done()
				end
			end,
		})
	end)

	it_async("loads a dependency before its dependant", function(done)
		fresh()
		pack.setup({ repo("b") }, {
			confirm = false,
			done = function(errs)
				if check(done, #errs == 0, table.concat(errs, "; ")) and
				    check(done, api.get_option("@pack_order") == "a,b,",
				    tostring(api.get_option("@pack_order"))) and
				    check(done, pack.get({ "b" })[1].active, "b not active") then
					done()
				end
			end,
		})
	end)

	it_async("reports a dependency cycle", function(done)
		fresh()
		pack.setup({ repo("c") }, {
			confirm = false,
			done = function(errs)
				if check(done, table.concat(errs, "; "):find("dependency cycle") ~= nil,
				    table.concat(errs, "; ")) then
					done()
				end
			end,
		})
	end)

	it_async("del removes a plugin and clean removes the orphans", function(done)
		pack.del({ "c" }, {
			done = function()
				if not check(done, not exists(pack.dir .. "/c/termo.json"), "c still there") or
				    not check(done, kinds[#kinds] == "delete:c", tostring(kinds[#kinds])) then
					return
				end
				fresh()
				pack.setup({ repo("a") }, {
					confirm = false,
					done = function()
						pack.clean({
							done = function(errs)
								if check(done, #errs == 0, table.concat(errs, "; ")) and
								    check(done, not exists(pack.dir .. "/b/termo.json"), "b kept") and
								    check(done, not exists(pack.dir .. "/d/termo.json"), "d kept") and
								    check(done, exists(pack.dir .. "/a/termo.json"), "a removed") then
									done()
								end
							end,
						})
					end,
				})
			end,
		})
	end)

	it_async("asks before installing and waits for a client", function(done)
		fresh()
		pack.setup({ repo("d") })
		api.defer(200, function()
			if check(done, not exists(pack.dir .. "/d/termo.json"), "installed without asking") then
				done()
			end
		end)
	end)

	local function press(tname, key)
		local out = api.cmd("list-keys -T " .. tname .. " " .. key) or ""
		local ref = out:match("run%-lua %-r (%d+)")
		if ref == nil then
			return nil, "no stub bound: " .. out
		end
		api.cmd("run-lua -r " .. ref)
		return true
	end

	it_async("keys in the spec bind a stub that loads and runs the action", function(done)
		fresh()
		pack.setup({ { src = repo("a"), keys = { { "root", "F12", "set -g @lazy_key hit" } } } }, {
			confirm = false,
			done = function(errs)
				local got = pack.get({ "a" })[1]
				if not check(done, #errs == 0, table.concat(errs, "; ")) or
				    not check(done, got.lazy and not got.active, "loaded eagerly") or
				    not check(done, api.get_option("@pack_order") == "", "main ran") then
					return
				end
				local ok, err = press("root", "F12")
				if check(done, ok, err) and
				    check(done, api.get_option("@pack_order") == "a,",
				    tostring(api.get_option("@pack_order"))) and
				    check(done, api.get_option("@lazy_key") == "hit", "action did not run") and
				    check(done, pack.get({ "a" })[1].active, "not active") then
					done()
				end
			end,
		})
	end)

	it_async("an event trigger loads once", function(done)
		fresh()
		pack.setup({ { src = repo("a"), event = "@lazy-go" } }, {
			confirm = false,
			done = function()
				if not check(done, not pack.get({ "a" })[1].active, "loaded eagerly") then
					return
				end
				api.emit("@lazy-go")
				api.emit("@lazy-go")
				if check(done, api.get_option("@pack_order") == "a,",
				    tostring(api.get_option("@pack_order"))) then
					done()
				end
			end,
		})
	end)

	it_async("cond = false keeps the plugin inactive", function(done)
		fresh()
		pack.setup({ { src = repo("a"), cond = false, event = "@lazy-no" } }, {
			confirm = false,
			done = function()
				api.emit("@lazy-no")
				if check(done, not pack.get({ "a" })[1].active, "loaded") and
				    check(done, api.get_option("@pack_order") == "", "main ran") then
					done()
				end
			end,
		})
	end)

	it_async("a format trigger loads on the first evaluation", function(done)
		fresh()
		pack.setup({ { src = repo("e"), format = "lazyfmt" } }, {
			confirm = false,
			done = function(errs)
				if not check(done, #errs == 0, table.concat(errs, "; ")) or
				    not check(done, not pack.get({ "e" })[1].active, "loaded eagerly") then
					return
				end
				local v = api.eval("#{lazyfmt}")
				if check(done, v == "yes", "format gave " .. tostring(v)) and
				    check(done, pack.get({ "e" })[1].active, "not active") then
					done()
				end
			end,
		})
	end)

	it_async("manifest keys make a plugin lazy unless the spec says lazy = false", function(done)
		fresh()
		pack.setup({ repo("e") }, {
			confirm = false,
			done = function()
				if not check(done, not pack.get({ "e" })[1].active, "loaded eagerly") then
					return
				end
				local ok, err = press("root", "F11")
				if not check(done, ok, err) or
				    not check(done, api.get_option("@lazy_manifest") == "hit", "action did not run") or
				    not check(done, pack.get({ "e" })[1].active,
				    "not active: " .. table.concat(pack.errors, "; ")) then
					return
				end
				fresh()
				pack.setup({ { src = repo("e"), lazy = false } }, {
					confirm = false,
					done = function()
						if check(done, pack.get({ "e" })[1].active, "not loaded eagerly") then
							done()
						end
					end,
				})
			end,
		})
	end)

	it_async("a cmd trigger defines a stub that loads and re-runs the command", function(done)
		fresh()
		pack.setup({ { src = repo("f"), cmd = "specfcmd" } }, {
			confirm = false,
			done = function(errs)
				if not check(done, #errs == 0, table.concat(errs, "; ")) or
				    not check(done, not pack.get({ "f" })[1].active, "loaded eagerly") then
					return
				end
				local _, err = api.cmd("specfcmd 'a b'")
				if check(done, err == nil, tostring(err)) and
				    check(done, pack.get({ "f" })[1].active, "not active") and
				    check(done, api.get_option("@specfcmd") == "a b",
				    tostring(api.get_option("@specfcmd"))) then
					termo.command.del("specfcmd")
					done()
				end
			end,
		})
	end)

	it_async("a mode trigger loads when the mode is entered", function(done)
		fresh()
		pack.setup({ { src = repo("a"), mode = "specmode" } }, {
			confirm = false,
			done = function()
				if not check(done, not pack.get({ "a" })[1].active, "loaded eagerly") then
					return
				end
				api.emit("client-key-table-changed", { key_table = "root" })
				if not check(done, not pack.get({ "a" })[1].active, "loaded on root") then
					return
				end
				api.emit("client-key-table-changed", { key_table = "mode-specmode" })
				if check(done, pack.get({ "a" })[1].active, "not active") then
					done()
				end
			end,
		})
	end)

	it_async("lazy = true loads only by hand", function(done)
		fresh()
		pack.setup({ { src = repo("a"), lazy = true } }, {
			confirm = false,
			done = function()
				if not check(done, not pack.get({ "a" })[1].active, "loaded eagerly") then
					return
				end
				local mod = pack.load("a")
				if check(done, mod ~= nil and mod.name == "a", "load returned nothing") and
				    check(done, pack.get({ "a" })[1].active, "not active") then
					api.system({ "rm", "-rf", root })
					done()
				end
			end,
		})
	end)

	it("rejects a missing manifest", function()
		local r, err = pack.load("/nonexistent/plugin")
		eq(r, nil)
		eq(err:match("not found") ~= nil, true)
	end)
end)
