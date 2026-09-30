-- JSON encode and decode, enough for run-lua -j and termo.json manifests.
-- Tables with a sequence part encode as arrays, the rest as objects; an
-- empty table is an empty object unless it was decoded from an array.
local M = {}

local ARRAY = setmetatable({}, { __mode = "k" })

local escapes = {
	['"'] = '\\"', ["\\"] = "\\\\", ["\b"] = "\\b", ["\f"] = "\\f",
	["\n"] = "\\n", ["\r"] = "\\r", ["\t"] = "\\t",
}

local function encode_string(s)
	return '"' .. s:gsub('[%c"\\]', function(c)
		return escapes[c] or string.format("\\u%04x", c:byte())
	end) .. '"'
end

local function is_array(t)
	if ARRAY[t] then
		return true
	end
	local n = 0
	for k in pairs(t) do
		if type(k) ~= "number" or k < 1 or k % 1 ~= 0 then
			return false
		end
		n = n + 1
	end
	return n > 0 and n == #t
end

local encode

local function encode_table(t, seen)
	if seen[t] then
		error("cannot encode a recursive table")
	end
	seen[t] = true
	local out = {}
	if is_array(t) then
		for i = 1, #t do
			out[i] = encode(t[i], seen)
		end
		seen[t] = nil
		return "[" .. table.concat(out, ",") .. "]"
	end
	local keys = {}
	for k in pairs(t) do
		keys[#keys + 1] = tostring(k)
	end
	table.sort(keys)
	for i, k in ipairs(keys) do
		local v = t[k]
		if v == nil then
			v = t[tonumber(k)]
		end
		out[i] = encode_string(k) .. ":" .. encode(v, seen)
	end
	seen[t] = nil
	return "{" .. table.concat(out, ",") .. "}"
end

function encode(v, seen)
	local kind = type(v)
	if kind == "nil" then
		return "null"
	elseif kind == "boolean" then
		return v and "true" or "false"
	elseif kind == "number" then
		if v ~= v or v == math.huge or v == -math.huge then
			return "null"
		end
		return string.format("%.14g", v)
	elseif kind == "string" then
		return encode_string(v)
	elseif kind == "table" then
		return encode_table(v, seen or {})
	end
	return encode_string(tostring(v))
end

function M.encode(v)
	return encode(v)
end

-- Decoder: a small recursive descent parser reporting the byte offset of
-- the first error.
local unescapes = {
	['"'] = '"', ["\\"] = "\\", ["/"] = "/", b = "\b", f = "\f", n = "\n",
	r = "\r", t = "\t",
}

local function utf8_char(cp)
	if cp < 0x80 then
		return string.char(cp)
	elseif cp < 0x800 then
		return string.char(0xC0 + math.floor(cp / 0x40), 0x80 + cp % 0x40)
	elseif cp < 0x10000 then
		return string.char(0xE0 + math.floor(cp / 0x1000),
		    0x80 + math.floor(cp / 0x40) % 0x40, 0x80 + cp % 0x40)
	end
	return string.char(0xF0 + math.floor(cp / 0x40000),
	    0x80 + math.floor(cp / 0x1000) % 0x40,
	    0x80 + math.floor(cp / 0x40) % 0x40, 0x80 + cp % 0x40)
end

local decode_value

local function skip(s, i)
	return s:find("[^ \t\r\n]", i) or #s + 1
end

local function decode_string(s, i)
	local out, j = {}, i + 1
	while true do
		local c = s:sub(j, j)
		if c == "" then
			error("unterminated string at " .. i)
		elseif c == '"' then
			return table.concat(out), j + 1
		elseif c == "\\" then
			local e = s:sub(j + 1, j + 1)
			if e == "u" then
				local hex = s:sub(j + 2, j + 5)
				if not hex:match("^%x%x%x%x$") then
					error("bad \\u escape at " .. j)
				end
				local cp = tonumber(hex, 16)
				j = j + 6
				if cp >= 0xD800 and cp <= 0xDBFF and
				    s:sub(j, j + 1) == "\\u" then
					local lo = tonumber(s:sub(j + 2, j + 5), 16)
					if lo and lo >= 0xDC00 and lo <= 0xDFFF then
						cp = 0x10000 + (cp - 0xD800) * 0x400 +
						    (lo - 0xDC00)
						j = j + 6
					end
				end
				out[#out + 1] = utf8_char(cp)
			else
				local u = unescapes[e]
				if not u then
					error("bad escape at " .. j)
				end
				out[#out + 1] = u
				j = j + 2
			end
		else
			local k = s:find('["\\]', j) or #s + 1
			out[#out + 1] = s:sub(j, k - 1)
			j = k
		end
	end
end

local function decode_number(s, i)
	local j = s:find("[^-+.eE%d]", i) or #s + 1
	local n = tonumber(s:sub(i, j - 1))
	if n == nil then
		error("bad number at " .. i)
	end
	return n, j
end

local function decode_array(s, i)
	local out = {}
	ARRAY[out] = true
	i = skip(s, i + 1)
	if s:sub(i, i) == "]" then
		return out, i + 1
	end
	while true do
		out[#out + 1], i = decode_value(s, i)
		i = skip(s, i)
		local c = s:sub(i, i)
		if c == "]" then
			return out, i + 1
		elseif c ~= "," then
			error("expected , or ] at " .. i)
		end
		i = skip(s, i + 1)
	end
end

local function decode_object(s, i)
	local out = {}
	i = skip(s, i + 1)
	if s:sub(i, i) == "}" then
		return out, i + 1
	end
	while true do
		if s:sub(i, i) ~= '"' then
			error("expected a key at " .. i)
		end
		local k
		k, i = decode_string(s, i)
		i = skip(s, i)
		if s:sub(i, i) ~= ":" then
			error("expected : at " .. i)
		end
		out[k], i = decode_value(s, skip(s, i + 1))
		i = skip(s, i)
		local c = s:sub(i, i)
		if c == "}" then
			return out, i + 1
		elseif c ~= "," then
			error("expected , or } at " .. i)
		end
		i = skip(s, i + 1)
	end
end

function decode_value(s, i)
	i = skip(s, i)
	local c = s:sub(i, i)
	if c == "{" then
		return decode_object(s, i)
	elseif c == "[" then
		return decode_array(s, i)
	elseif c == '"' then
		return decode_string(s, i)
	elseif c == "-" or c:match("%d") then
		return decode_number(s, i)
	elseif s:sub(i, i + 3) == "true" then
		return true, i + 4
	elseif s:sub(i, i + 4) == "false" then
		return false, i + 5
	elseif s:sub(i, i + 3) == "null" then
		return nil, i + 4
	end
	error("unexpected character at " .. i)
end

function M.decode(s)
	local v, i = decode_value(s, 1)
	if skip(s, i) <= #s then
		error("trailing data at " .. i)
	end
	return v
end

-- Mark a table so it encodes as an array even when empty.
function M.array(t)
	t = t or {}
	ARRAY[t] = true
	return t
end

return M
