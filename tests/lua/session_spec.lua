local api = termo.api
local session = termo.session

describe("termo.api.write_file", function()
	local root = os.tmpname()
	os.remove(root)

	it("creates the directories, writes the data and leaves no temporary", function()
		local path = root .. "/a/b/file.json"
		eq(api.write_file(path, "one"), true)
		eq(api.write_file(path, "two"), true)
		local f = io.open(path)
		eq(f:read("*a"), "two")
		f:close()
		eq(io.open(path .. ".tmp"), nil)
	end)

	it("reports an error instead of raising", function()
		local ok, err = api.write_file("/nonexistent-root-dir/x/y", "z")
		eq(ok, nil)
		eq(err:find("nonexistent") ~= nil, true)
		os.remove(root .. "/a/b/file.json")
		os.remove(root .. "/a/b")
		os.remove(root .. "/a")
		os.remove(root)
	end)
end)

describe("termo.session", function()
	local root = os.tmpname()
	os.remove(root)
	local saved_dir = session.dir

	local function read(path)
		local f = io.open(path)
		if f == nil then
			return nil
		end
		local s = f:read("*a")
		f:close()
		return s
	end

	it_async("saves a session, skips an unchanged one and lists it", function(done)
		session.dir = root
		local sid = api.eval("#{session_id}")
		local name = api.eval("#{session_name}")
		api.set_option("resurrect", "layout", sid)
		session.save_all(function(first)
			session.save_all(function(second)
				local snap = termo.json.decode(read(root .. "/" .. name .. ".json") or "{}")
				local listed = session.list()
				if check(done, #first >= 1 and first[1] == name, "first " .. #first) and
				    check(done, #second == 0, "rewrote an unchanged session") and
				    check(done, snap.version == 1 and #snap.windows >= 1, "snapshot") and
				    check(done, listed[1] and listed[1].name == name and listed[1].alive,
				    "list") then
					done()
				end
			end)
		end)
	end)

	it_async("a rename moves the file and del removes it", function(done)
		local sid = api.eval("#{session_id}")
		local name = api.eval("#{session_name}")
		api.cmd("rename-session -t " .. sid .. " specrenamed")
		session.save_all(function()
			local old = read(root .. "/" .. name .. ".json")
			local new = read(root .. "/specrenamed.json")
			api.cmd("rename-session -t " .. sid .. " " .. name)
			session.save_all(function()
				local back = read(root .. "/" .. name .. ".json")
				local gone = read(root .. "/specrenamed.json") == nil
				local deleted = session.del(name)
				local again = session.del(name)
				api.set_option("resurrect", "off", sid)
				if check(done, old == nil, "old name kept") and
				    check(done, new ~= nil, "new name missing") and
				    check(done, back ~= nil and gone, "rename back") and
				    check(done, deleted == true and again == false, "del") and
				    check(done, read(root .. "/" .. name .. ".json") == nil, "file left") then
					api.set_option("resurrect", "layout", sid)
					done()
				end
			end)
		end)
	end)

	it_async("revive refuses a live session and a missing file; clean keeps live ones", function(done)
		local name = api.eval("#{session_name}")
		local sid = api.eval("#{session_id}")
		session.save_all(function()
			local r1, e1 = session.revive(name)
			local r2, e2 = session.revive("nope")
			local removed = session.clean()
			local kept = read(root .. "/" .. name .. ".json")
			session.del(name)
			api.set_option("resurrect", "off", sid)
			session.dir = saved_dir
			api.system({ "rm", "-rf", root })
			if check(done, r1 == nil and e1:find("exists") ~= nil, tostring(e1)) and
			    check(done, r2 == nil and e2:find("not found") ~= nil, tostring(e2)) and
			    check(done, #removed == 0, "clean removed " .. #removed) and
			    check(done, kept ~= nil, "clean removed a live session") then
				done()
			end
		end)
	end)
end)
