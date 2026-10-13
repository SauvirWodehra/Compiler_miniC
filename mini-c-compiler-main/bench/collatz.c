int print(int);
int main() {
  int best = 0, arg = 0;
  for (int i = 1; i < 100000; i++) {
    int n = i, s = 0;
    while (n != 1) { if (n % 2 == 0) n = n / 2; else n = 3 * n + 1; s++; }
    if (s > best) { best = s; arg = i; }
  }
  print(arg); print(best);
  return 0;
}
