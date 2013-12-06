/*
 * Centaurean Density
 * http://www.libssc.net
 *
 * Copyright (c) 2013, Guillaume Voirin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Centaurean nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * 19/10/13 00:06
 */

#include "main_decode.h"

DENSITY_FORCE_INLINE DENSITY_DECODE_STATE density_decode_read_header(density_byte_buffer *restrict in, density_decode_state *restrict state) {
    if (in->position + sizeof(density_main_header) > in->size)
        return DENSITY_DECODE_STATE_STALL_ON_INPUT_BUFFER;

    state->totalRead += density_main_header_read(in, &state->header);

    state->process = DENSITY_DECODE_PROCESS_READ_BLOCKS;

    return DENSITY_DECODE_STATE_READY;
}

DENSITY_FORCE_INLINE DENSITY_DECODE_STATE density_decode_read_footer(density_byte_buffer *restrict in, density_decode_state *restrict state) {
    if (in->position + sizeof(density_main_footer) > in->size)
        return DENSITY_DECODE_STATE_STALL_ON_INPUT_BUFFER;

    state->totalRead += density_main_footer_read(in, &state->footer);

    state->process = DENSITY_DECODE_PROCESS_FINISHED;

    return DENSITY_DECODE_STATE_READY;
}

DENSITY_FORCE_INLINE void density_decode_update_totals(density_byte_buffer *restrict in, density_byte_buffer *restrict out, density_decode_state *restrict state, const uint_fast64_t inPositionBefore, const uint_fast64_t outPositionBefore) {
    state->totalRead += in->position - inPositionBefore;
    state->totalWritten += out->position - outPositionBefore;
}

DENSITY_FORCE_INLINE DENSITY_DECODE_STATE density_decode_init(density_byte_buffer *in, density_byte_buffer *workBuffer, const uint_fast64_t workBufferSize, density_decode_state *restrict state) {
    DENSITY_DECODE_STATE decodeState;

    state->totalRead = 0;
    state->totalWritten = 0;

    if ((decodeState = density_decode_read_header(in, state)))
        return decodeState;

    switch (state->header.compressionMode) {
        case DENSITY_COMPRESSION_MODE_COPY:
            density_block_decode_init(&state->blockDecodeStateA, DENSITY_BLOCK_MODE_COPY, (DENSITY_BLOCK_TYPE) state->header.blockType, sizeof(density_main_footer), NULL, NULL, NULL, NULL);
            break;

        case DENSITY_COMPRESSION_MODE_CHAMELEON_ALGORITHM:
            density_block_decode_init(&state->blockDecodeStateA, DENSITY_BLOCK_MODE_KERNEL, (DENSITY_BLOCK_TYPE) state->header.blockType, sizeof(density_main_footer), malloc(sizeof(density_chameleon_decode_state)), (void *) density_chameleon_decode_init, (void *) density_chameleon_decode_process, (void *) density_chameleon_decode_finish);
            break;

        case DENSITY_COMPRESSION_MODE_MANDALA_ALGORITHM:
            break;

        default:
            return DENSITY_DECODE_STATE_ERROR;
    }

    state->workBuffer = workBuffer;
    state->workBufferData.memorySize = workBufferSize;

    return DENSITY_DECODE_STATE_READY;
}

DENSITY_FORCE_INLINE DENSITY_DECODE_STATE density_decode_process(density_byte_buffer *restrict in, density_byte_buffer *restrict out, density_decode_state *restrict state, const density_bool flush) {
    DENSITY_BLOCK_DECODE_STATE blockDecodeState;
    uint_fast64_t inPositionBefore;
    uint_fast64_t outPositionBefore;

    while (true) {
        inPositionBefore = in->position;
        outPositionBefore = out->position;

        switch (state->process) {
            case DENSITY_DECODE_PROCESS_READ_BLOCKS:
                blockDecodeState = density_block_decode_process(in, out, &state->blockDecodeStateA, flush);
                density_decode_update_totals(in, out, state, inPositionBefore, outPositionBefore);

                switch (blockDecodeState) {
                    case DENSITY_BLOCK_DECODE_STATE_READY:
                        state->process = DENSITY_DECODE_PROCESS_READ_FOOTER;
                        return DENSITY_DECODE_STATE_READY;

                    case DENSITY_BLOCK_DECODE_STATE_STALL_ON_OUTPUT_BUFFER:
                        return DENSITY_DECODE_STATE_STALL_ON_OUTPUT_BUFFER;

                    case DENSITY_BLOCK_DECODE_STATE_STALL_ON_INPUT_BUFFER:
                        return DENSITY_DECODE_STATE_STALL_ON_INPUT_BUFFER;

                    case DENSITY_BLOCK_DECODE_STATE_ERROR:
                        return DENSITY_DECODE_STATE_ERROR;
                }
                break;

            default:
                return DENSITY_DECODE_STATE_ERROR;
        }
    }
}

DENSITY_FORCE_INLINE DENSITY_DECODE_STATE density_decode_finish(density_byte_buffer *in, density_decode_state *restrict state) {
    if (state->process ^ DENSITY_DECODE_PROCESS_READ_FOOTER)
        return DENSITY_DECODE_STATE_ERROR;

    switch (state->header.compressionMode) {
        case DENSITY_COMPRESSION_MODE_MANDALA_ALGORITHM:
            density_block_decode_finish(&state->blockDecodeStateB);
            free(state->blockDecodeStateB.kernelDecodeState);

        case DENSITY_COMPRESSION_MODE_CHAMELEON_ALGORITHM:
            density_block_decode_finish(&state->blockDecodeStateA);
            free(state->blockDecodeStateA.kernelDecodeState);
            break;

        default:
            break;
    }

    return density_decode_read_footer(in, state);
}