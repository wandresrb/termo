# Meson build de termo, junto a autotools (Makefile.am + configure.ac), no en su lugar.
# Los dos describen el mismo programa: si cambias la lista de fuentes o una comprobación en
# uno, cámbiala en el otro. Verificado en Linux; macOS/BSD/Solaris siguen la lógica de
# configure.ac pero no se han probado.
#
#   meson setup build && meson compile -C build     # ./build/termo
#   meson install -C build                          # bin/termo y man1/termo.1 bajo --prefix
#
# Diferencia con autotools: el ejecutable se llama `termo`, no `tmux`.
project('termo', 'c', version: 'next',
  meson_version: '>= 1.1',
  default_options: ['c_std=gnu99', 'warning_level=1', 'debug=false', 'optimization=2'],
  license: 'ISC')

cc = meson.get_compiler('c')
os = host_machine.system()

# La versión sale de configure.ac para que no haya dos sitios donde subirla.
ac_init = run_command('sed', '-n', 's/^AC_INIT(\\[tmux\\], *\\(.*\\))$/\\1/p',
  files('configure.ac'), check: true)
version = ac_init.stdout().strip()
if version == ''
  error('no encuentro la versión en AC_INIT de configure.ac')
endif

platform = {
  'linux': 'linux', 'darwin': 'darwin', 'freebsd': 'freebsd', 'openbsd': 'openbsd',
  'netbsd': 'netbsd', 'dragonfly': 'dragonfly', 'sunos': 'sunos', 'haiku': 'haiku',
  'cygwin': 'cygwin',
}.get(os, 'unknown')

# --- Definiciones del sistema (lo que AC_USE_SYSTEM_EXTENSIONS y compañía ponían) -----
sysdefs = []
if os == 'linux'
  sysdefs += ['-D_GNU_SOURCE', '-D_DEFAULT_SOURCE', '-D_XOPEN_SOURCE=600']
elif os == 'darwin'
  sysdefs += ['-D_DARWIN_C_SOURCE']
elif os == 'netbsd'
  sysdefs += ['-D_NETBSD_SOURCE', '-D_OPENBSD_SOURCE']
elif os == 'sunos'
  sysdefs += ['-D__EXTENSIONS__', '-D_XPG6']
elif os == 'haiku'
  sysdefs += ['-D_BSD_SOURCE']
elif os == 'cygwin'
  sysdefs += ['-D_GNU_SOURCE', '-DTMUX_SOCK_PERM=0']
endif

# Las comprobaciones de abajo deben compilar con las mismas definiciones que el código.
chk = sysdefs
defs = []         # -D... que acaban en add_project_arguments
libs = []         # dependencias y bibliotecas que se enlazan
compat = []       # fuentes de compat/ que sustituyen lo que el sistema no trae

# --- Opciones -----------------------------------------------------------------------
# --- Bibliotecas --------------------------------------------------------------------
libevent = dependency(['libevent_core', 'libevent'], version: '>= 2', required: false)
if not libevent.found()
  libevent = cc.find_library('event_core', required: false)
endif
if not libevent.found()
  libevent = cc.find_library('event', required: true)
endif
libs += libevent
if cc.has_header('event2/event.h', dependencies: libevent)
  defs += '-DHAVE_EVENT2_EVENT_H=1'
else
  defs += '-DHAVE_EVENT_H=1'
endif

ncurses = dependency(['tinfow', 'tinfo', 'ncursesw', 'ncurses'], required: false)
if not ncurses.found()
  ncurses = cc.find_library('curses', required: true)
endif
libs += ncurses
if cc.has_header('ncurses.h', dependencies: ncurses, args: chk)
  defs += '-DHAVE_NCURSES_H=1'
elif cc.has_header('curses.h', dependencies: ncurses, args: chk)
  defs += '-DHAVE_CURSES_H=1'
else
  error('no encuentro ncurses.h ni curses.h')
endif
foreach f : ['tiparm', 'tiparm_s']
  if cc.has_function(f, dependencies: ncurses, prefix: '#include <term.h>', args: chk)
    defs += '-DHAVE_@0@=1'.format(f.to_upper())
  endif
endforeach

libm = cc.find_library('m', required: false)
libs += libm
librt = cc.find_library('rt', required: false)     # clock_gettime en glibc viejas
libs += librt
libutil = cc.find_library('util', required: false) # forkpty, imsg, fdforkpty
libs += libutil
libresolv = cc.find_library('resolv', required: false) # b64_ntop
libbsd = cc.find_library('bsd', required: false)   # flock en algunos sistemas
if os == 'sunos'
  libs += [cc.find_library('socket', required: false), cc.find_library('nsl', required: false)]
endif

utf8proc = dependency('libutf8proc', required: get_option('utf8proc'))
if utf8proc.found()
  libs += utf8proc
  defs += '-DHAVE_UTF8PROC=1'
  compat += 'compat/utf8proc.c'
elif os == 'darwin'
  warning('macOS: sin utf8proc el ancho de caracteres Unicode es malo; usa -Dutf8proc=enabled')
endif

systemd = dependency('libsystemd', required: get_option('systemd'))
if systemd.found()
  libs += systemd
  defs += '-DHAVE_SYSTEMD=1'
  compat += 'compat/systemd.c'
  if get_option('cgroups').enabled() or get_option('cgroups').auto()
    defs += '-DENABLE_CGROUPS=1'
  endif
elif get_option('cgroups').enabled()
  error('cgroups necesita systemd (-Dsystemd=enabled)')
endif

utempter = cc.find_library('utempter', required: get_option('utempter'))
if utempter.found() and cc.has_header('utempter.h', args: chk)
  libs += utempter
  defs += '-DHAVE_UTEMPTER=1'
endif

# --- Cabeceras ----------------------------------------------------------------------
foreach h : ['bitstring.h', 'dirent.h', 'fcntl.h', 'inttypes.h', 'libproc.h', 'libutil.h',
             'ndir.h', 'paths.h', 'pty.h', 'stdint.h', 'sys/dir.h', 'sys/ndir.h',
             'sys/tree.h', 'ucred.h', 'util.h']
  if cc.has_header(h, args: chk)
    defs += '-DHAVE_@0@=1'.format(h.underscorify().to_upper())
  endif
endforeach

# --- Funciones ----------------------------------------------------------------------
fn_prefix = '''
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>
#include <dirent.h>
'''
fn_deps = [libm, librt, libutil, libbsd]

# Existen o no; si existen solo se define HAVE_X.
# Cada una declarada en su cabecera; incluirla en el prefijo común rompería otros sistemas.
extra_hdr = {'flock': '#include <sys/file.h>', 'prctl': '#include <sys/prctl.h>',
             'proc_pidinfo': '#include <libproc.h>', 'getpeerucred': '#include <ucred.h>'}
foreach f : ['dirfd', 'flock', 'prctl', 'proc_pidinfo', 'getpeerucred', 'sysconf']
  if cc.has_function(f, prefix: fn_prefix + extra_hdr.get(f, ''), args: chk,
                     dependencies: fn_deps)
    defs += '-DHAVE_@0@=1'.format(f.to_upper())
  endif
endforeach

# Con implementación en compat/ cuando el sistema no la trae (AC_REPLACE_FUNCS).
foreach f : ['asprintf', 'cfmakeraw', 'clock_gettime', 'closefrom', 'explicit_bzero',
             'freezero', 'getdtablecount', 'getdtablesize', 'getpeereid', 'getline',
             'getprogname', 'htonll', 'memmem', 'ntohll', 'setenv', 'setproctitle',
             'strcasestr', 'strlcat', 'strlcpy', 'strndup', 'strsep']
  if cc.has_function(f, prefix: fn_prefix, args: chk, dependencies: fn_deps)
    defs += '-DHAVE_@0@=1'.format(f.to_upper())
  else
    compat += 'compat/@0@.c'.format(f)
  endif
endforeach
if not cc.has_function('strnlen', prefix: fn_prefix, args: chk)
  compat += 'compat/strnlen.c'
endif

# Estas tres existen en el sistema pero pueden estar rotas (sanitizers de clang):
# se prueban ejecutándolas, como hace configure.ac.
run_ok = meson.can_run_host_binaries()
strtonum_ok = run_ok and cc.run('''#include <stdlib.h>
int main(void) { return strtonum("0", 0, 1, NULL) == 0 ? 0 : 1; }''',
  args: chk, name: 'strtonum funciona').returncode() == 0
if strtonum_ok
  defs += '-DHAVE_STRTONUM=1'
else
  compat += 'compat/strtonum.c'
endif
if not (run_ok and cc.run('''#include <stdlib.h>
int main(void) { return reallocarray(NULL, 1, 1) == NULL; }''',
    args: chk, name: 'reallocarray funciona').returncode() == 0)
  compat += 'compat/reallocarray.c'
endif
if not (run_ok and cc.run('''#include <stdlib.h>
int main(void) { return recallocarray(NULL, 1, 1, 1) == NULL; }''',
    args: chk, name: 'recallocarray funciona').returncode() == 0)
  compat += 'compat/recallocarray.c'
endif

# Siempre el getopt de compat: el de glibc no respeta el orden de argumentos y el de musl
# no pone optarg a NULL (ver configure.ac).
compat += 'compat/getopt_long.c'

if cc.has_function('sys_signame', prefix: '#include <signal.h>', args: chk) or \
   cc.links('#include <signal.h>\nextern const char *const sys_signame[];\nint main(void){return sys_signame[1]==0;}',
     args: chk, name: 'sys_signame')
  defs += '-DHAVE_SYS_SIGNAME=1'
endif

# b64_ntop: en libc o en libresolv; si no, compat/base64.c.
b64_src = '''#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
int main(void) { b64_ntop(NULL, 0, NULL, 0); return 0; }'''
if cc.links(b64_src, args: chk, name: 'b64_ntop')
  defs += '-DHAVE_B64_NTOP=1'
elif libresolv.found() and cc.links(b64_src, args: chk, dependencies: libresolv, name: 'b64_ntop con -lresolv')
  libs += libresolv
  defs += '-DHAVE_B64_NTOP=1'
else
  compat += 'compat/base64.c'
endif

# err.h y compañía.
if not (cc.has_function('err', prefix: '#include <err.h>', args: chk) and
        cc.has_function('errx', prefix: '#include <err.h>', args: chk) and
        cc.has_function('warn', prefix: '#include <err.h>', args: chk) and
        cc.has_function('warnx', prefix: '#include <err.h>', args: chk))
  compat += 'compat/err.c'
endif

if cc.has_function('imsg_add', prefix: '#include <sys/types.h>\n#include <imsg.h>', args: chk,
                   dependencies: libutil)
  defs += '-DHAVE_IMSG=1'
else
  compat += ['compat/imsg.c', 'compat/imsg-buffer.c']
endif

if cc.has_function('daemon', prefix: '#include <stdlib.h>\n#include <unistd.h>', args: chk)
  defs += '-DHAVE_DAEMON=1'
else
  compat += 'compat/daemon.c'
endif

if cc.has_function('stravis', prefix: '#include <stdlib.h>\n#include <vis.h>', args: chk) and \
   cc.has_header_symbol('vis.h', 'VIS_DQ', prefix: '#include <stdlib.h>', args: chk)
  defs += '-DHAVE_VIS=1'
else
  compat += ['compat/vis.c', 'compat/unvis.c']
endif

fdforkpty_pre = '#include <sys/types.h>\n#include <termios.h>\n#include <pty.h>'
if cc.has_function('fdforkpty', dependencies: libutil, args: chk)
  defs += '-DHAVE_FDFORKPTY=1'
else
  compat += 'compat/fdforkpty.c'
endif
if cc.has_function('forkpty', prefix: fdforkpty_pre, dependencies: libutil, args: chk)
  defs += '-DHAVE_FORKPTY=1'
else
  compat += 'compat/forkpty-@0@.c'.format(platform)
endif

# sys/queue.h del sistema solo sirve si trae todo lo que tmux usa.
queue_ok = true
foreach s : ['TAILQ_CONCAT', 'TAILQ_PREV', 'TAILQ_REPLACE']
  queue_ok = queue_ok and cc.has_header_symbol('sys/queue.h', s, args: chk)
endforeach
if queue_ok
  defs += '-DHAVE_QUEUE_H=1'
endif

if cc.links('''#include <stdio.h>
extern char *__progname;
int main(void) { printf("%s\\n", __progname); return 0; }''', args: chk, name: '__progname')
  defs += '-DHAVE___PROGNAME=1'
endif
if cc.links('''#include <errno.h>
#include <stdio.h>
int main(void) { printf("%s\\n", program_invocation_short_name); return 0; }''',
    args: chk, name: 'program_invocation_short_name')
  defs += '-DHAVE_PROGRAM_INVOCATION_SHORT_NAME=1'
endif
if cc.has_header_symbol('sys/prctl.h', 'PR_SET_NAME', args: chk)
  defs += '-DHAVE_PR_SET_NAME=1'
endif
if cc.has_header_symbol('sys/socket.h', 'SO_PEERCRED', args: chk)
  defs += '-DHAVE_SO_PEERCRED=1'
endif
if cc.has_header_symbol('fcntl.h', 'F_CLOSEM', args: chk)
  defs += '-DHAVE_FCNTL_CLOSEM=1'
endif
if run_command('test', '-d', '/proc/self', check: false).returncode() == 0
  defs += '-DHAVE_PROC_PID=1'
endif

# glibc devuelve mal la memoria al sistema salvo que tmux llame a malloc_trim.
if cc.links('''#include <stdlib.h>
#ifdef __GLIBC__
#include <malloc.h>
int main(void) { malloc_trim(0); return 0; }
#else
no
#endif''', args: chk, name: 'malloc_trim')
  defs += '-DHAVE_MALLOC_TRIM=1'
endif

if not cc.has_header_symbol('sys/socket.h', 'CMSG_DATA', args: chk)
  error('no encuentro CMSG_DATA; configure.ac prueba aquí _XOPEN_SOURCE(_EXTENDED), no portado')
endif

# Particularidades de macOS (ver el case "darwin" de configure.ac).
if os == 'darwin'
  defs += ['-DBROKEN___DEAD=1', '-DBROKEN_CMSG_FIRSTHDR=1']
  compat += ['compat/daemon.c', 'compat/daemon-darwin.c']
endif

# --- TERM y comando de bloqueo por defecto ------------------------------------------
default_term = get_option('default_term')
if default_term == ''
  default_term = 'screen'
  if run_ok
    foreach t : ['screen-256color', 'tmux', 'tmux-256color']   # gana el último que exista
      probe = cc.run('''#include <stdlib.h>
#include <ncurses.h>
#include <term.h>
int main(void) { return setupterm("@0@", -1, NULL) == OK ? 0 : 1; }'''.format(t),
        dependencies: ncurses, args: chk, name: 'terminfo ' + t)
      if probe.compiled() and probe.returncode() == 0
        default_term = t
      endif
    endforeach
  endif
elif not (default_term.startswith('screen') or default_term.startswith('tmux'))
  error('default_term inadecuado: debe empezar por screen o tmux')
endif

lock_cmd = 'lock -np'
if platform == 'linux' and find_program('vlock', required: false).found()
  lock_cmd = 'vlock'
endif

# --- Argumentos de compilación -------------------------------------------------------
sysconfdir = get_option('prefix') / get_option('sysconfdir')
defs += [
  '-DTMUX_VERSION="@0@"'.format(version),
  # Mismo orden de búsqueda que Makefile.am: aquí vive ~/.config/tmux/tmux.conf.
  '-DTMUX_CONF="@0@/tmux.conf:~/.tmux.conf:$XDG_CONFIG_HOME/tmux/tmux.conf:~/.config/tmux/tmux.conf"'.format(sysconfdir),
  '-DTMUX_LOCK_CMD="@0@"'.format(lock_cmd),
  '-DTMUX_TERM="@0@"'.format(default_term),
  '-DTMUX_MOUSE=1',
]
if get_option('socket_path') != ''
  defs += '-DTMUX_SOCK="@0@"'.format(get_option('socket_path'))
endif
if get_option('sixel')
  defs += '-DENABLE_SIXEL=1'
endif
if get_option('debug_checks')
  defs += '-DDEBUG'
endif

add_project_arguments(sysdefs, defs, language: 'c')
# Los fuentes incluyen "tmux.h" y "compat.h" con comillas, y cmd-parse.c nace en el build.
add_project_arguments('-iquote', meson.project_source_root(),
                      '-iquote', meson.project_build_root(), language: 'c')
# Avisos de tmux que hacen ruido y no indican fallos (los mismos que apaga Makefile.am).
add_project_arguments(cc.get_supported_arguments(
  '-Wno-pointer-sign', '-Wno-attributes', '-Wno-unused-result', '-Wno-format-y2k',
  '-Wno-maybe-uninitialized', '-Wno-use-after-free', '-Wno-unknown-warning-option'),
  language: 'c')

# --- cmd-parse.y -> cmd-parse.c -------------------------------------------------------
yacc = find_program('bison', 'byacc', 'yacc')
yacc_args = yacc.full_path().endswith('bison') ? ['-y'] : []
cmd_parse = custom_target('cmd-parse',
  input: 'cmd-parse.y',
  output: 'cmd-parse.c',
  command: [yacc, yacc_args, '-o', '@OUTPUT@', '@INPUT@'])

# --- Fuentes (la lista de dist_tmux_SOURCES de Makefile.am) ---------------------------
sources = files(
  'alerts.c', 'arguments.c', 'attributes.c', 'cfg.c', 'client.c',
  'cmd-attach-session.c', 'cmd-bind-key.c', 'cmd-break-pane.c', 'cmd-capture-pane.c',
  'cmd-choose-tree.c', 'cmd-command-prompt.c', 'cmd-confirm-before.c',
  'cmd-copy-mode.c', 'cmd-detach-client.c', 'cmd-display-menu.c',
  'cmd-display-message.c', 'cmd-find-window.c', 'cmd-find.c', 'cmd-if-shell.c',
  'cmd-join-pane.c', 'cmd-kill-pane.c', 'cmd-kill-server.c', 'cmd-kill-session.c',
  'cmd-kill-window.c', 'cmd-list-buffers.c', 'cmd-list-clients.c',
  'cmd-list-commands.c', 'cmd-list-keys.c', 'cmd-list-panes.c', 'cmd-list-sessions.c',
  'cmd-list-windows.c', 'cmd-load-buffer.c', 'cmd-lock-server.c', 'cmd-move-window.c',
  'cmd-new-session.c', 'cmd-new-window.c', 'cmd-paste-buffer.c', 'cmd-pipe-pane.c',
  'cmd-queue.c', 'cmd-refresh-client.c', 'cmd-rename-session.c', 'cmd-rename-window.c',
  'cmd-resize-pane.c', 'cmd-resize-window.c', 'cmd-respawn-pane.c',
  'cmd-respawn-window.c', 'cmd-rotate-window.c', 'cmd-run-shell.c',
  'cmd-save-buffer.c', 'cmd-select-layout.c', 'cmd-select-pane.c',
  'cmd-select-window.c', 'cmd-send-keys.c', 'cmd-server-access.c', 'cmd-set-buffer.c',
  'cmd-set-environment.c', 'cmd-set-option.c', 'cmd-show-environment.c',
  'cmd-show-messages.c', 'cmd-show-options.c', 'cmd-show-prompt-history.c',
  'cmd-source-file.c', 'cmd-split-window.c', 'cmd-swap-pane.c', 'cmd-swap-window.c',
  'cmd-switch-client.c', 'cmd-unbind-key.c', 'cmd-wait-for.c', 'cmd.c', 'colour.c',
  'control-notify.c', 'control.c', 'environ.c', 'events-payload.c', 'events.c',
  'file.c', 'format-draw.c', 'format.c', 'fuzzy.c', 'grid-reader.c', 'grid-view.c',
  'grid.c', 'hooks.c', 'hyperlinks.c', 'input-keys.c', 'input.c', 'job.c', 'json.c',
  'key-bindings.c', 'key-string.c', 'layout-custom.c', 'layout-set.c', 'layout.c',
  'log.c', 'menu.c', 'mode-tree.c', 'monitor.c', 'names.c', 'options-table.c',
  'options.c', 'paste.c', 'proc.c', 'prompt-history.c', 'prompt.c', 'regsub.c',
  'resize.c', 'screen-redraw.c', 'screen-write.c', 'screen.c', 'server-acl.c',
  'server-client.c', 'server-fn.c', 'server.c', 'session.c', 'sort.c', 'spawn.c',
  'status.c', 'style.c', 'tmux.c', 'tty-acs.c', 'tty-draw.c', 'tty-features.c',
  'tty-keys.c', 'tty-term.c', 'tty.c', 'utf8-combined.c', 'utf8.c', 'window-border.c',
  'window-buffer.c', 'window-client.c', 'window-clock.c', 'window-copy.c',
  'window-customize.c', 'window-panes.c', 'window-switch.c', 'window-tree.c',
  'window-visible.c', 'window.c', 'xmalloc.c',
  'osdep-@0@.c'.format(platform),
)
if get_option('sixel')
  sources += files('image.c', 'image-sixel.c')
endif

termo = executable('termo', sources, cmd_parse, files(compat),
  dependencies: libs,
  install: true)

# --- Man page: tmux.1 trae @SYSCONFDIR@ y se instala como termo.1 (mdoc) -------------
configure_file(
  input: 'tmux.1',
  output: 'termo.1',
  configuration: {'SYSCONFDIR': sysconfdir},
  install: true,
  install_dir: get_option('mandir') / 'man1')

test('version', termo, args: ['-V'])

summary({
  'versión': version, 'plataforma': platform, 'TERM por defecto': default_term,
  'comando de bloqueo': lock_cmd, 'compat/': ' '.join(compat),
}, section: 'termo')
summary({
  'utf8proc': utf8proc.found(), 'systemd': systemd.found(), 'utempter': utempter.found(),
  'sixel': get_option('sixel'),
}, section: 'Opciones', bool_yn: true)
