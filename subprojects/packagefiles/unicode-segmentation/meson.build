project('unicode-segmentation', 'rust', version: '1.13.3', license: 'MIT OR Apache-2.0',
  meson_version: '>= 1.4.0')

lib = static_library('unicode_segmentation', 'src/lib.rs',
  rust_abi: 'rust',
  rust_args: ['--edition=2018', '--cap-lints', 'allow'],
)
meson.override_dependency('unicode-segmentation', declare_dependency(link_with: lib))
