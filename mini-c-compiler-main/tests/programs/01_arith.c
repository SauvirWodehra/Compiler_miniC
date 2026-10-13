int print(int);
int main() {
  print(1 + 2 * 3);
  print((1 + 2) * 3);
  print(100 - 7 - 3);
  print(-17 / 5);
  print(-17 % 5);
  print(17 % -5);
  print(2147483647 + 1);      /* wraps (compiled with -fwrapv) */
  print(-(-5));
  print(+8);
  return 0;
}
