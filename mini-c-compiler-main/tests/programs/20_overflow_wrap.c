int print(int);
int main() {
  int x = 2147483647;
  x = x + 1; print(x);
  int y = -2147483647 - 1;
  print(y - 1);
  print(y * -1);
  print(65536 * 65536);
  int h = 0;
  for (int i = 0; i < 100; i++) h = h * 31 + i;
  print(h);
  return 0;
}
