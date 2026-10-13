int print(int);
int is_prime(int n) {
  if (n < 2) return 0;
  for (int d = 2; d * d <= n; d++) if (n % d == 0) return 0;
  return 1;
}
int main() {
  int count = 0;
  for (int n = 0; n < 1500000; n++) count += is_prime(n);
  print(count);
  return 0;
}
