project('termo', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c23',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
host_os = host_machine.system()

# The tree uses these C23 features; fail here, not in the middle of the build.
c23_probe = '''
#include <stdckdint.h>
constexpr int termo_c23 = 1;
enum termo_c23_enum : unsigned char { TERMO_C23 = termo_c23 };
int main(void) { int *p = nullptr; int r; return ckd_add(&r, 1, 2) || p != nullptr; }
'''
if not cc.compiles(c23_probe, args: ['-std=c23'],
    name: 'C23 (nullptr, constexpr, fixed-type enum, stdckdint.h)')
  error('termo needs C23: GCC 14+, Clang 20+ or Xcode 26+ (Apple clang 21)')
endif

# Basic include directories
inc_dirs = include_directories('.', 'src', 'src/core', 'src/compat')

# Core engine & shared utilities
core_sources = files(
  'src/core/util.c',
  'src/core/arguments.c',
  'src/core/attributes.c',
  'src/core/alerts.c',
  'src/core/colour.c',
  'src/core/control.c',
  'src/core/control-notify.c',
  'src/core/environ.c',
  'src/core/events.c',
  'src/core/events-payload.c',
  'src/core/fuzzy.c',
  'src/core/hooks.c',
  'src/core/hyperlinks.c',
  'src/core/job.c',
  'src/core/key-bindings.c',
  'src/core/key-string.c',
  'src/core/log.c',
  'src/core/menu.c',
  'src/core/mode-tree.c',
  'src/core/monitor.c',
  'src/core/names.c',
  'src/core/paste.c',
  'src/core/popup.c',
  'src/core/prompt.c',
  'src/core/prompt-history.c',
  'src/core/regsub.c',
  'src/core/resize.c',
  'src/core/session.c',
  'src/core/sort.c',
  'src/core/spawn.c',
  'src/core/status.c',
  'src/core/style.c',
  'src/core/xmalloc.c',
)

# CLI & Interactive Commands
cmd_sources = files(
  'src/cmd/cmd.c',
  'src/cmd/find.c',
  'src/cmd/queue.c',
  'src/cmd/choose-tree.c',
  'src/cmd/command-prompt.c',
  'src/cmd/confirm-before.c',
  'src/cmd/copy-mode.c',
  'src/cmd/display-menu.c',
  'src/cmd/display-message.c',
  'src/cmd/if-shell.c',
  'src/cmd/refresh-client.c',
  'src/cmd/run-shell.c',
  'src/cmd/select-layout.c',
  'src/cmd/server-access.c',
  'src/cmd/show-messages.c',
  'src/cmd/show-prompt-history.c',
  'src/cmd/source-file.c',
  'src/cmd/wait-for.c',
  # kill
  'src/cmd/kill/pane.c',
  'src/cmd/kill/window.c',
  'src/cmd/kill/session.c',
  'src/cmd/kill/server.c',
  # list
  'src/cmd/list/buffers.c',
  'src/cmd/list/clients.c',
  'src/cmd/list/commands.c',
  'src/cmd/list/keys.c',
  'src/cmd/list/panes.c',
  'src/cmd/list/sessions.c',
  'src/cmd/list/windows.c',
  # window
  'src/cmd/window/new.c',
  'src/cmd/window/split.c',
  'src/cmd/window/select.c',
  'src/cmd/window/rename.c',
  'src/cmd/window/resize.c',
  'src/cmd/window/respawn.c',
  'src/cmd/window/rotate.c',
  'src/cmd/window/swap.c',
  'src/cmd/window/move.c',
  'src/cmd/window/find.c',
  # pane
  'src/cmd/pane/break.c',
  'src/cmd/pane/capture.c',
  'src/cmd/pane/join.c',
  'src/cmd/pane/pipe.c',
  'src/cmd/pane/resize.c',
  'src/cmd/pane/respawn.c',
  'src/cmd/pane/select.c',
  'src/cmd/pane/swap.c',
  # session
  'src/cmd/session/new.c',
  'src/cmd/session/attach.c',
  'src/cmd/session/detach.c',
  'src/cmd/session/rename.c',
  'src/cmd/session/switch.c',
  'src/cmd/session/lock.c',
  # buffer
  'src/cmd/buffer/load.c',
  'src/cmd/buffer/save.c',
  'src/cmd/buffer/paste.c',
  'src/cmd/buffer/set.c',
  # options
  'src/cmd/options/set.c',
  'src/cmd/options/show.c',
  'src/cmd/options/set_env.c',
  'src/cmd/options/show_env.c',
  # keys
  'src/cmd/keys/bind.c',
  'src/cmd/keys/unbind.c',
  'src/cmd/keys/send.c',
)

# Window subsystem
window_sources = files(
  'src/window/window.c',
  'src/window/border.c',
  'src/window/buffer.c',
  'src/window/client.c',
  'src/window/clock.c',
  'src/window/copy.c',
  'src/window/customize.c',
  'src/window/panes.c',
  'src/window/switch.c',
  'src/window/tree.c',
  'src/window/visible.c',
)

# Layout subsystem
layout_sources = files(
  'src/layout/layout.c',
  'src/layout/custom.c',
  'src/layout/set.c',
)

# Screen redraw & buffer manipulation
screen_sources = files(
  'src/screen/screen.c',
  'src/screen/redraw.c',
  'src/screen/write.c',
)

# Grid scrollback cell buffer
grid_sources = files(
  'src/grid/grid.c',
  'src/grid/view.c',
  'src/grid/reader.c',
)

# TTY & Outer Terminal Management
tty_sources = files(
  'src/tty/tty.c',
  'src/tty/acs.c',
  'src/tty/draw.c',
  'src/tty/features.c',
  'src/tty/keys.c',
  'src/tty/term.c',
)

# Server process & client multiplexing
server_sources = files(
  'src/server/server.c',
  'src/server/client.c',
  'src/server/fn.c',
  'src/server/acl.c',
)

# Client process
client_sources = files(
  'src/client/client.c',
  'src/client/proc.c',
  'src/client/file.c',
)

# Configuration & Options table
config_sources = files(
  'src/config/cfg.c',
  'src/config/options.c',
  'src/config/options-table.c',
)

# VT100 / xterm input parsing
input_sources = files(
  'src/input/input.c',
  'src/input/keys.c',
)

# Format #{...} evaluation engine
format_sources = files(
  'src/format/format.c',
  'src/format/draw.c',
)

# UTF-8 decoding & character metrics
utf8_sources = files(
  'src/utf8/utf8.c',
  'src/utf8/combined.c',
)

sources = core_sources + cmd_sources + window_sources + layout_sources + \
          screen_sources + grid_sources + tty_sources + server_sources + \
          client_sources + config_sources + input_sources + format_sources + \
          utf8_sources

# Bison generator for cmd-parse.y
bison = find_program('bison', required: true)
cmd_parse_c = custom_target(
  'cmd-parse.c',
  input: 'src/cmd/cmd-parse.y',
  output: 'cmd-parse.c',
  command: [bison, '-o', '@OUTPUT@', '@INPUT@'],
)
sources += cmd_parse_c

# Dependencies
deps = []

# libevent
libevent_dep = dependency('libevent', version: '>=2.0', required: true)
deps += libevent_dep

# curses
ncurses_dep = dependency('ncursesw', required: false)
if not ncurses_dep.found()
  ncurses_dep = dependency('ncurses', required: true)
endif
deps += ncurses_dep

# System util (for forkpty on BSD/macOS/Linux)
util_dep = cc.find_library('util', required: false)
if util_dep.found()
  deps += util_dep
endif

# Math library
m_dep = cc.find_library('m', required: false)
if m_dep.found()
  deps += m_dep
endif

# Resolv library (for b64_ntop on some OSes)
resolv_dep = cc.find_library('resolv', required: false)
if resolv_dep.found()
  deps += resolv_dep
endif

# Optional utf8proc
utf8proc_opt = get_option('utf8proc')
utf8proc_dep = dependency('libutf8proc', required: false)
if not utf8proc_dep.found()
  utf8proc_dep = dependency('utf8proc', required: utf8proc_opt)
endif
if utf8proc_dep.found()
  sources += files('src/compat/utf8proc.c')
  deps += utf8proc_dep
endif

# Optional LuaJIT (Phase 2 foundation)
luajit_opt = get_option('luajit')
luajit_dep = dependency('luajit', version: '>=2.1', required: luajit_opt)
if luajit_dep.found()
  deps += luajit_dep
  sources += files('src/lua/runtime.c', 'src/lua/api.c', 'src/lua/cmd.c',
    'src/lua/events.c', 'src/lua/keymap.c', 'src/lua/timer.c',
    'src/lua/format.c', 'src/lua/ui.c', 'src/cmd/run-lua.c')
  install_subdir('runtime', install_dir: get_option('datadir') / 'termo')
endif

# Optional Sixel support
if get_option('sixel')
  sources += files('src/image/image.c', 'src/image/sixel.c')
endif

sysconf_dir = get_option('prefix') / get_option('sysconfdir')
install_data('etc/termo.conf', install_dir: sysconf_dir / 'termo')

# Global C definitions & flags
c_args = [
  '-DTERMO_VERSION="' + meson.project_version() + '"',
  '-DTMUX_VERSION="' + meson.project_version() + '"',
  '-DTMUX_CONF="' + sysconf_dir / 'termo/termo.conf' +
    ':~/.config/termo/termo.conf:~/.config/tmux/tmux.conf:~/.tmux.conf' +
    ':~/.config/termo/init.lua"',
  '-DTERMO_RUNTIME="' + get_option('prefix') / get_option('datadir') / 'termo/runtime"',
  '-DTMUX_LOCK_CMD="lock -np"',
  '-DTMUX_TERM="screen"',
  '-DTMUX_MOUSE=1',
  '-DDEBUG',
]
if get_option('b_sanitize').contains('address')
  c_args += '-DASAN'
endif

if cc.has_header('ncurses.h', dependencies: ncurses_dep)
  c_args += '-DHAVE_NCURSES_H=1'
elif cc.has_header('curses.h', dependencies: ncurses_dep)
  c_args += '-DHAVE_CURSES_H=1'
endif

if get_option('sixel')
  c_args += '-DENABLE_SIXEL=1'
endif

if utf8proc_dep.found()
  c_args += '-DHAVE_UTF8PROC=1'
endif

if luajit_dep.found()
  c_args += '-DHAVE_LUAJIT=1'
endif

# Always needed compatibility shims
sources += files('src/compat/getopt_long.c', 'src/compat/fdforkpty.c')

# OS-specific settings and sources
if host_os == 'darwin'
  sources += files('src/osdep/osdep-darwin.c', 'src/compat/daemon.c', 'src/compat/daemon-darwin.c')
  c_args += '-DBROKEN_CMSG_FIRSTHDR'
elif host_os == 'linux'
  sources += files('src/osdep/osdep-linux.c')
  c_args += '-D_GNU_SOURCE'
elif host_os == 'freebsd'
  sources += files('src/osdep/osdep-freebsd.c')
elif host_os == 'openbsd'
  sources += files('src/osdep/osdep-openbsd.c')
elif host_os == 'netbsd'
  sources += files('src/osdep/osdep-netbsd.c')
  c_args += '-D_OPENBSD_SOURCE'
else
  error('unsupported host: ' + host_os + ' (termo targets Linux, macOS, FreeBSD, OpenBSD, NetBSD)')
endif

# Check headers
check_headers = [
  ['event2/event.h', 'HAVE_EVENT2_EVENT_H'],
  ['paths.h', 'HAVE_PATHS_H'],
  ['stdint.h', 'HAVE_STDINT_H'],
  ['bitstring.h', 'HAVE_BITSTRING_H'],
  ['util.h', 'HAVE_UTIL_H'],
  ['libutil.h', 'HAVE_LIBUTIL_H'],
  ['pty.h', 'HAVE_PTY_H'],
]

foreach item : check_headers
  hdr = item[0]
  def = item[1]
  if cc.has_header(hdr, dependencies: deps)
    c_args += '-D@0@=1'.format(def)
  endif
endforeach

# Check queue.h
if cc.has_header_symbol('sys/queue.h', 'TAILQ_REPLACE') and \
   cc.has_header_symbol('sys/queue.h', 'TAILQ_CONCAT') and \
   cc.has_header_symbol('sys/queue.h', 'TAILQ_PREV')
  c_args += '-DHAVE_QUEUE_H=1'
endif

# Check tree.h
if cc.has_header_symbol('sys/tree.h', 'RB_GENERATE') and \
   cc.has_header_symbol('sys/tree.h', 'RB_ENTRY')
  c_args += '-DHAVE_TREE_H=1'
endif

# Check functions and compile compat shims if missing
check_functions = [
  ['closefrom', 'src/compat/closefrom.c', '#include <unistd.h>'],
  ['explicit_bzero', 'src/compat/explicit_bzero.c', '#include <string.h>'],
  ['freezero', 'src/compat/freezero.c', '#include <stdlib.h>'],
  ['getpeereid', 'src/compat/getpeereid.c', '#include <sys/types.h>\n#include <unistd.h>'],
  ['getprogname', 'src/compat/getprogname.c', '#include <stdlib.h>'],
  ['htonll', 'src/compat/htonll.c', '#include <sys/types.h>\n#include <arpa/inet.h>'],
  ['ntohll', 'src/compat/ntohll.c', '#include <sys/types.h>\n#include <arpa/inet.h>'],
  ['reallocarray', 'src/compat/reallocarray.c', '#include <stdlib.h>'],
  ['recallocarray', 'src/compat/recallocarray.c', '#include <stdlib.h>'],
  ['setproctitle', 'src/compat/setproctitle.c', '#include <stdlib.h>'],
  ['strtonum', 'src/compat/strtonum.c', '#include <stdlib.h>'],
]

compat_sources = []
foreach item : check_functions
  fn = item[0]
  shim = item[1]
  prefix = item[2]
  if cc.has_function(fn, prefix: prefix, dependencies: deps)
    c_args += '-DHAVE_@0@=1'.format(fn.to_upper())
  else
    compat_sources += files(shim)
  endif
endforeach
sources += compat_sources

# Small platform facts the tree reads directly
if cc.has_header_symbol('signal.h', 'sys_signame', prefix: '#define _DARWIN_C_SOURCE\n#define _GNU_SOURCE')
  c_args += '-DHAVE_SYS_SIGNAME=1'
endif
if cc.has_function('malloc_trim', prefix: '#include <malloc.h>')
  c_args += '-DHAVE_MALLOC_TRIM=1'
endif

# systemd: socket activation and one cgroup per pane (Linux)
systemd_dep = dependency('libsystemd', required: get_option('systemd'))
if systemd_dep.found()
  sources += files('src/compat/systemd.c')
  deps += systemd_dep
  c_args += ['-DHAVE_SYSTEMD=1', '-DENABLE_CGROUPS=1']
endif

# vis(3): macOS libc has strnvis() with the dst/len arguments swapped, so
# probing for the symbol is not enough; check the OpenBSD prototype compiles.
if cc.compiles('''
#include <vis.h>
int strnvis(char *, const char *, size_t, int);
int main(void) { return VIS_DQ; }
''', name: 'OpenBSD-compatible strnvis')
  c_args += '-DHAVE_VIS=1'
else
  sources += files('src/compat/vis.c', 'src/compat/unvis.c')
endif

# Check for b64_ntop
if cc.has_function('b64_ntop', prefix: '#include <sys/types.h>\n#include <netinet/in.h>\n#include <resolv.h>', dependencies: deps)
  c_args += '-DHAVE_B64_NTOP=1'
else
  sources += files('src/compat/base64.c')
endif

# Check for imsg
if cc.has_function('imsg_init', prefix: '#include <sys/types.h>\n#include <imsg.h>', dependencies: deps)
  c_args += '-DHAVE_IMSG=1'
else
  sources += files('src/compat/imsg.c', 'src/compat/imsg-buffer.c')
endif

# Check for tiparm / tiparm_s in curses/ncurses
if cc.has_function('tiparm_s', prefix: '#include <curses.h>\n#include <term.h>', dependencies: deps)
  c_args += '-DHAVE_TIPARM_S=1'
elif cc.has_function('tiparm', prefix: '#include <curses.h>\n#include <term.h>', dependencies: deps)
  c_args += '-DHAVE_TIPARM=1'
endif

# Warnings the tree holds at zero (measured 2026-09-07, clang 21); CI adds -Werror.
# Measured and left off: -Wconversion (1574), -Wcast-qual (60).
supported_c_args = cc.get_supported_arguments([
  '-Wimplicit-fallthrough',
  '-Wshadow',
  '-Wmissing-prototypes',
  '-Wstrict-prototypes',
  '-Wvla',
  '-Wformat=2',
  '-Wsign-compare',
  # compat/queue.h coexists with the system <sys/queue.h> that macOS headers pull in
  '-Wno-macro-redefined',
  # u_char buffers passed to char APIs, 66 sites of upstream style
  '-Wno-pointer-sign',
  # glibc _FORTIFY_SOURCE marks write(2) and friends; not fixable with (void)
  '-Wno-unused-result',
])
c_args += supported_c_args

# Everything except main() as a library: the binary, the unit tests and the
# fuzzers all link it.
termo_lib = static_library(
  'termo',
  sources,
  include_directories: inc_dirs,
  dependencies: deps,
  c_args: c_args,
)

termo_exe = executable(
  'termo',
  'src/core/main.c',
  include_directories: inc_dirs,
  dependencies: deps,
  c_args: c_args,
  link_with: termo_lib,
  install: true,
)

# Tests directory
subdir('tests')
