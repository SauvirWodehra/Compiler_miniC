int print(int);
int main() {
  int s = 0, i = 0;
  while (i < 10) { s += i; i++; }
  print(s);
  int p = 1;
  for (int k = 1; k <= 10; k++) p = p * 2;
  print(p);
  int n = 0;
  for (;;) { n++; if (n >= 7) break; }
  print(n);
  int odd = 0;
  for (int k = 0; k < 20; k++) { if (k % 2 == 0) continue; odd += k; }
  print(odd);
  return 0;
}
