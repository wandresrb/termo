-- The Lua side of termo. termo.api holds the C functions; this module adds
-- the ergonomics on top of them. Step 1 exposes the raw API only.
local termo = termo

termo.version = termo.api.version

return termo
