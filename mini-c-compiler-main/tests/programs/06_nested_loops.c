int print(int);
int main() {
  int count = 0;
  for (int i = 0; i < 10; i++)
    for (int j = 0; j < 10; j++) {
      if (j > i) break;
      if ((i + j) % 3 == 0) continue;
      count += i * j;
    }
  print(count);
  int a = 0;
  while (a < 3) { int b = 0; while (b < 4) { print(a * 10 + b); b++; } a++; }
  return 0;
}
