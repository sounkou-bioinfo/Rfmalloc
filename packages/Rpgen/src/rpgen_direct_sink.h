#ifndef RPGEN_DIRECT_SINK_H
#define RPGEN_DIRECT_SINK_H

/*
 * Terminal record hook for PLINK 2's single-threaded PGEN writer.
 *
 * rpgen_ingest() activates this hook before entering an upstream importer.
 * The importer still owns parsing and format semantics, but its decoded
 * genovec, phase, and dosage tracks go to Rfmalloc instead of being encoded
 * as a temporary PGEN and decoded again.  The hook is process-global for the
 * same reason PLINK 2's bigstack is process-global: import runs on R's
 * evaluator thread and is not reentrant.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rpgen_direct_sink_active(void);
int rpgen_direct_sink_open(uint32_t variant_ct, uint32_t sample_ct);
int rpgen_direct_sink_append(uint32_t variant_idx, uint32_t sample_ct,
    const uintptr_t *genovec, int phase_supplied,
    const uintptr_t *phasepresent, const uintptr_t *phaseinfo,
    const uintptr_t *dosage_present, const uint16_t *dosage_main,
    uint32_t dosage_ct);
int rpgen_direct_sink_append_difflist(uint32_t variant_idx,
    uint32_t sample_ct, const uintptr_t *raregeno,
    const uint32_t *difflist_sample_ids, uint32_t difflist_common_geno,
    uint32_t difflist_len);
int rpgen_direct_sink_writer_finish(uint32_t variant_ct,
    uint32_t emitted_variant_ct);
const char *rpgen_direct_sink_error(void);

#ifdef __cplusplus
}
#endif

#endif /* RPGEN_DIRECT_SINK_H */
