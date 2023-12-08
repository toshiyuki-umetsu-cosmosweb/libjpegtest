/*
 * jdatadst.c
 *
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * This file is part of the Independent JPEG Group's software.
 * For conditions of distribution and use, see the accompanying README file.
 *
 * This file contains compression data destination routines for the case of
 * emitting JPEG data to a file (or any stdio stream).  While these routines
 * are sufficient for most applications, some will want to use a different
 * destination manager.
 * IMPORTANT: we assume that fwrite() will correctly transcribe an array of
 * JOCTETs into 8-bit-wide elements on external storage.  If char is wider
 * than 8 bits on your machine, you may need to do some tweaking.
 */
 /* this is not a core library module, so it doesn't define JPEG_INTERNALS */
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"
#include "jmemsys.h"

#include <string.h>

#define OUTPUT_BUF_SIZE  (4096)	/* choose an efficiently fwrite'able size */

/* Expanded data destination object for memory output */

typedef struct {
    struct jpeg_destination_mgr pub; /* public fields */

    unsigned char** outbuffer;	/* target buffer */
    size_t* outsize;
    unsigned char* newbuffer;	/* newly allocated buffer */
    size_t newbufsize; /* size of newbuffer. */
    JOCTET* buffer;		/* start of buffer */
    size_t bufsize; /* size of buffer. */
    bool allow_allocate_memory; /* allow alocate memory. */
} my_mem_destination_mgr;

typedef my_mem_destination_mgr* my_mem_dest_ptr;

METHODDEF(void)
init_mem_destination(j_compress_ptr cinfo)
{
    /* no work necessary here */
}

METHODDEF(bool)
empty_mem_output_buffer(j_compress_ptr cinfo)
{
    size_t nextsize;
    JOCTET* nextbuffer;
    my_mem_dest_ptr dest = (my_mem_dest_ptr)cinfo->dest;

    if (!dest->allow_allocate_memory) { // Disallow allocate memory?
        ERREXIT(cinfo, JERR_BAD_STATE);
        return false;
    }

    /* Try to allocate new buffer with double size */
    nextsize = dest->bufsize * 2;
    nextbuffer = (JOCTET*)(malloc(nextsize));
    if (nextbuffer == NULL) {
        ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 11);
        return false;
    }

    memcpy(nextbuffer, dest->buffer, dest->bufsize);

    if (dest->newbuffer != NULL) {
        free(dest->newbuffer);
        dest->newbuffer = NULL;
        dest->newbufsize = 0;
    }

    dest->newbuffer = nextbuffer;
    dest->newbufsize = nextsize;

    dest->pub.next_output_byte = nextbuffer + dest->bufsize;
    dest->pub.free_in_buffer = dest->bufsize;

    dest->buffer = nextbuffer;
    dest->bufsize = nextsize;

    return true;
}

METHODDEF(void)
term_mem_destination(j_compress_ptr cinfo)
{
    my_mem_dest_ptr dest = (my_mem_dest_ptr)cinfo->dest;

    *dest->outbuffer = dest->buffer; // Overwrite passed variable.
    *dest->outsize = dest->bufsize - dest->pub.free_in_buffer; // Overwrite passed variable.

    return;
}

/*
 * Prepare for output to a memory buffer.
 * The caller may supply an own initial buffer with appropriate size.
 * Otherwise, or when the actual data output exceeds the given size,
 * the library adapts the buffer size as necessary.
 * The standard library functions malloc/free are used for allocating
 * larger memory, so the buffer is available to the application after
 * finishing compression, and then the application is responsible for
 * freeing the requested memory.
 * Note:  An initial buffer supplied by the caller is expected to be
 * managed by the application.  The library does not free such buffer
 * when allocating a larger buffer.
 */
GLOBAL(bool)
jpeg_mem_dest(j_compress_ptr cinfo,
    unsigned char** outbuffer, size_t* outsize, bool allow_allocate_memory)
{
    size_t initial_bufsize;
    my_mem_dest_ptr dest;
    unsigned char* alloc_bufp;

    if ((outbuffer == NULL) || (outsize == NULL)) {	/* sanity check */
        ERREXIT(cinfo, JERR_BUFFER_SIZE);
        return false;
    }
    if (!allow_allocate_memory && ((*outbuffer == NULL) || (*outsize == 0))) {
        ERREXIT(cinfo, JERR_BAD_BUFFER_MODE);
        return false;
    }

    /* The destination object is made permanent so that multiple JPEG images
     * can be written to the same buffer without re-executing jpeg_mem_dest.
     */
    if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
        cinfo->dest = (struct jpeg_destination_mgr*)(*cinfo->mem->alloc_small)
            ((j_common_ptr)cinfo, JPOOL_PERMANENT, SIZEOF(my_mem_destination_mgr));
    }

    dest = (my_mem_dest_ptr)cinfo->dest;
    dest->pub.init_destination = init_mem_destination;
    dest->pub.empty_output_buffer = empty_mem_output_buffer;
    dest->pub.term_destination = term_mem_destination;
    dest->outbuffer = outbuffer;
    dest->outsize = outsize;
    dest->newbuffer = NULL;
    dest->allow_allocate_memory = allow_allocate_memory;

    if ((*outbuffer == NULL) || (*outsize == 0)) { // Specified buffer is none or empty?
        /* Allocate initial buffer */
        initial_bufsize = OUTPUT_BUF_SIZE;
        alloc_bufp = (unsigned char*)(malloc(initial_bufsize));
        if (alloc_bufp == NULL) {
            ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 10);
            return false;
        }

        dest->newbuffer = alloc_bufp;
        dest->newbufsize = initial_bufsize;
        *outbuffer = alloc_bufp;
        *outsize = initial_bufsize;
    }
    dest->buffer = *outbuffer;
    dest->bufsize = *outsize;
    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = dest->bufsize;

    return true;
}