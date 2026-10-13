int print(int);
int is_odd(int n);
int is_even(int n) { if (n == 0) return 1; return is_odd(n - 1); }
int is_odd(int n) { if (n == 0) return 0; return is_even(n - 1); }
int main() {
  for (int i = 0; i < 6; i++) print(is_even(i) * 10 + is_odd(i));
  return 0;
}
