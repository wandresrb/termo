py = import('python').find_installation('python3')

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
  test('unit-' + m, termo_test, args: [m], suite: 'unit', protocol: 'tap', timeout: 60)
endforeach

# Lua specs inside a real server (TAP relayed by run-lua), and the Lua UI
# driven through a pty-attached client
if luajit_dep.found()
  test('lua', find_program('sh'), args: [files('lua/run.sh'), termo_exe],
    suite: 'lua', protocol: 'tap', timeout: 60)
  test('integration_lua_ui', py,
    args: [files('integration/test_lua_ui.py'), termo_exe],
    suite: 'integration',
    timeout: 180,
  )
endif

# Integration test suite in Python
test('integration_termo', py,
  args: [files('integration/test_termo.py'), termo_exe],
  suite: 'integration',
  timeout: 30,
)

# Upstream regression suite, all scripts, in parallel
test('regress', py,
  args: [files('regress/runner.py'), termo_exe],
  suite: 'regress',
  timeout: 1500,
)

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
