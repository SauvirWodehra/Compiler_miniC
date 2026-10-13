int print(int);
/* Division/modulo by powers of two are lowered to shifts; C truncates toward
   zero, so negative dividends are the interesting cases. */
int check(int x) {
  print(x / 2); print(x % 2);
  print(x / 4); print(x % 4);
  print(x / 8); print(x % 8);
  print(x / 1024); print(x % 1024);
  print(x / 1073741824); print(x % 1073741824);
  return 0;
}
int main() {
  int xs = 0;
  check(0); check(1); check(-1); check(7); check(-7); check(8); check(-8);
  check(1023); check(-1023); check(2147483647); check(-2147483647 - 1);
  for (int i = -20; i <= 20; i++) xs = xs * 3 + i / 4 - i % 4 + i / 2;
  print(xs);
  return 0;
}
