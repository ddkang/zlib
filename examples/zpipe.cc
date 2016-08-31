/* zpipe.c: example of proper use of zlib's inflate() and deflate()
   Not copyrighted -- provided to the public domain
   Version 1.4  11 December 2005  Mark Adler */

/* Version history:
   1.0  30 Oct 2004  First version
   1.1   8 Nov 2004  Add void casting for unused return values
                     Use switch statement for inflate() return values
   1.2   9 Nov 2004  Add assertions to document zlib guarantees
   1.3   6 Apr 2005  Remove incorrect assertion in inf()
   1.4  11 Dec 2005  Add hack to avoid MSDOS end-of-line conversions
                     Avoid some compiler warnings for input and output buffers
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "zlib.h"
#include "MemMgrAllocator.hh"
#include "Seccomp.hh"
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define CHUNK 16384
void * mem_mgr_opaque = NULL;

/* Compress from file source to file dest until EOF on source.
   def() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_STREAM_ERROR if an invalid compression
   level is supplied, Z_VERSION_ERROR if the version of zlib.h and the
   version of the library linked do not match, or Z_ERRNO if there is
   an error reading or writing the files. */
int def(int source, int dest, int level)
{
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate deflate state */
    strm.zalloc = Sirikata::MemMgrAllocatorMalloc;
    strm.zfree = Sirikata::MemMgrAllocatorFree;
    strm.opaque = mem_mgr_opaque;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return ret;
        if (!Sirikata::installStrictSyscallFilter(false)) {
          abort();
        }
    /* compress until end of file */
    do {
         auto status = read(source, in, CHUNK);
	 if (status < 0) {
	   if (errno == EINTR) {
	     continue;
	   } else {
	     return Z_STREAM_ERROR;
	   }
	 }
        strm.avail_in = status;

        flush = status == 0 ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            have = CHUNK - strm.avail_out;
	    const unsigned char * tmp =out;
	    while (have > 0) {
	      auto status = write(dest, tmp, have);
              if (status == 0) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
              } else if (status < 0) {
	      if (errno == EINTR) {
	       continue;
	      } else {
		deflateEnd(&strm);
		return Z_ERRNO;
	      }
	    }
	    tmp += status;
             have -= status;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input will be used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    (void)deflateEnd(&strm);
    return Z_OK;
}

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int inf(int source, int dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Sirikata::MemMgrAllocatorMalloc;
    strm.zfree = Sirikata::MemMgrAllocatorFree;
    strm.opaque = mem_mgr_opaque;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;
    if (!Sirikata::installStrictSyscallFilter(false)) {
      abort();
    }

    /* decompress until deflate stream ends or end of file */
    do {
        ssize_t status = read(source, in, CHUNK);
	if (status < 0) {
	  if (errno == EINTR) {
	    continue;
	  }
	  (void)inflateEnd(&strm);
	  return Z_ERRNO;
	}
        strm.avail_in = status;
	  
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;
	    const unsigned char *tmp = out;
	    do {
	      ssize_t status = write(dest, tmp, have);
	      if (status <0) {
		if (errno == EINTR) {
		  continue;
		} else {
		  (void)inflateEnd(&strm);
		  return Z_ERRNO;		  
		}
	      } else if (status == 0) {
		  (void)inflateEnd(&strm);
		  return Z_ERRNO;		  
	      } else {
		tmp += status;
		have -= status;
	      }
            } while(have > 0);
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

/* report a zlib or i/o error */
void zerr(int ret)
{
    fputs("zpipe: ", stderr);
    switch (ret) {
    case Z_ERRNO:
        if (ferror(stdin))
            fputs("error reading stdin\n", stderr);
        if (ferror(stdout))
            fputs("error writing stdout\n", stderr);
        break;
    case Z_STREAM_ERROR:
        fputs("invalid compression level\n", stderr);
        break;
    case Z_DATA_ERROR:
        fputs("invalid or incomplete deflate data\n", stderr);
        break;
    case Z_MEM_ERROR:
        fputs("out of memory\n", stderr);
        break;
    case Z_VERSION_ERROR:
        fputs("zlib version mismatch!\n", stderr);
    }
}

/* compress or decompress from stdin to stdout */
int main(int argc, char **argv)
{
    int ret;
    mem_mgr_opaque = Sirikata::MemMgrAllocatorInit(16 * 1024 * 1024, 0, 0, 16, false);
    /* avoid end-of-line conversions */
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    int c, quality = 9;
    bool decomp = false;
    while ((c = getopt (argc, argv, "hdq:")) != -1) {
        switch(c) {
            case 'd':
                decomp = true;
                break;
            case 'q':
                quality = std::stoi(std::string(optarg));
                break;
            case '?':
                if (optopt == 'q')
                    fputs("quality argument requires parameter", stderr);
                break;
            case 'h':
            default:
                fputs("zpipe usage: zpipe [-q [1-9]] [-d] < source > dest", stderr);
                return 1;
        }
    }
    if (quality > 9 || quality < 1) {
        fputs("zpipe usage: zpipe [-q [1-9]] [-d] < source > dest", stderr);
        return 1;
    }

    /* do compression if not decompressing */
    if (!decomp) {
        ret = def(0, 1, quality);
        if (ret != Z_OK)
          zerr(ret);
        syscall(60, ret);
        return ret;
    } else { /* otherwise decompress */
        ret = inf(0, 1);
        if (ret != Z_OK)
            zerr(ret);
        syscall(60, ret);
    }
}
