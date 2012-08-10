#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#include <chpl-comm-printf-macros.h>
#include "chplcgfns.h"
#include "chpl-comm-launch.h"
#include "chpl-comm-locales.h"
#include "chpllaunch.h"
#include "chpl-mem.h"
#include "chpltypes.h"
#include "error.h"


static void chpl_launch_sanity_checks(const char* argv0) {
  // Do sanity checks just before launching.
  struct stat statBuf;

  // Make sure the _real binary exists
  // (this should be called after someone has called
  // chpl_compute_real_binary_name() )
  if (stat(chpl_get_real_binary_name(), &statBuf) != 0) {
    char errorMsg[256];
    sprintf(errorMsg, "unable to locate file: %s", chpl_get_real_binary_name());
    chpl_error(errorMsg, 0, 0);
  }
}

//
// Use this function to run short utility programs that will return less
//  than 1024 characters of output.  The program must not expect any input.
//  On success, returns the number of bytes read and the output of the
//  command in outbuf.  Returns -1 on failure.
//
int
chpl_run_utility1K(const char *command, char *const argv[], char *outbuf, int outbuflen) {
  const int buflen = 1024;
  int curlen;
  char buf[buflen];
  char *cur;
  int fdo[2], outfd;
  int fde[2], errfd;
  fd_set set;
  pid_t pid;
  int status;
  int rv, numRead;

  if (pipe(fdo) < 0) {
    sprintf(buf, "Unable to run '%s' (pipe failed): %s\n", command, strerror(errno));
    chpl_internal_error(buf);
  }
  if (pipe(fde) < 0) {
    sprintf(buf, "Unable to run '%s' (pipe failed): %s\n", command, strerror(errno));
    chpl_internal_error(buf);
  }

  pid = fork();
  switch (pid) {
  case 0: // child should exit on errors
    close(fdo[0]);
    if (fdo[1] != STDOUT_FILENO) {
      if (dup2(fdo[1], STDOUT_FILENO) != STDOUT_FILENO) {
        sprintf(buf, "Unable to run '%s' (dup2 failed): %s",
                command, strerror(errno));
        chpl_internal_error(buf);
      }
    }
    close(fde[0]);
    if (fde[1] != STDERR_FILENO) {
      if (dup2(fde[1], STDERR_FILENO) != STDERR_FILENO) {
        sprintf(buf, "Unable to run '%s' (dup2 failed): %s",
                command, strerror(errno));
        chpl_internal_error(buf);
      }
    }
    execvp(command, argv);
    // should only return on error
    sprintf(buf, "Unable to run '%s': %s",
                command, strerror(errno));
    chpl_internal_error(buf);
  case -1:
    sprintf(buf, "Unable to run '%s' (fork failed): %s",
            command, strerror(errno));
    chpl_warning(buf, 0, 0);
    return -1;
  default:
    outfd = fdo[0];
    errfd = fde[0];
    close(fdo[1]);
    close(fde[1]);
    numRead = 0;
    curlen = buflen > outbuflen ? outbuflen : buflen;
    cur = buf;
    while (numRead < buflen) {
      struct timeval tv = { 1, 0 };
      FD_ZERO(&set);
      FD_SET(outfd, &set);
      FD_SET(errfd, &set);
      select(errfd+1, &set, NULL, NULL, &tv);
      if (FD_ISSET(outfd, &set)) {
        rv = read(outfd, cur, buflen);
        if (rv == 0) {
          if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        } else if (rv > 0) {
          cur += rv;
          numRead += rv;
          curlen -= rv;
        } else {
          sprintf(buf, "Unable to run '%s' (read failed): %s",
                  command, strerror(errno));
          chpl_warning(buf, 0, 0);
          return -1;
        }
      }
      if (FD_ISSET(errfd, &set)) {
        rv = read(errfd, cur, buflen);
        if (rv == 0) {
          if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        } else if (rv > 0) {
          cur += rv;
          numRead += rv;
          curlen -= rv;
        } else {
          sprintf(buf, "Unable to run '%s' (read failed): %s",
                  command, strerror(errno));
          chpl_warning(buf, 0, 0);
          return -1;
        }
      }
    }

    if (numRead != 0) {
      if (strstr(buf, "internal error: ") == NULL) {
        memcpy(outbuf, buf, numRead);
      } else {
        // The utility program ran, but failed with an internal error
        // from child's branch above (dup2 or exevp)
        buf[numRead] = 0;
        chpl_warning(buf, 0, 0);
        return -1;
      }
    } else {
      sprintf(buf, "Unable to run '%s' (no bytes read)", command);
      chpl_warning(buf, 0, 0);
      return -1;
    }

    // NOTE: We don't do a waitpid() here, so the program may keep running.
    //  That is a bad program, and I'm not going to deal with it here.
  }
  return numRead;
}

//
// This function returns a NULL terminated argv list as required by
// chpl_launch_using_exec(), i.e., execve(3).
//
//     argc, argv: as passed to main()
//     largc, largv: launcher command and args
//
char** chpl_bundle_exec_args(int argc, char *const argv[],
                              int largc, char *const largv[]) {
  int len = argc+largc+1;
  char **newargv = chpl_mem_allocMany(len, sizeof(char*),
                                      CHPL_RT_MD_COMMAND_BUFFER, -1, "");
  if (!newargv) {
    chpl_internal_error("Could not allocate memory");
  }

  newargv[len-1] = NULL;

  if (largc > 0) {
    // launcher args
    memcpy(newargv, largv, largc*sizeof(char *));
  }
  if (argc > 0) {
    // binary
    chpl_compute_real_binary_name(argv[0]);
    newargv[largc] = (char *) chpl_get_real_binary_name();
    if (argc > 1) {
      // other args (skip binary name)
      memcpy(newargv+largc+1, argv+1, (argc-1)*sizeof(char *));
    }
  }

  return newargv;
}

//
// This function calls execvp(3)
//
int chpl_launch_using_exec(const char* command, char * const argv1[], const char* argv0) {
  if (verbosity > 1) {
    char * const *arg;
    printf("%s ", command);
    fflush(stdout);
    for (arg = argv1+1; *arg; arg++) {
      printf(" %s", *arg);
      fflush(stdout);
    }
    printf("\n");
  }
  chpl_launch_sanity_checks(argv0);

  execvp(command, argv1);
  {
    char msg[256];
    sprintf(msg, "execvp() failed: %s", strerror(errno));
    chpl_internal_error(msg);
  }
  return -1;
}

int chpl_launch_using_fork_exec(const char* command, char * const argv1[], const char* argv0) {
  int status;
  pid_t pid = fork();
  switch (pid) {
  case 0:
    chpl_launch_using_exec(command, argv1, argv0);
    // should not return
  case -1:
  {
    char msg[256];
    sprintf(msg, "fork() failed: %s", strerror(errno));
    chpl_internal_error(msg);
  }
  default:
    {
      if (waitpid(pid, &status, 0) != pid) {
        char msg[256];
        sprintf(msg, "waitpid() failed: %s", strerror(errno));
        chpl_internal_error(msg);
      }
    }
  }
  return WEXITSTATUS(status);
}

int chpl_launch_using_system(char* command, char* argv0) {
  if (verbosity > 1) {
    printf("%s\n", command);
  }
  chpl_launch_sanity_checks(argv0);
  return system(command);
}


int handleNonstandardArg(int* argc, char* argv[], int argNum, 
                         int32_t lineno, chpl_string filename) {
  int numHandled = chpl_launch_handle_arg(*argc, argv, argNum, 
                                          lineno, filename);
  if (numHandled == 0) {
    char* message = chpl_glom_strings(3, "Unexpected flag:  \"", argv[argNum], 
                                      "\"");
    chpl_error(message, lineno, filename);
    return 0;
  } else {
    int i;
    for (i=argNum+numHandled; i<*argc; i++) {
      argv[i-numHandled] = argv[i];
    }
    *argc -= numHandled;
    return -1;  // back the cursor up in order to re-parse this arg
  }
}


void printAdditionalHelp(void) {
  chpl_launch_print_help();
}

// These are defined in the config.c file, which is built
// on-the-fly in runtime/etc/Makefile.launcher.
extern const char launcher_real_suffix[];
extern const char launcher_exe_suffix[];    // May be the empty string.

#define BIN_NAME_SIZE 256
static char chpl_real_binary_name[BIN_NAME_SIZE];

void chpl_compute_real_binary_name(const char* argv0) {

  char* cursor = chpl_real_binary_name;
  int exe_length = strlen(launcher_exe_suffix);
  int length;
  const char* real_suffix = getenv("CHPL_LAUNCHER_SUFFIX");
  
  if (NULL == real_suffix) {
    real_suffix = launcher_real_suffix;
  }
  
  length = strlen(argv0);
  if (length + strlen(launcher_real_suffix) >= BIN_NAME_SIZE)
    chpl_internal_error("Real executable name is too long.");

  // See if the launcher name contains the exe_suffix
  if (exe_length > 0 &&
      !strncmp(argv0 + length - exe_length, launcher_exe_suffix, exe_length))
    // We matched the exe suffix, so remove it before adding the real suffix.
    length -= exe_length;

  // Copy the filename sans exe suffix.
  strncpy(cursor, argv0, length);
  cursor += length;
  strcpy(cursor, launcher_real_suffix);
}

const char* chpl_get_real_binary_name(void) {
  return &chpl_real_binary_name[0];
}

int main(int argc, char* argv[]) {
  //
  // This is a user invocation, so parse the arguments to determine
  // the number of locales.
  //
  int32_t execNumLocales;
  CreateConfigVarTable();
  parseArgs(&argc, argv);

  execNumLocales = getArgNumLocales();

  //
  // If the user did not specify a number of locales let the
  // comm layer decide how many to use (or flag an error)
  //
  if (execNumLocales == 0) {
    execNumLocales = chpl_comm_default_num_locales();
  }
  //
  // Before proceeding, allow the comm layer to verify that the
  // number of locales is reasonable
  //
  chpl_comm_verify_num_locales(execNumLocales);

  //
  // Let the comm layer do any last-minute pre-launch activities it
  // needs to.
  //
  CHPL_COMM_PRELAUNCH();

  //
  // Launch the program
  // This may not return (e.g., if calling chpl_launch_using_exec())
  //
  return chpl_launch(argc, argv, execNumLocales);
}
