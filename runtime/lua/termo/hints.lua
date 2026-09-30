local mode = require("termo.mode")

return {
	modes = mode.modes,
	mode = mode.define,
	enter = mode.enter,
	line = mode.line,
	setup = mode.setup,
}
