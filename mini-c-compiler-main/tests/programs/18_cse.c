int print(int);
int main() {
  int a = 0, b = 0;
  for (int i = 0; i < 4; i++) {
    int x = i * 7 + 3;
    int y = i * 7 + 3;
    a += x * y;
    b += (i * 7 + 3) - x;
  }
  print(a); print(b);
  return 0;
}
