# Plan 003: unit test suite

- Status: Approved 2026-09-06; Steps 1 to 4 landed 2026-09-07; Step 5 (close the Must-cover table,
  coverage measured locally instead of a nightly gate) landed 2026-09-07
- Intent: [`intent/003-test-suite.md`](../intent/003-test-suite.md)
- Spec: [`specs/003-test-suite.md`](../specs/003-test-suite.md)

Four steps on the single working branch, each green on CI before the next. Proof for every step: `meson test -C build` passes
with sanitizers on the three CI cells, regress unchanged.

## Step 1: infrastructure and the modules that already bit us

- `meson.build`: `termo_lib`, `termo_exe` from `main.c` + `termo_lib`; `src/core/util.c` with
  the globals and helpers that were in `main.c`.
- `tests/unit/test.h`, `main.c`, `harness.c`: framework, TAP runner, bootstrap and reset.
- `tests/meson.build`: `termo-test` from `unit/*.c`, one `test()` per module; fuzzers use
  `harness.c`.
- `tests/fuzz/*-fuzzer.c`: `LLVMFuzzerInitialize` calls `termo_test_init`.
- `tests/unit/test_compat.c`, `test_options.c`, `test_cfg.c`, `test_format.c`.
- Delete `tests/unit/test_sanity.c`.
- `nightly.yml`: `coverage` job (removed in Step 5: coverage is measured locally, no gate).
- `CLAUDE.md`, `README.md`, `CONTRIBUTING.md`, `ROADMAP.md`: phases renumbered, how to write a
  unit test, `/unit` skill.

Proof: `meson test --suite unit` shows every case (TAP); `test_cfg.c` loads `etc/termo.conf` and
checks each value; `test_compat.c` passes on macOS and Linux.

## Step 2: leaf modules that phases 4 and 5 rewrite

`test_regsub.c`, `test_utf8.c`, `test_grid.c`. Proof: nightly `no-utf8proc` variant runs
`test_utf8.c` and passes.

## Step 3: text parsers

`test_colour.c`, `test_style.c`, `test_key_string.c`, `test_arguments.c`, `test_layout.c`.

## Step 4: emulation

`test_screen_write.c`, `test_input.c`. Proof: DA reply case fails when built with `-Dsixel=true`
unless the expectation is sixel-aware (the test reads `ENABLE_SIXEL`).

## Step 5: every "Must cover" row has named cases

The spec's table is the contract; Steps 1 to 4 left ten rows partial and two unmet (`format`:
"each modifier" covered seven families of about twenty; `key_string`: "whole table both ways"
covered 32 names of 79). Step 5 adds the cases below, fixes the two vacuous cases
(`compat/freezero_accepts_null`, `layout/destroying_a_zoomed_window_does_not_reenter_destroy`)
and moves the fixtures that three files duplicated into `harness.c` (`termo_test_window`,
`termo_test_session`, `termo_test_drain`, `termo_test_item`, `termo_test_wide`; `termo_test_reset`
also empties the paste stack and resets the input buffer size).

Production fixes that the cases forced, all one-liners: `options_array_set` dereferenced a null
`cause` on a bad colour; `options_parse("")` returned without setting `*key`;
`colour_parseX11` accepted `300,0,0` and truncated it; `args_string_percentage_and_expand`
read `value[-1]` on an empty string.

Quirks pinned by a test and left as upstream (`input/`, `grid/`, `screen/` follow tmux):
the trailing OSC 8 close in `grid_string_cells` repeats the last cell's code; SGR `38;2;r;g`
truncated re-reads `r`,`g` as attributes; `screen_alternate_off` restores the cursor when not in
alternate; `layout_resize_floating_pane_to` subtracts the border and the relative variant does
not; `attributes_tostring(NOATTR)` emits a name `attributes_fromstring` rejects; `style_parse`
accepts an unknown `range=` kind silently.

Cases per file (names are the `TEST(module, name)` in the tree):

- `test_grid.c` (+14): `view_insert_delete_lines_region_stop_at_rlower`,
  `view_scroll_region_up_partial_region_feeds_history`,
  `remove_history_drops_bottom_lines_and_shifts_view`,
  `collect_history_trims_ten_percent_then_all_over_limit`,
  `bounds_out_of_range_is_ignored_or_default`, `string_cells_with_sequences_exact_sgr_output`,
  `string_cells_emits_hyperlink_open_and_close`,
  `line_limit_and_in_set_see_through_padding_and_tabs`,
  `reader_cursor_jump_and_jump_back_cross_wrapped_lines`, `reader_edge_motion_wraps_and_clamps`,
  `duplicate_lines_and_compare`, `flag_strings_name_bits`,
  `view_clear_history_scrolls_only_used_lines`, `empty_line_and_clear_with_background_colour`.
- `test_colour.c` (+4): `parseX11_accepts_every_documented_form`,
  `256toRGB_table_roundtrips_through_find_rgb`, `palette_from_option_builds_default_palette`,
  `totheme_toescape_and_theme_table`.
- `test_style.c` (+7): `parse_every_attribute_name_and_negation`,
  `apply_and_add_overlay_option_onto_cell`, `parse_colour_single_value`,
  `link_nolink_and_default_reset`, `dim_width_pad_align_list_range_forms_and_bad_input_atomicity`,
  `ranges_get_range_by_position`, `scrollbar_style_from_option_defaults_and_override`.
- `test_utf8.c` (+1): `stravisx_bounds_length_and_vis_dq_escapes_dollar`.
- `test_key_string.c` (+1): `whole_table_roundtrips_both_ways` (1379 rows both ways).
- `test_regsub.c` (+1): `output_grows_with_many_replacements`.
- `test_screen_write.c` (+16), `test_layout.c` (+8), `test_input.c` (+21: 15 parser, 6
  `input_key`), `test_format.c` (+14, two hardened), `test_options.c` (+14), `test_cfg.c` (+4, one
  tightened), `test_arguments.c` (+10), `test_compat.c` (+7, one replaced): the names are the
  `TEST(` lines in each file, `<function>_<property>`.

Coverage measured locally (`build-cov`, unit suite only, line %), as information:

| File | Before Step 5 | After |
|---|---|---|
| `src/compat/vis.c` / `unvis.c` / `getopt_long.c` | 46 / 0 / 0 | 90 / 73 / 30 |
| `src/config/options.c` / `cfg.c` | 54 / 37 | 86 / 56 |
| `src/core/arguments.c` | 53 | 90 |
| `src/core/colour.c` / `style.c` / `key-string.c` / `regsub.c` | 56 / 64 / 72 / 100 | 89 / 88 / 76 / 100 |
| `src/format/format.c` | 17 | 68 |
| `src/grid/grid.c` / `view.c` / `reader.c` | 65 / 80 / 66 | 83 / 99 / 79 |
| `src/input/input.c` / `keys.c` | 40 / 0 | 77 / 78 |
| `src/layout/layout.c` / `custom.c` / `set.c` | 35 / 73 / 16 | 58 / 73 / 16 |
| `src/screen/write.c` / `screen.c` | 51 / 42 | 72 / 70 |
| `src/utf8/utf8.c` / `combined.c` | 73 / 54 | 76 / 54 |

## Close

Step 5 green in unit, lua, integration and regress under ASAN and UBSAN, and in the
`-Dluajit=disabled` variant. No coverage gate.

## Risks

- Some `options.c` entry points call server-side functions (`recalculate_sizes`,
  `server_redraw_client`) on set. They are linked from `libtermo` and are no-ops without
  clients or sessions; if one dereferences a missing global, wrap it in that test.
- `load_cfg_from_buffer` queues commands through `cmdq`; the test must drain the queue with
  `cmdq_next(NULL)` as `input-fuzzer.c` does.
