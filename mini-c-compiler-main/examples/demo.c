int print(int);

int sum_scaled(int n) {
  int scale = 4 * 2;          // folded to 8
  int total = 0;
  for (int i = 0; i < n; i++) {
    int a = i * scale;        // i * 8  ->  i << 3
    int b = i * scale;        // same expression: reused (CSE)
    total = total + a + b;
  }
  return total;
  print(-1);                  // unreachable: removed
}

int main() {
  print(sum_scaled(10));
  return 0;
}
