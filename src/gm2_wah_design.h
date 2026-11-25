/* The setup the three wahs share: the Filter block's Range updater for subtypes
 * 0..2, P:0x092527.
 */
#ifndef GM2_WAH_DESIGN_H
#define GM2_WAH_DESIGN_H

#include <stdint.h>

/* Y:0x40A, the filter's frequency shift: 2 for a raw Range of 0 (Low), 1 for 1
 * (High). */
int32_t gm2_wah_design_shift(int range);

#endif /* GM2_WAH_DESIGN_H */
