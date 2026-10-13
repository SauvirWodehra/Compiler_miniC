int print(int);
int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
int ack(int m, int n) {
  if (m == 0) return n + 1;
  if (n == 0) return ack(m - 1, 1);
  return ack(m - 1, ack(m, n - 1));
}
int gcd(int a, int b) { if (b == 0) return a; return gcd(b, a % b); }
int main() {
  print(fact(10)); print(fib(25)); print(ack(2, 3)); print(gcd(1071, 462));
  return 0;
}
