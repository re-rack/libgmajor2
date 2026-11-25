/* Function-level DSP56300 harness for the G-Major 2 application image.
 *
 * Loads the application load records into a flat P memory view, reproduces the
 * firmware's own X/Y/L initializer in C, and calls single DSP functions with a
 * synthetic JSR + STOP stub. No device peripherals are modeled: accesses above
 * the mapped windows read back zero and writes to them are dropped.
 */
#ifndef GM2_EMU_H
#define GM2_EMU_H

#include <stdint.h>

#define GM2_MEM_WORDS 0x100000u /* X, Y and P are mapped over 0..GM2_MEM_WORDS-1 */
#define GM2_STUB_ADDR 0xFFF00u  /* unused P above every loaded record */
#define GM2_SCRATCH_ADDR 0xFFF10u
#define GM2_INIT_TABLE 0x80000u /* four-word descriptors: flags, dst, src, count */
#define GM2_CHAIN 0x0985EFu     /* X: descriptor record pointer per algorithm */
/* The software stack the reset code sets (P:0x0000AE). Calls that must keep the
 * Modulation block's waveform tables at X:0x1000.. use it instead of 0x1000. */
#define GM2_DEVICE_STACK 0x0005A9u
#define GM2_FILTER_SUBTYPES 0x084B79u /* P: dispatcher, table, word per subtype */
#define GM2_MOD_SUBTYPES 0x08532Eu    /* P: dispatcher, table, word per subtype */

enum gm2_space { GM2_X = 0, GM2_Y = 1, GM2_P = 2 };

/* 24-bit two's complement word <-> int32, the DSP's sample representation. */
static inline int32_t gm2_sign24(uint32_t v) {
  v &= 0xFFFFFFu;
  return (v & 0x800000u) != 0 ? -(int32_t)(0x1000000u - v) : (int32_t)v;
}

static inline uint32_t gm2_word24(int32_t v) { return (uint32_t)v & 0xFFFFFFu; }

enum {
  GM2_CALL_OK = 0,
  GM2_CALL_NO_RETURN = 1, /* budget exhausted, or stopped somewhere unexpected */
};

typedef struct gm2_emu gm2_emu;

gm2_emu *gm2_create(void);
void gm2_destroy(gm2_emu *emu);

/* Raw word access; out-of-range addresses read zero and drop writes. */
uint32_t gm2_read(const gm2_emu *emu, enum gm2_space sp, uint32_t addr);
void gm2_write(gm2_emu *emu, enum gm2_space sp, uint32_t addr, uint32_t value);
void gm2_write_run(gm2_emu *emu, enum gm2_space sp, uint32_t addr, const uint32_t *in,
                   uint32_t words);

/* Live backing store of one space, GM2_MEM_WORDS words. Safe to read directly;
 * data-space writes may go through it, P-space writes must use gm2_write(). */
uint32_t *gm2_raw(gm2_emu *emu, enum gm2_space sp);

/* Load every record `<dir>/manifest.csv` lists into P memory, in manifest order:
 * all but the bootstrap, terminator and filler rows. Returns 0 on success, -1 on
 * error. */
int gm2_load_application(gm2_emu *emu, const char *dir, int verbose);

/* The firmware reads some descriptor records through X even though the load
 * stream places them in P. Fill the missing X words from P, leaving X words that
 * already hold data alone, and return how many words were filled. */
uint32_t gm2_fill_x_from_p(gm2_emu *emu, uint32_t addr, uint32_t words);

/* Install `rate` the way P:0x083B5C does: P:0x000BFD converts the integer to the
 * firmware float, then P:0x082DC0 writes the derived constants to X:0x099429.
 * Returns 0, or -1 when a routine does not return. */
int gm2_install_rate(gm2_emu *emu, unsigned rate);

/* Mirror the descriptor record of `algorithm` from GM2_CHAIN, so the accessor
 * P:0x0832CB reads its clamp words instead of zeros. Returns the words filled,
 * or -1 when the chain entry has no P-side record. */
int gm2_mirror_descriptor(gm2_emu *emu, unsigned algorithm);

/* Present the Filter block (algorithm 1) as it runs subtype 0..6, the way the
 * device's shared P/X memory shows it to the accessor: the subtype's sub-record
 * (P:0x085326 for subtypes 0..2, P:0x08504E for 3 and 4, P:0x084E66 for 5 and 6)
 * in X:(GM2_CHAIN + 1), mirrored with the subtype's descriptor table from
 * GM2_FILTER_SUBTYPES, and the record's table word pointing at that table.
 * Raw values stay the caller's. Returns 0, or -1 for a subtype outside 0..6. */
int gm2_filter_subtype(gm2_emu *emu, unsigned subtype);

/* Present the Delay block (algorithm 4) as it runs subtype 0..3: the master
 * record P:0x086275 and all four subtype descriptor tables (P:0x085EB5,
 * P:0x085FA5, P:0x086095, P:0x086185 from X:0x098ED5..0x098ED8) mirrored to X,
 * with P:0x08627C and X:0x08627C pointing at `subtype`'s table as P:0x094A03's
 * `movem r3,p:$8627c` leaves it on the device's shared P/X memory. Returns 0,
 * or -1 for a subtype outside 0..3. */
int gm2_delay_subtype(gm2_emu *emu, unsigned subtype);

/* Present the Modulation block (algorithm 3) as it runs subtype 0..12: the
 * subtype's sub-record (P:0x085EAD for subtypes 0..8, P:0x08562D for 9 and 10,
 * P:0x085815 for 11 and 12) in X:(GM2_CHAIN + 3), mirrored with all thirteen
 * subtype descriptor tables from GM2_MOD_SUBTYPES, and the master and sub-record
 * table words pointing at `subtype`'s table as P:0x092A3B, P:0x092F8A, P:0x09360D
 * and P:0x094037 leave them on the device's shared P/X memory. Returns 0, or -1
 * for a subtype outside 0..12. */
int gm2_mod_subtype(gm2_emu *emu, unsigned subtype);

/* Present the Pitch block (algorithm 2) as it runs subtype 0..4: the master
 * record P:0x084B71 and all five subtype descriptor tables (P:0x0846C1,
 * P:0x0847B1, P:0x0848A1, P:0x084991, P:0x084A81 from X:0x098853..0x098857)
 * mirrored to X, with P:0x084B78 and X:0x084B78 pointing at `subtype`'s table
 * as P:0x090F25's `movem r6,p:$84b78` leaves it on the device's shared P/X
 * memory, and the global Tuner Ref (X:0x099A61) set to the factory 440 Hz
 * default (P:0x083AB8). Returns 0, or -1 for a subtype outside 0..4. */
int gm2_pitch_subtype(gm2_emu *emu, unsigned subtype);

/* Apply the descriptor table at P:GM2_INIT_TABLE exactly as P:0x082AA2 does.
 * Returns the number of descriptors applied, -1 if the table never terminates. */
int gm2_apply_init_table(gm2_emu *emu);

/* Run the effect block's activation routine on a fresh emulator, started as the
 * device does: image loaded, initializer applied, `rate` installed, descriptor of
 * `algorithm` mirrored, X:(ui+1) = 1, A = algorithm, R0 = `ui`, R7 = 0x1000.
 * On success `level_addr` holds the destination word the block's level code left
 * at X:(ui+14) and `words` the three input, wet and dry words it staged at
 * X:(ui+25..27). Returns 0, or -1 with a message on stderr.
 *
 * The words read are the ones the level struct at X:(ui+6) publishes, which is
 * where the Gate & EQ and compressor blocks keep theirs; the mirror is the
 * algorithm's master descriptor, so this drives only blocks whose algorithm id is
 * also their activation id. The Tremolo/Panner object (Filter subtypes 5 and 6)
 * keeps its level struct at X:(ui+36) and stages it at X:0x098905..X:0x098907, and
 * its block needs gm2_filter_subtype before any updater runs, so a caller drives
 * that object's activation itself instead of through this function. */
int gm2_block_levels(const char *dir, unsigned rate, unsigned algorithm, uint32_t ui,
                     uint32_t activation, uint32_t *level_addr, uint32_t words[3]);

/* Writes `jsr target` + `stop` at GM2_STUB_ADDR and runs until the stop; OK only
 * if the callee returned. */
int gm2_call(gm2_emu *emu, uint32_t target, int32_t budget, uint32_t *cycles);

/* Enter at `pc` with no return address pushed and run until the stop; for
 * continuations that end by jumping out instead of returning. */
int gm2_run_until_stop(gm2_emu *emu, uint32_t pc, int32_t budget, uint32_t *cycles);

uint32_t gm2_get_reg(const gm2_emu *emu, unsigned reg);
void gm2_set_reg(gm2_emu *emu, unsigned reg, uint32_t value);
void gm2_reset_regs(gm2_emu *emu);

#endif /* GM2_EMU_H */
