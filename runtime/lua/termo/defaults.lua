local opt = termo.opt
local keymap = termo.keymap

opt.history_limit = 50000
opt.renumber_windows = true
opt.mouse = true
opt.escape_time = 10
opt.focus_events = true
opt.set_clipboard = "on"
termo.cmd("set -sa terminal-features ',*:256:RGB'")
opt.default_terminal = "tmux-256color"

opt.mode_keys = "vi"
opt.status_keys = "vi"
keymap.set("copy-mode-vi", "v", "send -X begin-selection")
keymap.set("copy-mode-vi", "y", "send -X copy-pipe-and-cancel")

keymap.set("f", "new-pane")
keymap.set("F", "if -F '#{pane_floating_flag}' { join-pane -h -t '{top-left}' } { break-pane -W }")
keymap.set("C-f", "move-pane -P front")

keymap.set("M-s", "stack-pane")
keymap.set("M-S", "stack-pane -n")

opt.resurrect = "on"
