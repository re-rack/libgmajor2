# Emulator plumbing

`gm2_emu.c` and `gm2_emu.h` run the G-Major 2 application image on the xemu DSP56300
core (`libdsp56300_emu_ffi`). They:

- load the records `reverse_engineering/firmware/manifest.csv` lists into a flat P view;
- apply the firmware's X/Y/L initializer in C (`gm2_apply_init_table()`);
- install a sample rate the way the firmware does (`gm2_install_rate()`);
- present a block's subtype to the parameter accessor (`gm2_filter_subtype()`,
  `gm2_delay_subtype()`, `gm2_mod_subtype()`, `gm2_pitch_subtype()`);
- run a block activation for its level words (`gm2_block_levels()`);
- call single routines (`gm2_call()`) or enter audio continuations
  (`gm2_run_until_stop()`).

No device peripherals are modeled: accesses above the mapped windows read zero and
writes to them are dropped. The test recorder in `tests/` is the only user and
the root `Makefile` builds it. Link with `-ldsp56300_emu_ffi -ldl -lpthread -lm`.
