int print(int);
int main() {
  int a = 6 * 7;
  int b = a + 0;
  int c = b * 1;
  int d = c - c;
  int e = (a == 42) && (b != 0);
  int f = 1000 * 1000 * 1000 * 3;
  print(a); print(b); print(c); print(d); print(e); print(f);
  if (1 < 2) print(1); else print(2);
  while (0) print(999);
  return 0;
}
