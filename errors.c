#include "errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void die(const char* s){
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(s);
  exit(1);
}
