#include <stdio.h>
#include <string.h>
#include "files.h"
#include "misc.h"
#include "pass.h"
#include "stringutil.h"

#define FIRST NULL
#define LAST NULL
#define RUN(x) new x()
#include "passlist.h"


#define REGISTER(pass) \
  if (strcmp(passname, #pass) == 0) { \
    return new pass(); \
  }


static Pass* stringToPass(char* passname) {
#include "passlist.cpp"  
  INT_FATAL("Couldn't find a pass named %s", passname);
  return NULL;
}


static void runPass(Pass* pass, Stmt* program) {
  pass->run(program);
}


static void parsePassFile(char* passfilename, Stmt* program) {
  FILE* passfile = openInputFile(passfilename);
  char passname[80];
  int readword;
  bool done;
  do {
    readword = fscanf(passfile, "%s", passname);
  } while (readword == 1 && strcmp(passname, "FIRST,") != 0);
  do {
    readword = fscanf(passfile, "%s", passname);
    done = strcmp(passname, "LAST") == 0;
    if (!done) {
      if (strncmp(passname, "RUN(", 4) != 0) {
	fail("ill-formed passname: %s", passname);
      }
      char* passnameStart = passname + 4; // 4 == strlen("RUN(")
      int passnameLen = strlen(passnameStart);
      passnameStart[passnameLen-2] = '\0';
      Pass* pass = stringToPass(passnameStart);
      runPass(pass, program);
    }
  } while (readword == 1 && !done);
  closeInputFile(passfile);
}


void runPasses(char* passfilename, Stmt* program) {
  if (strcmp(passfilename, "") == 0) {
    Pass** pass = passlist+1;  // skip over FIRST
    
    while ((*pass) != NULL) {
      runPass(*pass, program);
      
      pass++;
    }
  } else {
    parsePassFile(passfilename, program);
  }
}


void DummyPass::run(Stmt* program) {
  fprintf(stdout, "Running dummy pass\n");
}
