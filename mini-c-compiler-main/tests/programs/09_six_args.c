int print(int);
int mix(int a, int b, int c, int d, int e, int f) {
  return a * 100000 + b * 10000 + c * 1000 + d * 100 + e * 10 + f;
}
int sum6(int a, int b, int c, int d, int e, int f) { return a + b + c + d + e + f; }
int main() {
  print(mix(1, 2, 3, 4, 5, 6));
  print(mix(6, 5, 4, 3, 2, 1));
  print(sum6(mix(0,0,0,0,0,1), 2, 3, 4, sum6(1,1,1,1,1,1), 6));
  return 0;
}
