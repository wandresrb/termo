project('unicode-width', 'rust', version: '0.2.2', license: 'MIT OR Apache-2.0',
  meson_version: '>= 1.4.0')

lib = static_library('unicode_width', 'src/lib.rs',
  rust_abi: 'rust',
  rust_args: ['--edition=2021', '--cfg', 'feature="cjk"', '--cfg', 'feature="std"',
              '--cap-lints', 'allow'],
)
meson.override_dependency('unicode-width', declare_dependency(link_with: lib))
