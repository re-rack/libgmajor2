/* Every lookup table gm2_tables.c keeps, read from the application image at the
 * addresses gm2_tables.h names: the words the initializer loads as they stand
 * after boot, and the tables the firmware builds while it runs, each read right
 * after the routine that builds it.
 *
 * P:0x093820 fills X:0x1000..0x15FF, so the Tri-Chorus Asymmetric pass overwrites
 * the Normal tables and each pass is read before the next one runs.
 */
#include "firmware_common.h"
#include "gm2_emu.h"
#include "gm2_fixture.h"
#include "gm2_record.h"

#include <dsp56300.h>

#include <stdio.h>

#define PITCH_UI 0x0987EAu /* the Pitch block's UI object */
#define MOD_UI 0x098C7Fu   /* the Modulation block's UI object */
#define MOD_SUBTYPE (MOD_UI + 2u)
#define MOD_SKIP (MOD_UI + 4u)
#define PITCH_RESET 0x082B79u   /* clears the pitch state, writes the detector biquad */
#define PITCH_UPDATER 0x090937u /* writes the exp2 polynomial when X:(UI+1) != 0 */
#define BOOT_SINE 0x083A02u     /* fills Y:0x0100 with sin(2 pi k / 256) */
#define WAVE_GENERATOR 0x093820u /* fills the six voice tables at X:0x1000 */
#define CALL_BUDGET 8000000

/* X:(MOD_UI+2) selects the phase offsets P:0x093820 builds from. */
enum { MOD_WAVE_PLAIN = 0, MOD_WAVE_TRICHORUS_NORM = 4, MOD_WAVE_TRICHORUS_ASYM = 5 };

typedef struct {
  const char *name;
  enum gm2_space space;
  uint32_t addr;
  uint32_t words;
} table_source;

static const table_source k_loaded[] = {
    {"gm2_log2_table", GM2_X, 0x0009E8u, 33u},
    {"gm2_exp2_table", GM2_X, 0x000A09u, 33u},
    {"gm2_phaser_range_table", GM2_X, 0x098ACAu, 36u},
    {"gm2_univibe_ap_table", GM2_X, 0x098BB8u, 16u},
    {"gm2_moddelay_pharev_table", GM2_Y, 0x000030u, 15u},
    {"gm2_moddelay_voice_gains[0..2]", GM2_X, 0x098D37u, 18u},
    /* P:0x093884 reads the two through-zero rows out of subtype order: X:0x098D49
     * holds TZF Positive and X:0x098D4F TZF Negative. */
    {"gm2_moddelay_voice_gains[3]", GM2_X, 0x098D4Fu, 6u},
    {"gm2_moddelay_voice_gains[4]", GM2_X, 0x098D49u, 6u},
    {"gm2_moddelay_depth_norm_table", GM2_X, 0x098D55u, 28u},
    {"gm2_moddelay_depth_asym_table", GM2_X, 0x098D71u, 28u},
    {"gm2_moddelay_depth_tzf_table", GM2_X, 0x098D8Du, 21u},
    {"gm2_moddelay_width_norm_table", GM2_X, 0x098DA2u, 21u},
    {"gm2_moddelay_width_asym_table", GM2_X, 0x098DB7u, 21u},
    {"gm2_pitch_scale_table", GM2_X, 0x09A254u, 144u},
    {"gm2_note_beats", GM2_X, 0x000AD4u, 17u},
    {"gm2_note_divisor", GM2_X, 0x000AE5u, 17u},
    /* X:0x098DF3, the Ignore entry, is the last Modulation subtype descriptor
     * table pointer and X:0x098DF4 is the delay engine swap flag, so the factors
     * start at Tempo 1. */
    {"gm2_delay_note_table[1..16]", GM2_X, 0x098DF5u, 32u},
    {"gm2_ratio_table", GM2_X, 0x098EEBu, 32u},
    {"gm2_time_table", GM2_X, 0x0998D8u, 54u},
    {"gm2_biquad_q_table", GM2_X, 0x000A5Cu, 66u},
    {"gm2_reverb_decay_table", GM2_Y, 0x000952u, 513u},
    {"gm2_reverb_early_l_table", GM2_X, 0x09904Cu, 120u},
    {"gm2_reverb_early_r_table", GM2_X, 0x0990C4u, 120u},
    {"gm2_reverb_size_table", GM2_X, 0x09913Cu, 135u},
    {"gm2_reverb_color_biquad_table", GM2_X, 0x0991C3u, 216u},
    {"gm2_reverb_locol_table", GM2_X, 0x09929Bu, 14u},
    {"gm2_reverb_hicol_table", GM2_X, 0x0992A9u, 28u},
    {"gm2_reverb_tank_lengths", GM2_X, 0x0992C9u, 14u},
    {"gm2_reverb_tank_signs", GM2_X, 0x0992DBu, 14u},
};

static const table_source k_pitch[] = {
    {"gm2_pitch_det_biquad", GM2_Y, 0x000279u, 5u},
    {"gm2_pitch_log2_poly", GM2_X, 0x000440u, 6u},
    {"gm2_pitch_exp2_poly", GM2_X, 0x00041Fu, 4u},
};

static const table_source k_sine[] = {
    {"gm2_sine_table", GM2_Y, 0x000100u, 256u},
};

static const table_source k_plain_sine[] = {
    {"gm2_lfo_sine_table", GM2_X, 0x001000u, 256u},
};

static const table_source k_trichorus_norm[] = {
    {"gm2_trichorus_norm_lfo_table", GM2_X, 0x001100u, 1280u},
};

static const table_source k_trichorus_asym[] = {
    {"gm2_trichorus_asym_lfo_table[0]", GM2_X, 0x001100u, 256u},
    {"gm2_trichorus_asym_lfo_set[0]", GM2_X, 0x001200u, 256u},
    {"gm2_trichorus_asym_lfo_set[1]", GM2_X, 0x001300u, 256u},
    {"gm2_trichorus_asym_lfo_table[1]", GM2_X, 0x001400u, 256u},
    {"gm2_trichorus_asym_lfo_set[2]", GM2_X, 0x001500u, 256u},
};

#define TABLES(entries) (sizeof entries / sizeof entries[0])

static int call(gm2_emu *emu, uint32_t entry) {
  if (gm2_call(emu, entry, CALL_BUDGET, NULL) != GM2_CALL_OK) {
    fprintf(stderr, "tables: P:0x%06X never returned\n", entry);
    return -1;
  }
  return 0;
}

/* The pitch block's reset and its first update, in the order the device runs
 * them: the reset clears X:0x425..0x5A9, the log2 polynomial among them. */
static int pitch_tables(gm2_emu *emu) {
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  if (call(emu, PITCH_RESET) != 0) {
    return -1;
  }
  gm2_write(emu, GM2_X, PITCH_UI + 1u, 1u);
  gm2_set_reg(emu, DSP56300_REG_A2, 0);
  gm2_set_reg(emu, DSP56300_REG_A1, 0);
  gm2_set_reg(emu, DSP56300_REG_A0, 0);
  gm2_set_reg(emu, DSP56300_REG_R0, PITCH_UI);
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  return call(emu, PITCH_UPDATER);
}

static int boot_sine(gm2_emu *emu) {
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  return call(emu, BOOT_SINE);
}

/* The Modulation block's waveform generator, entered through its UI object the
 * way its activation calls it. */
static int wave_tables(gm2_emu *emu, unsigned subtype) {
  gm2_write(emu, GM2_X, MOD_SUBTYPE, subtype);
  gm2_write(emu, GM2_X, MOD_SKIP, 0u);
  gm2_set_reg(emu, DSP56300_REG_R0, MOD_UI);
  gm2_set_reg(emu, DSP56300_REG_R7, GM2_DEVICE_STACK);
  return call(emu, WAVE_GENERATOR);
}

/* Reads one group into the fixture: its words, then the index that says where
 * they are. */
static int record_tables(gm2_fixture_writer *writer, const gm2_emu *emu, const table_source *source,
                         unsigned count, uint32_t *first) {
  for (unsigned i = 0; i < count; i++) {
    const table_source *table = &source[i];
    gm2_fixture_table index;
    unsigned char record[GM2_FIX_TABLE_WORDS_SIZE];
    index.first = *first;
    index.count = table->words;
    snprintf(index.name, sizeof index.name, "%s", table->name);
    for (uint32_t word = 0; word < table->words; word++) {
      gm2_fixture_put_word(record, gm2_sign24(gm2_read(emu, table->space, table->addr + word)));
      if (gm2_fixture_writer_add(writer, GM2_FIX_TABLE_WORDS, sizeof record, record) != 0) {
        return -1;
      }
      (*first)++;
    }
    unsigned char bytes[GM2_FIX_TABLE_INDEX_SIZE];
    gm2_fixture_put_table(bytes, &index);
    if (gm2_fixture_writer_add(writer, GM2_FIX_TABLE_INDEX, sizeof bytes, bytes) != 0) {
      return -1;
    }
  }
  return 0;
}

int gm2_record_tables(const char *path) {
  gm2_emu *emu = gm2_firmware_boot("tables");
  if (emu == NULL) {
    return -1;
  }
  gm2_fixture_writer writer;
  if (gm2_fixture_writer_open(&writer, path) != 0) {
    gm2_destroy(emu);
    return -1;
  }
  uint32_t first = 0;
  int status = record_tables(&writer, emu, k_loaded, TABLES(k_loaded), &first);
  if (status == 0) {
    status = pitch_tables(emu);
  }
  if (status == 0) {
    status = record_tables(&writer, emu, k_pitch, TABLES(k_pitch), &first);
  }
  if (status == 0) {
    status = boot_sine(emu);
  }
  if (status == 0) {
    status = record_tables(&writer, emu, k_sine, TABLES(k_sine), &first);
  }
  if (status == 0) {
    status = wave_tables(emu, MOD_WAVE_PLAIN);
  }
  if (status == 0) {
    status = record_tables(&writer, emu, k_plain_sine, TABLES(k_plain_sine), &first);
  }
  if (status == 0) {
    status = wave_tables(emu, MOD_WAVE_TRICHORUS_NORM);
  }
  if (status == 0) {
    status = record_tables(&writer, emu, k_trichorus_norm, TABLES(k_trichorus_norm), &first);
  }
  if (status == 0) {
    status = wave_tables(emu, MOD_WAVE_TRICHORUS_ASYM);
  }
  if (status == 0) {
    status = record_tables(&writer, emu, k_trichorus_asym, TABLES(k_trichorus_asym), &first);
  }
  gm2_destroy(emu);
  if (status != 0) {
    gm2_fixture_writer_abort(&writer);
    return -1;
  }
  if (gm2_fixture_writer_close(&writer) != 0) {
    fprintf(stderr, "tables: cannot write %s\n", path);
    return -1;
  }
  uint32_t tables = TABLES(k_loaded) + TABLES(k_pitch) + TABLES(k_sine) + TABLES(k_plain_sine) +
                    TABLES(k_trichorus_norm) + TABLES(k_trichorus_asym);
  printf("%-15s recorded %u tables, %u words to %s\n", "tables", tables, first, path);
  return 0;
}
