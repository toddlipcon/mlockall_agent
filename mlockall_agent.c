// Copyright (c) 2011, Cloudera, inc. All rights reserved.

#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "jvmti.h"

// The environment variable containing the user that will be setuid to
#define USER_ENV "MLOCKALL_TARGET_USER"

#define PREFIX "mlockall_agent: "
#define LOG(fmt, ...) { fprintf(stderr, PREFIX fmt, ## __VA_ARGS__); }

typedef struct opts {
  char *setuid_user;
} opts_t;

static int parse_options(char *options, opts_t *parsed) {
  char *dup = strdup(options);
  char *save = NULL, *save2 = NULL;
  char *tok;
  int ret = 0;
  while ((tok = strtok_r(options, ",", &save)) != NULL) {
    options = NULL;
    char *pair = strdup(tok);

    char *key = strtok_r(pair, "=", &save2);
    if (key != NULL) key = strdup(key);

    char *val = strtok_r(NULL, "=", &save2);
    if (val != NULL) val = strdup(val);

    if (strcmp(key, "user") == 0) {
      parsed->setuid_user = strdup(val);
    } else {
      LOG("Unknown agent parameter '%s'\n", key);
      ret = 1;
    }

    if (key) free(key);
    if (val) free(val);
    free(pair);
  }
  return ret;
}

static void warn_unless_root() {
  if (geteuid() != 0) {
    LOG("(this may be because java was not run as root!)\n");
  }
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *init_str, void *reserved) {
  opts_t opts;
  if (parse_options(init_str, &opts)) {
    return 1;
  }

  // Check that the target user for setuid is specified.
  if (opts.setuid_user == NULL) {
    LOG("Unable to setuid: specify a target username as the agent option user=<username>");
    return 1;
  }
  
  // Check that this user exists.
  struct passwd *pwd = getpwnam(opts.setuid_user);
  if (!pwd) {
    LOG("Unable to setuid: could not find user %s\n", opts.setuid_user);
    return 1;
  }

  // Boost the mlock limit up to infinity
  struct rlimit lim;
  lim.rlim_max = RLIM_INFINITY;
  lim.rlim_cur = lim.rlim_max;
  if (setrlimit(RLIMIT_MEMLOCK, &lim)) {
    perror(PREFIX "Unable to boost memlock resource limit");
    warn_unless_root();
    return 1;
  }

  // Actually lock our memory, including future allocations.
  if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
    perror(PREFIX "Unable to lock memory.");
    warn_unless_root();
    return 1;
  }
 
  // Drop down to the user's supplemental group list
  if (initgroups(opts.setuid_user, pwd->pw_gid)) {
    perror(PREFIX "Unable to initgroups");
    warn_unless_root();
    return 1;
  }
 
  // And primary group ID
  if (setgid(pwd->pw_gid)) {
    perror(PREFIX "Unable to setgid");
    warn_unless_root();
    return 1;
  }

  // And user ID
  if (setuid(pwd->pw_uid)) {
    perror(PREFIX "Unable to setuid");
    warn_unless_root();
    return 1;
  }

  LOG("Successfully locked memory and setuid to %s\n", opts.setuid_user);
  return 0;
}
