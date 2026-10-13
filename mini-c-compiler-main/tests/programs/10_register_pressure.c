int print(int);
/* More simultaneously-live values than the 5 allocatable registers: forces spills. */
int main() {
  int a = 1, b = 2, c = 3, d = 4, e = 5, f = 6, g = 7, h = 8, i = 9, j = 10;
  for (int k = 0; k < 5; k++) {
    a = a + b; b = b + c; c = c + d; d = d + e; e = e + f;
    f = f + g; g = g + h; h = h + i; i = i + j; j = j + a;
  }
  print(a); print(b); print(c); print(d); print(e);
  print(f); print(g); print(h); print(i); print(j);
  print(a + b + c + d + e + f + g + h + i + j);
  return 0;
}
