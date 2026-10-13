// Tiny runtime linked into every test program: print(n) writes n and a newline.
#include <stdio.h>

int print(int n) {
  printf("%d\n", n);
  return n;
}
