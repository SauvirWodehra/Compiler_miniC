int print(int);
int main() {
  int a = 5, b = 9;
  print(a < b); print(a <= b); print(a > b); print(a >= b);
  print(a == b); print(a != b); print(a == 5); print(!a); print(!0); print(!!b);
  print(3 < a); print(10 > a);
  return 0;
}
