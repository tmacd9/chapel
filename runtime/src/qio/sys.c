// squelch warning on Mac OS X
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#define _POSIX_C_SOURCE 20112L

#ifndef SIMPLE_TEST
#include "chplrt.h"
#endif

#include "sys.h"
#include "qbuffer.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/uio.h> // maybe need this for preadv/pwritev
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef __MTA__
#include <machine/param.h>
#ifndef IOV_MAX
#define IOV_MAX UIO_MAXIOV
#endif
#endif

// preadv/pwritev are available
// only on linux/glibc 2.10 or later
#ifdef __GLIBC__
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2,10)
// Glibc > 2.10 has preadv, pwritev
#define HAS_PREADV
#define HAS_PWRITEV
#endif
#endif
#endif

// Should be available in sys_xsi_strerror_r.c
extern int sys_xsi_strerror_r(int errnum, char* buf, size_t buflen);

void sys_init_sys_sockaddr(sys_sockaddr_t* addr)
{
  memset(addr, 0, sizeof(sys_sockaddr_t));
  addr->len = sizeof(sys_sockaddr_storage_t);
}

// -------------------  system call wrappers -----------------------------

size_t sys_page_size(void)
{
  long pagesize = -1;
  err_t err;
#ifdef _SC_PAGESIZE
  err = sys_sysconf(_SC_PAGESIZE, &pagesize);
  if( err == 0 && pagesize > 0 ) return pagesize;
#endif

  // Some systems offer PAGE_SIZE...
#ifdef PAGE_SIZE
  pagesize = PAGE_SIZE;
#endif

#ifdef __MTA__
#ifdef NBPG
  pagesize = NBPG;
#endif
#endif

  if( pagesize > 0 ) return pagesize;

  fprintf(stderr, "Fatal error: could not get page size\n");
  abort();
  return 0;
}


#if 0
err_t sys_fseeko(FILE* stream, off_t offset, int whence)
{
  int got;
  err_t err_out;

  got = fseeko(stream, offset, whence);
  if( got == 0 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_ftello(FILE* stream, off_t* offset_out)
{
  off_t got;
  err_t err_out;

  got = ftello(stream);
  if( got != (off_t) -1 ) {
    *offset_out = got;
    err_out = 0;
  } else {
    *offset_out = got;
    err_out = errno;
  }

  return err_out;
}

err_t sys_fopen(const char* path, const char* mode, FILE** file_out)
{
  FILE* got;
  err_t err_out;

  got = fopen(path, mode);
  if( got ) {
    *file_out = got;
    err_out = 0;
  } else {
    *file_out = got;
    err_out = errno;
  }

  return err_out;
}

err_t sys_fdopen(fd_t fd, const char* mode, FILE** file_out)
{
  FILE* got;
  err_t err_out;

  got = fdopen(fd, mode);
  if( got ) {
    *file_out = got;
    err_out = 0;
  } else {
    *file_out = got;
    err_out = errno;
  }

  return err_out;
}

err_t sys_fclose(FILE* fp)
{
  int got;
  err_t err_out;

  // might block with SO_LINGER
  STARTING_SLOW_SYSCALL;
  got = fclose(fp);
  if( got == 0 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_fread(void* ptr, size_t size, size_t nmemb, FILE* stream, size_t* num_read)
{
  size_t got;
  err_t err_out;

  // might block.
  STARTING_SLOW_SYSCALL;
  got = fread(ptr, size, nmemb, stream);
  if( got > 0 ) {
    *num_read = got;
    err_out = 0;
  } else {
    if( feof(stream) ) err_out = EEOF;
    else err_out = ferror(stream);
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream, size_t* num_written)
{
  size_t got;
  err_t err_out;

  // might block.
  STARTING_SLOW_SYSCALL;
  got = fwrite(ptr, size, nmemb, stream);
  if( got > 0 ) {
    *num_written = got;
    err_out = 0;
  } else {
    if( feof(stream) ) err_out = EEOF;
    else err_out = ferror(stream);
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_fflush(FILE* stream)
{
  int got;
  err_t err_out;

  // might block.
  STARTING_SLOW_SYSCALL;
  got = fflush(stream);
  if( got == 0 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}


err_t sys_feof(FILE* stream, int *iseof)
{
  *iseof = feof(stream);
  return 0;
}

err_t sys_ferror(FILE* stream)
{
  return ferror(stream);
}

#endif

err_t sys_posix_fadvise(fd_t fd, off_t offset, off_t len, int advice)
{
  int got;
  err_t err_out;

  got = 0;
#if (_XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L)
#ifdef POSIX_FADV_NORMAL
  got = posix_fadvise(fd, offset, len, advice);
#endif
#endif
  err_out = got;

  return err_out;
}

err_t sys_posix_madvise(void* addr, size_t len, int advice)
{
  int got;
  err_t err_out;

  got = 0;
#ifdef POSIX_MADV_NORMAL
  got = posix_madvise(addr, len, advice);
#endif
  err_out = got;

  return err_out;
}



static
const char* extended_errors[] = {
  "end of file",
  "short read or write",
  "bad format",
  "illegal multibyte sequence", // most systems already have EILSEQ but not all
  "overflow", // most systems already have EOVERFLOW but not all
  NULL
};


// allocates and returns an error string in *string_out
// which must be freed.
static
err_t sys_strerror_internal(err_t error, char** string_out, size_t extra_space)
{
  // normal errors are in normal places.
  // EAI_AGAIN... etc are at 10000 + num.
  int buf_sz = 248 + extra_space;
  char* buf = NULL;
  char* newbuf;
  const char* errmsg;
  int got;
  err_t err_out;

  err_out = 0;

  if( EXTEND_ERROR_OFFSET <= error
                          && error < EXTEND_ERROR_OFFSET+EXTEND_ERROR_NUM) {
    errmsg = extended_errors[error - EXTEND_ERROR_OFFSET];
    buf_sz = strlen(errmsg) + 1;
    buf = (char*) qio_malloc(buf_sz + extra_space);
    if( ! buf ) return ENOMEM;
    strcpy(buf, errmsg);
    *string_out = buf;
    return 0;
  }

  while( 1 ) {
    newbuf = (char*) qio_realloc(buf, buf_sz + extra_space);
    if( ! newbuf ) {
      qio_free(buf);
      return ENOMEM;
    }
    buf = newbuf;
    got = sys_xsi_strerror_r(error, buf, buf_sz);
    if( got == 0 ) break;
    if( got == -1 && errno != ERANGE ) {
      err_out = errno;
      break;
    }
    buf_sz *= 2; // try again with a bigger buffer.
  }

  // maybe it's a EAI/gai error, which we add GAI_ERROR_OFFSET to.
#ifdef HAS_GETADDRINFO
  if( got == -1 && err_out == EINVAL ) {
    const char* gai_str;
    int len;
    gai_str = gai_strerror(error - GAI_ERROR_OFFSET);

    if( ! gai_str ) {
      err_out = errno;
    } else {
      len = strlen(gai_str);
      if( len + 1 > buf_sz ) {
        newbuf = (char*) qio_realloc(buf, len + 1 + extra_space);
        if( ! newbuf ) {
          qio_free(buf);
          return ENOMEM;
        }
        buf = newbuf;
      }
      strcpy(buf, gai_str);
    }
  }
#endif

  *string_out = buf;
  return err_out;
}

err_t sys_strerror(err_t error, const char** string_out)
{
  return sys_strerror_internal(error, (char**) string_out, 0);
}

const char* sys_strerror_syserr_str(qioerr error, err_t* err_in_strerror)
{
  char* ret = NULL;
  err_t code = qio_err_to_int(error);
  const char* msg = qio_err_msg(error);
  size_t extra_space = 0;
  size_t start = 0;
  size_t msg_len = 0;

  if( msg ) {
    msg_len = strlen(msg);
    extra_space = msg_len + 3;
  }

  *err_in_strerror = sys_strerror_internal(code, &ret, extra_space);
  if( msg && ret ) {
    start = strlen(ret);
    ret[start] = ':';
    ret[start+1] = ' ';
    memcpy(&ret[start+2], msg, msg_len);
    ret[start+2+msg_len] = '\0';
  }
  return ret;
}

// returns an allocated string in string_out, which must be freed.
err_t sys_readlink(const char* path, const char** string_out)
{
  ssize_t got;
  char* buf = NULL;
  char* newbuf;
  int buf_sz = 248;
  err_t ret = EINVAL;

  while( 1 ) {
    newbuf = (char*) qio_realloc(buf, buf_sz);
    if( ! newbuf ) {
      qio_free(buf);
      return ENOMEM;
    }
    buf = newbuf;
    got = readlink(path, buf, buf_sz);
    if( got == -1 ) {
      qio_free(buf);
      *string_out = NULL;
      return errno;
    }
    if( got+1 < buf_sz ) {
      buf[got] = '\0';
      // OK!
      *string_out = buf;
      ret = 0;
      break;
    }
    // otherwise, buffer is too small.
    buf_sz *= 2;
  }

  return ret;
}

// Returns true if an environment variable called name was found,
// in which case *string_out will point to the string found in
// the process's environment block. Don't try to free it.
// Returns false if the name was not found in the environment
//  (ie getenv returned NULL).
int sys_getenv(const char* name, const char** string_out)
{
  char *buf;

  buf = getenv(name);
  if (buf==NULL) {
    return 0;
  } else {
    *string_out = buf;
    return 1;
  }
}

err_t sys_open(const char* pathname, int flags, mode_t mode, fd_t* fd_out)
{
  int got;
  err_t err_out;

  got = open(pathname, flags, mode);
  if( got != -1 ) {
    *fd_out = got;
    err_out = 0;
  } else {
    *fd_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_close(fd_t fd)
{
  int got;
  err_t err_out;

  // might block with SO_LINGER
  STARTING_SLOW_SYSCALL;
  got = close(fd);
  if( got == 0 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_lseek(fd_t fd, off_t offset, int whence, off_t* offset_out)
{
  off_t got;
  err_t err_out;

  got = lseek(fd, offset, whence);
  if( got != (off_t) -1 ) {
    *offset_out = got;
    err_out = 0; 
  } else {
    *offset_out = got;
    err_out = errno;
  }

  return err_out;
}

err_t sys_stat(const char* path, struct stat* buf)
{
  off_t got;
  err_t err_out;

  got = stat(path, buf);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_fstat(fd_t fd, struct stat* buf)
{
  off_t got;
  err_t err_out;

  got = fstat(fd, buf);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_lstat(const char* path, struct stat* buf)
{  off_t got;
  err_t err_out;

  got = lstat(path, buf);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_mkstemp(char* template_, fd_t* fd_out)
{
  int got;
  err_t err_out;

  got = mkstemp(template_);
  if( got != -1 ) {
    *fd_out = got;
    err_out = 0;
  } else {
    *fd_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_ftruncate(fd_t fd, off_t length)
{
  int got;
  err_t err_out;

  got = ftruncate(fd, length);
  if( got == 0 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_sysconf(int name, long* val_out)
{
  long got;
  err_t err_out;

  got = sysconf(name);
  if( got != -1 ) {
    *val_out = got;
    err_out = 0;
  } else {
    *val_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_posix_fallocate(fd_t fd, off_t offset, off_t len)
{
  err_t err_out;
  STARTING_SLOW_SYSCALL;
#ifdef __linux__
  err_out = posix_fallocate(fd, offset, len);
#else
  err_out = ENOSYS;
#endif
  DONE_SLOW_SYSCALL;
  return err_out;
}

err_t sys_mmap(void* addr, size_t length, int prot, int flags, fd_t fd, off_t offset, void** ret_out)
{
  void* got;
  err_t err_out;
  got = mmap(addr, length, prot, flags, fd, offset);
  if( got != MAP_FAILED ) {
    err_out = 0;
    *ret_out = got;
  } else {
    err_out = errno;
    *ret_out = NULL;
  }

  return err_out;
}

err_t sys_munmap(void* addr, size_t length)
{
  int rc;
  err_t err_out;
  rc = munmap(addr, length);
  if( rc ) {
    err_out = errno;
  } else {
    err_out = 0;
  }

  return err_out;
}


err_t sys_read(int fd, void* buf, size_t count, ssize_t* num_read_out)
{
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = read(fd, buf, count);
  if( got != -1 ) {
    *num_read_out = got;
    if( got == 0 && count != 0 ) err_out = EEOF;
    else err_out = 0;
  } else {
    *num_read_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_write(int fd, const void* buf, size_t count, ssize_t* num_written_out)
{
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = write(fd, buf, count);
  if( got != -1 ) {
    *num_written_out = got;
    err_out = 0;
  } else {
    *num_written_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_pread(int fd, void* buf, size_t count, off_t offset, ssize_t* num_read_out)
{
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = pread(fd, buf, count, offset);
  #ifdef __CYGWIN__
  if( got == -1 && errno == ENODATA ) got = 0;
  #endif
  if( got != -1 ) {
    *num_read_out = got;
    if( got == 0 && count != 0 ) err_out = EEOF;
    else err_out = 0;
  } else {
    *num_read_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_pwrite(int fd, const void* buf, size_t count, off_t offset, ssize_t* num_written_out)
{
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = pwrite(fd, buf, count, offset);
  if( got != -1 ) {
    *num_written_out = got;
    err_out = 0;
  } else {
    *num_written_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

// Return the total number of bytes in an IO vector
int64_t sys_iov_total_bytes(const struct iovec* iov, int iovcnt)
{
  int64_t tot = 0;
  int i;
  for( i = 0; i < iovcnt; i++ ) tot += iov[i].iov_len;
  return tot;
}

err_t sys_readv(fd_t fd, const struct iovec* iov, int iovcnt, ssize_t* num_read_out)
{
  ssize_t got;
  ssize_t got_total;
  err_t err_out;
  int i;
  int niovs = IOV_MAX;

  STARTING_SLOW_SYSCALL;

  err_out = 0;
  got_total = 0;
  for( i = 0; i < iovcnt; i += niovs ) {
    niovs = iovcnt - i;
    if( niovs > IOV_MAX ) niovs = IOV_MAX;

    // Some systems readv doesn't take a const struct iovec*, hence the cast
    got = readv(fd, (struct iovec*) &iov[i], niovs);
    if( got != -1 ) {
      got_total += got;
    } else {
      err_out = errno;
      break;
    }
    if( got != sys_iov_total_bytes(&iov[i], niovs) ) {
      break;
    }
  }

  if( err_out == 0 && got_total == 0 && sys_iov_total_bytes(iov, iovcnt) != 0 ) err_out = EEOF;

  *num_read_out = got_total;

  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_writev(fd_t fd, const struct iovec* iov, int iovcnt, ssize_t* num_written_out)
{
  ssize_t got;
  ssize_t got_total;
  err_t err_out;
  int i;
  int niovs = IOV_MAX;

  STARTING_SLOW_SYSCALL;

  err_out = 0;
  got_total = 0;
  for( i = 0; i < iovcnt; i += niovs ) {
    niovs = iovcnt - i;
    if( niovs > IOV_MAX ) niovs = IOV_MAX;

    // Some systems writev doesn't take a const struct iovec*, hence the cast
    got = writev(fd, (struct iovec*) &iov[i], niovs);
    if( got != -1 ) {
      got_total += got;
    } else {
      err_out = errno;
      break;
    }
    if( got != sys_iov_total_bytes(&iov[i], niovs) ) {
      break;
    }
  }

  *num_written_out = got_total;

  DONE_SLOW_SYSCALL;

  return err_out;
}

#ifdef HAS_PREADV
err_t sys_preadv(fd_t fd, const struct iovec* iov, int iovcnt, off_t seek_to_offset, ssize_t* num_read_out)
{
  ssize_t got;
  ssize_t got_total;
  err_t err_out;
  int i;
  int niovs = IOV_MAX;

  STARTING_SLOW_SYSCALL;

  err_out = 0;
  got_total = 0;
  for( i = 0; i < iovcnt; i += niovs ) {
    niovs = iovcnt - i;
    if( niovs > IOV_MAX ) niovs = IOV_MAX;

    // Some systems preadv doesn't take a const struct iovec*, hence the cast
    got = preadv(fd, (struct iovec*) &iov[i], niovs, seek_to_offset + got_total);
    #ifdef __CYGWIN__
    if( got == -1 && errno == ENODATA ) got = 0;
    #endif
    if( got != -1 ) {
      got_total += got;
    } else {
      err_out = errno;
      break;
    }
    if( got != sys_iov_total_bytes(&iov[i], niovs) ) {
      break;
    }
  }

  if( err_out == 0 && got_total == 0 && sys_iov_total_bytes(iov, iovcnt) != 0 ) err_out = EEOF;

  *num_read_out = got_total;

  DONE_SLOW_SYSCALL;

  return err_out;
}

#else

err_t sys_preadv(fd_t fd, const struct iovec* iov, int iovcnt, off_t seek_to_offset, ssize_t* num_read_out)
{
  ssize_t got;
  ssize_t got_total;
  err_t err_out;
  int i;

  STARTING_SLOW_SYSCALL;

  err_out = 0;
  got_total = 0;
  for( i = 0; i < iovcnt; i++ ) {
    got = pread(fd, iov[i].iov_base, iov[i].iov_len, seek_to_offset + got_total);
    #ifdef __CYGWIN__
    if( got == -1 && errno == ENODATA ) got = 0;
    #endif
    if( got != -1 ) {
      got_total += got;
    } else {
      err_out = errno;
      break;
    }
    if( got != iov[i].iov_len ) {
      break;
    }
  }

  if( err_out == 0 && got_total == 0 && sys_iov_total_bytes(iov, iovcnt) != 0 ) err_out = EEOF;

  *num_read_out = got_total;

  DONE_SLOW_SYSCALL;

  return err_out;
}

#endif
#ifdef HAS_PWRITEV
err_t sys_pwritev(fd_t fd, const struct iovec* iov, int iovcnt, off_t seek_to_offset, ssize_t* num_written_out)
{
  ssize_t got;
  ssize_t got_total;
  err_t err_out;
  int i;
  int niovs = IOV_MAX;

  STARTING_SLOW_SYSCALL;

  err_out = 0;
  got_total = 0;
  for( i = 0; i < iovcnt; i += niovs ) {
    niovs = iovcnt - i;
    if( niovs > IOV_MAX ) niovs = IOV_MAX;

    // Some systems pwritev doesn't take a const struct iovec*, hence the cast
    got = pwritev(fd, (struct iovec*) &iov[i], niovs, seek_to_offset + got_total);
    if( got != -1 ) {
      got_total += got;
    } else {
      err_out = errno;
      break;
    }
    if( got != sys_iov_total_bytes(&iov[i], niovs) ) {
      break;
    }
  }

  *num_written_out = got_total;

  DONE_SLOW_SYSCALL;

  return err_out;
}

#else

err_t sys_pwritev(fd_t fd, const struct iovec* iov, int iovcnt, off_t seek_to_offset, ssize_t* num_written_out)
{
  ssize_t got;
  ssize_t got_total;
  err_t err_out;
  int i;

  STARTING_SLOW_SYSCALL;

  err_out = 0;
  got_total = 0;
  for( i = 0; i < iovcnt; i++ ) {
    got = pwrite(fd, iov[i].iov_base, iov[i].iov_len, seek_to_offset + got_total);
    if( got != -1 ) {
      got_total += got;
    } else {
      err_out = errno;
      break;
    }
    if( got != iov[i].iov_len ) {
      break;
    }
  }

  *num_written_out = got_total;

  DONE_SLOW_SYSCALL;

  return err_out;
}


#endif

err_t sys_fsync(fd_t fd)
{
  int got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = fsync(fd);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_fcntl(fd_t fd, int cmd, int* ret_out)
{
  int got;
  err_t err_out;

  got = fcntl(fd, cmd);
  if( got != -1 ) {
    *ret_out = got;
    err_out = 0;
  } else {
    *ret_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_fcntl_long(fd_t fd, int cmd, long arg, int* ret_out)
{
  int got;
  err_t err_out;

  got = fcntl(fd, cmd, arg);
  if( got != -1 ) {
    *ret_out = got;
    err_out = 0;
  } else {
    *ret_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_fcntl_ptr(fd_t fd, int cmd, void* arg, int* ret_out)
{
  int got;
  err_t err_out;

  got = fcntl(fd, cmd, arg);
  if( got != -1 ) {
    *ret_out = got;
    err_out = 0;
  } else {
    *ret_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_dup(fd_t oldfd, fd_t* fd_out)
{
  int got;
  err_t err_out;

  got = dup(oldfd);
  if( got != -1 ) {
    *fd_out = got;
    err_out = 0;
  } else {
    *fd_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_dup2(int oldfd, int newfd, fd_t* fd_out)
{
  int got;
  err_t err_out;

  got = dup2(oldfd, newfd);
  if( got != -1 ) {
    *fd_out = got;
    err_out = 0;
  } else {
    *fd_out = -1;
    err_out = errno;
  }

  return err_out;
}

err_t sys_pipe(fd_t* read_fd_out, fd_t* write_fd_out)
{
  int got;
  err_t err_out;
  fd_t fds[2];

  fds[0] = fds[1] = -1;
  got = pipe(fds);
  if( got != -1 ) {
    *read_fd_out = fds[0];
    *write_fd_out = fds[1];
    err_out = 0;
  } else {
    *read_fd_out = -1;
    *write_fd_out = -1;
    err_out = errno;
  }

  return err_out;
}

/*
select
poll
epoll
*/

err_t sys_accept(fd_t sockfd, sys_sockaddr_t* addr_out, fd_t* fd_out)
{
  int got;
  err_t err_out;
  socklen_t addr_len = sizeof(sys_sockaddr_storage_t);

  STARTING_SLOW_SYSCALL;

  got = accept(sockfd, (struct sockaddr*) & addr_out->addr, &addr_len);
  if( got != -1 ) {
    if( addr_len > sizeof(sys_sockaddr_storage_t) ) {
      fprintf(stderr, "Warning: address truncated in sys_accept\n");
    }
    addr_out->len = addr_len;
    err_out = 0;
    *fd_out = got;
  } else {
    *fd_out = -1;
    err_out = errno;
  }

  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_bind(fd_t sockfd, const sys_sockaddr_t* addr)
{
  int got;
  err_t err_out;

  if( addr->len == 0 ) {
    return EINVAL;
  }

  got = bind(sockfd, (const struct sockaddr*) & addr->addr, addr->len);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_connect(fd_t sockfd, const sys_sockaddr_t* addr)
{
  int got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;

  got = connect(sockfd, (const struct sockaddr*) & addr->addr, addr->len);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  DONE_SLOW_SYSCALL;

  return err_out;
}

#ifdef HAS_GETADDRINFO

/* Commenting this out for the time being as it is not currently used and
   causes warnings in PrgEnv-gnu compiles due to static linking being the
   default and dynamic linking being required.

   Note that one solution to deal with the warning in the future would
   be to break this into its own .c/.o file so that users who use it
   get the warning which they might want while users who don't won't.
   Alternatively, we could look into squashing the error, but that
   seems more heavy-handed.

   -BLC */

//  err_t sys_getaddrinfo(const char* node, const char* service, 
//                       const sys_addrinfo_t* hints, sys_addrinfo_t ** res_out)
//  {
//    int got;
//    err_t err_out;
//  
//    STARTING_SLOW_SYSCALL;
//  
//    got = getaddrinfo(node, service, hints, res_out);
//    if( got == 0 ) {
//      err_out = 0;
//    } else {
//      err_out = got + GAI_ERROR_OFFSET;
//    }
//  
//    DONE_SLOW_SYSCALL;
//  
//    return err_out;
//  }

int sys_getaddrinfo_flags(sys_addrinfo_ptr_t a) {return a->ai_flags;}
int sys_getaddrinfo_family(sys_addrinfo_ptr_t a) {return a->ai_family;}
int sys_getaddrinfo_socktype(sys_addrinfo_ptr_t a) {return a->ai_socktype;}
int sys_getaddrinfo_protocol(sys_addrinfo_ptr_t a) {return a->ai_protocol;}
sys_sockaddr_t sys_getaddrinfo_addr(sys_addrinfo_ptr_t a) {
  sys_sockaddr_t ret;
  memcpy(&ret.addr, a->ai_addr, a->ai_addrlen);
  ret.len = a->ai_addrlen;
  return ret;
}
sys_addrinfo_ptr_t sys_getaddrinfo_next(sys_addrinfo_ptr_t a) {return a->ai_next;}

void sys_freeaddr_info(sys_addrinfo_ptr_t *p)
{
  freeaddrinfo(*p);
  *p = NULL;
}

err_t sys_getnameinfo(const sys_sockaddr_t* addr, char** host_out, char** serv_out, int flags)
{
  char* host_buf=0;
  char* new_host_buf;
  char* serv_buf=0;
  char* new_serv_buf;
  int host_buf_sz = NI_MAXHOST;
  int serv_buf_sz = NI_MAXSERV;
  int got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;

  while( 1 ) {
    new_host_buf = (char*) qio_realloc(host_buf, host_buf_sz);
    new_serv_buf = (char*) qio_realloc(serv_buf, serv_buf_sz);
    if( ! new_host_buf || ! new_serv_buf ) {
      qio_free(host_buf);
      qio_free(serv_buf);
      err_out = ENOMEM;
      goto error;
    }
    host_buf = new_host_buf;
    serv_buf = new_serv_buf;

    got = getnameinfo((const struct sockaddr*) & addr->addr, addr->len, 
                      host_buf, host_buf_sz,
                      serv_buf, serv_buf_sz,
                      flags);

#ifndef EAI_OVERFLOW
    break; // oddly enough... old Mac OS X does not have EAI_OVERFLOW.
#else
    if( got != EAI_OVERFLOW ) break;
    host_buf_sz *= 2;
    serv_buf_sz *= 2;
#endif
  }

  if( got == 0 ) {
    *host_out = NULL;
    *serv_out = NULL;
    err_out = 0;
  } else {
    *host_out = host_buf;
    *serv_out = serv_buf;
    if( got == EAI_SYSTEM ) err_out = errno;
    else err_out = GAI_ERROR_OFFSET + got;
  }

error:

  DONE_SLOW_SYSCALL;

  return err_out;
}

#endif

err_t sys_getpeername(fd_t sockfd, sys_sockaddr_t* addr)
{
  int got;
  err_t err_out;

  got = getpeername(sockfd, (struct sockaddr*) & addr->addr, & addr->len);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_getsockname(fd_t sockfd, sys_sockaddr_t* addr)
{
  int got;
  err_t err_out;

  got = getsockname(sockfd, (struct sockaddr*) & addr->addr, & addr->len);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}


err_t sys_getsockopt(fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen)
{
  int got;
  err_t err_out;

  got = getsockopt(sockfd, level, optname, optval, optlen);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_setsockopt(fd_t sockfd, int level, int optname, void* optval, socklen_t optlen)
{
  int got;
  err_t err_out;

  got = setsockopt(sockfd, level, optname, optval, optlen);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}


err_t sys_listen(fd_t sockfd, int backlog)
{
  int got;
  err_t err_out;

  got = listen(sockfd, backlog);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }

  return err_out;
}

err_t sys_recv(fd_t sockfd, void* buf, size_t len, int flags, ssize_t* num_recvd_out)
{
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = recv(sockfd, buf, len, flags);
  if( got != -1 ) {
    *num_recvd_out = got;
    err_out = 0;
  } else {
    *num_recvd_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_recvfrom(fd_t sockfd, void* buf, size_t len, int flags, sys_sockaddr_t* src_addr_out, ssize_t* num_recvd_out)
{
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = recvfrom(sockfd, buf, len, flags, (struct sockaddr*) &src_addr_out->addr, & src_addr_out->len);
  if( got != -1 ) {
    *num_recvd_out = got;
    err_out = 0;
  } else {
    *num_recvd_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_recvmsg(fd_t sockfd, struct msghdr *msg, int flags, ssize_t* num_recvd_out)

{
#ifndef __MTA__ 
  ssize_t got;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  got = recvmsg(sockfd, msg, flags);

  if( got != -1 ) {
    *num_recvd_out = got;
    err_out = 0;
  } else {
    *num_recvd_out = 0;
    err_out = errno;
  }

  DONE_SLOW_SYSCALL;

  return err_out;
#else
  return ENOSYS;
#endif
}

err_t sys_send(fd_t sockfd, const void* buf, int64_t len, int flags, ssize_t* num_sent_out)
{
  ssize_t sent;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  sent = send(sockfd, buf, len, flags);
  if( sent != -1 ) {
    *num_sent_out = sent;
    err_out = 0;
  } else {
    *num_sent_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_sendto(fd_t sockfd, const void* buf, int64_t len, int flags, const sys_sockaddr_t* dest_addr, ssize_t* num_sent_out)
{
  ssize_t sent;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  sent = sendto(sockfd, buf, len, flags, (const struct sockaddr*) &dest_addr->addr, dest_addr->len);
  if( sent != -1 ) {
    *num_sent_out = sent;
    err_out = 0;
  } else {
    *num_sent_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}

err_t sys_sendmsg(fd_t sockfd, const struct msghdr *msg, int flags, ssize_t* num_sent_out)
{
  ssize_t sent;
  err_t err_out;

  STARTING_SLOW_SYSCALL;
  sent = sendmsg(sockfd, msg, flags);
  if( sent != -1 ) {
    *num_sent_out = sent;
    err_out = 0;
  } else {
    *num_sent_out = 0;
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}


err_t sys_shutdown(fd_t sockfd, int how)
{
  int got;
  err_t err_out;

  // might block with SO_LINGER
  STARTING_SLOW_SYSCALL;
  got = shutdown(sockfd, how);
  if( got != -1 ) {
    err_out = 0;
  } else {
    err_out = errno;
  }
  DONE_SLOW_SYSCALL;

  return err_out;
}


err_t sys_socket(int domain, int type, int protocol, fd_t* sockfd_out)
{
  int got;
  err_t err_out;

  got = socket(domain, type, protocol);
  if( got != -1 ) {
    *sockfd_out = got;
    err_out = 0;
  } else {
    *sockfd_out = -1;
    err_out = errno;
  }

  return err_out;

}

err_t sys_socketpair(int domain, int type, int protocol, fd_t* sockfd_out_a, fd_t* sockfd_out_b)
{
  int got;
  err_t err_out;
  int sv[2];

  sv[0] = sv[1] = -1;
  got = socketpair(domain, type, protocol, sv);
  if( got != -1 ) {
    *sockfd_out_a = sv[0];
    *sockfd_out_b = sv[1];
    err_out = 0;
  } else {
    *sockfd_out_a = -1;
    *sockfd_out_b = -1;
    err_out = errno;
  }

  return err_out;
}


err_t sys_unlink(const char* path)
{
  int got;
  err_t err_out;

  got = unlink(path);
  if( got == 0 ) err_out = 0;
  else err_out = errno;

  return err_out;
}

err_t sys_getcwd(const char** path_out)
{
  int sz = 128;
  char* buf;
  char* got;
  err_t err = 0;

  buf = (char*) qio_malloc(sz);
  if( !buf ) return ENOMEM;
  while( 1 ) {
    got = getcwd(buf, sz);
    if( got != NULL ) break;
    else if( errno == ERANGE ) {
      // keep looping but with bigger buffer.
      sz = 2*sz;
      got = (char*) qio_realloc(buf, sz);
      if( ! got ) {
        qio_free(buf);
        return ENOMEM;
      }
    } else {
      // Other error, stop.
      err = errno;
    }
  }

  *path_out = buf;
  return err;
}