/*
 * jdatasrc_mem.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains decompression data source routines for the case of
 * reading JPEG data from a file (or any stdio stream).  While these routines
 * are sufficient for most applications, some will want to use a different
 * source manager.
 * IMPORTANT: we assume that fread() will correctly transcribe an array of
 * JOCTETs from 8-bit-wide elements on external storage.  If char is wider
 * than 8 bits on your machine, you may need to do some tweaking.
 */

 /* this is not a core library module, so it doesn't define JPEG_INTERNALS */
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"
#include "jmemsys.h"

METHODDEF(void)
init_mem_source(j_decompress_ptr cinfo)
{
    /* no work necessary here */
}

METHODDEF(bool)
fill_mem_input_buffer(j_decompress_ptr cinfo)
{
    static const JOCTET mybuffer[4] = {
      (JOCTET)0xFF, (JOCTET)JPEG_EOI, 0, 0
    };

    /* The whole JPEG data is expected to reside in the supplied memory
     * buffer, so any request for more data beyond the given buffer size
     * is treated as an error.
     */
    WARNMS(cinfo, JWRN_JPEG_EOF);

    /* Insert a fake EOI marker */

    cinfo->src->next_input_byte = mybuffer;
    cinfo->src->bytes_in_buffer = 2;

    return true;
}

/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 *
 * Writers of suspendable-input applications must note that skip_input_data
 * is not granted the right to give a suspension return.  If the skip extends
 * beyond the data currently in the buffer, the buffer can be marked empty so
 * that the next read will cause a fill_input_buffer call that can suspend.
 * Arranging for additional bytes to be discarded before reloading the input
 * buffer is the application writer's problem.
 */

METHODDEF(void)
skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    struct jpeg_source_mgr* src = cinfo->src;
    size_t nbytes;

    /* Just a dumb implementation for now.  Could use fseek() except
     * it doesn't work on pipes.  Not clear that being smart is worth
     * any trouble anyway --- large skips are infrequent.
     */
    if (num_bytes > 0) {
        nbytes = (size_t)num_bytes;
        while (nbytes > src->bytes_in_buffer) {
            nbytes -= src->bytes_in_buffer;
            (void)(*src->fill_input_buffer) (cinfo);
            /* note we assume that fill_input_buffer will never return FALSE,
             * so suspension need not be handled.
             */
        }
        src->next_input_byte += nbytes;
        src->bytes_in_buffer -= nbytes;
    }
}

/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */
METHODDEF(void)
term_source(j_decompress_ptr cinfo)
{
    /* no work necessary here */
}

/*
 * Prepare for input from a supplied memory buffer.
 * The buffer must contain the whole JPEG data.
 */
GLOBAL(bool)
jpeg_mem_src(j_decompress_ptr cinfo,
    const unsigned char* inbuffer, size_t insize)
{
    struct jpeg_source_mgr* src;

    if ((inbuffer == NULL) || (insize == 0)) {	/* Treat empty input as fatal error */
        ERREXIT(cinfo, JERR_INPUT_EMPTY);
        return false;
    }

    /* The source object is made permanent so that a series of JPEG images
     * can be read from the same buffer by calling jpeg_mem_src only before
     * the first one.
     */
    if (cinfo->src == NULL) {	/* first time for this JPEG object? */
        cinfo->src = (struct jpeg_source_mgr*)(*cinfo->mem->alloc_small)
            ((j_common_ptr)cinfo, JPOOL_PERMANENT, SIZEOF(struct jpeg_source_mgr));
        if (cinfo->src == NULL) {
            ERREXIT(cinfo, JERR_OUT_OF_MEMORY);
            return false;
        }
    }

    src = cinfo->src;
    src->init_source = init_mem_source;
    src->fill_input_buffer = fill_mem_input_buffer;
    src->skip_input_data = skip_input_data;
    src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
    src->term_source = term_source;
    src->bytes_in_buffer = insize;
    src->next_input_byte = (const JOCTET*)inbuffer;

    return true;
}