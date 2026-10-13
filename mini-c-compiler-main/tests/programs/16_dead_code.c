int print(int);
int f(int x) {
  return x * 2;
  print(12345);
  x = x + 1;
}
int g(int x) {
  while (1) { if (x > 5) return x; x++; }
  return -1;
}
int main() {
  print(f(21));
  print(g(0));
  int unused = 99 * 3;
  return 0;
}
