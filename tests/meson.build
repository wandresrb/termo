pymod = import('python')
py = pymod.find_installation('python3')

# Unit tests: one binary, TAP output, `termo-test [module [case]]`.
unit_sources = files(
  'unit/main.c',
  'unit/harness.c',
  'unit/test_compat.c',
  'unit/test_options.c',
  'unit/test_cfg.c',
  'unit/test_format.c',
  'unit/test_regsub.c',
  'unit/test_utf8.c',
  'unit/test_grid.c',
  'unit/test_colour.c',
  'unit/test_style.c',
  'unit/test_key_string.c',
  'unit/test_arguments.c',
  'unit/test_layout.c',
  'unit/test_screen_write.c',
  'unit/test_input.c',
)
unit_modules = ['compat', 'options', 'cfg', 'format', 'regsub', 'utf8', 'grid',
                'colour', 'style', 'key_string', 'arguments', 'layout',
                'screen_write', 'input']
if luajit_dep.found()
  unit_sources += files('unit/test_lua.c')
  unit_modules += 'lua'
endif
termo_test = executable('termo-test', unit_sources,
  include_directories: [inc_dirs, include_directories('unit')],
  dependencies: deps,
  c_args: c_args + ['-DTERMO_SOURCE_ROOT="' + meson.project_source_root() + '"'],
  link_with: termo_lib,
)
foreach m : unit_modules
  test('unit-' + m, termo_test, args: [m], suite: 'unit', protocol: 'tap', timeout: 60,
    priority: 100)
endforeach

# Lua specs inside a real server (TAP relayed by run-lua)
if luajit_dep.found()
  test('lua', find_program('sh'), args: [files('lua/run.sh'), termo_exe],
    suite: 'lua', protocol: 'tap', timeout: 60, priority: 50)
endif

# End-to-end: pytest drives the binary over its socket, control mode and a
# pty. One test per module; every server has its own socket, so they run in
# parallel. The Lua modules skip themselves on a build without LuaJIT.
py_e2e = pymod.find_installation('python3', modules: ['pytest'], required: get_option('e2e'))
if py_e2e.found()
  e2e_env = {
    'TERMO_BIN': termo_exe.full_path(),
    'TERMO_RUNTIME': meson.project_source_root() / 'runtime',
    'TERMO_HAS_LUA': luajit_dep.found() ? '1' : '0',
    'TERMO_E2E_TIMEOUT_SCALE': get_option('b_sanitize').contains('address') ? '3' : '1',
    'TERMO_E2E_UNDER_MESON': '1',
    'PYTHONDONTWRITEBYTECODE': '1',
  }
  foreach m : ['cli', 'session', 'control', 'screen', 'layout', 'lua', 'lua_ui']
    test('e2e-' + m, py_e2e,
      args: ['-m', 'pytest', '-q', '-p', 'no:cacheprovider',
             '--junitxml=' + meson.project_build_root() / 'meson-logs' / 'e2e-' + m + '.xml',
             files('e2e/test_' + m + '.py'), '--termo', termo_exe.full_path()],
      env: e2e_env,
      depends: termo_exe,
      suite: m == 'cli' ? ['e2e', 'smoke'] : ['e2e'],
      timeout: 120,
      workdir: meson.current_source_dir() / 'e2e')
  endforeach
else
  warning('pytest not found: the e2e suite is not registered (-De2e=enabled to require it)')
endif

# libFuzzer harnesses for the four external-input parsers. The library is
# rebuilt with coverage instrumentation so the fuzzer can see branches.
fuzz_opt = get_option('fuzz')
if not fuzz_opt.disabled()
  fuzz_ok = cc.links('''
int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n) { (void)d; (void)n; return 0; }
''', args: ['-fsanitize=fuzzer'], name: 'libFuzzer runtime')
  if not fuzz_ok and fuzz_opt.enabled()
    error('fuzz needs clang with the libFuzzer runtime (Apple clang does not ship it)')
  endif
  if fuzz_ok
    fuzz_lib = static_library('termo_fuzz', sources,
      include_directories: inc_dirs,
      dependencies: deps,
      c_args: c_args + ['-fsanitize=fuzzer-no-link'],
    )
    foreach t : ['cmd-parse', 'format', 'input', 'style']
      fuzz_exe = executable(t + '-fuzzer', 'fuzz/' + t + '-fuzzer.c', files('unit/harness.c'),
        include_directories: [inc_dirs, include_directories('unit')],
        dependencies: deps,
        c_args: c_args,
        link_with: fuzz_lib,
        link_args: ['-fsanitize=fuzzer'],
      )
      test('fuzz_' + t, fuzz_exe,
        args: ['-runs=2000', '-max_len=512',
               '-dict=' + meson.current_source_dir() / 'fuzz' / t + '-fuzzer.dict',
               meson.current_source_dir() / 'fuzz/corpus' / t],
        suite: 'fuzz',
        timeout: 300,
      )
    endforeach
  endif
endif
