int print(int);
/* Loop nest with many live temporaries (register pressure) and redundant math. */
int main() {
  int total = 0;
  for (int i = 0; i < 4000; i++) {
    for (int j = 0; j < 4000; j++) {
      int a = i * 8 + j;
      int b = i * 8 + j;      /* common subexpression */
      int c = a * 4 - b;      /* strength-reducible multiply */
      total = total + (c % 13) - (i + j) % 5 + 2 * 3;
    }
  }
  print(total);
  return 0;
}
