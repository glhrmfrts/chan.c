#define chan_unittest
#define chan_implementation

#include "chan.c"

int main(int argc, const char** argv) {
  (void) argc;
  (void) argv;
  return !chan_runtest();
}
