int print(int);
int main() {
  int x = 10;
  x += 5; print(x);
  x -= 3; print(x);
  x *= 4; print(x);
  x /= 5; print(x);
  x %= 4; print(x);
  int y = 7;
  print(y++); print(y); print(++y); print(y--); print(--y);
  int a, b, c;
  a = b = c = 42;
  print(a + b + c);
  return 0;
}
