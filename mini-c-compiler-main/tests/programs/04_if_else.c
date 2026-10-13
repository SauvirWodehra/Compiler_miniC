int print(int);
int classify(int x) {
  if (x < 0) return -1;
  else if (x == 0) return 0;
  else if (x < 10) return 1;
  else return 2;
}
int main() {
  print(classify(-5)); print(classify(0)); print(classify(7)); print(classify(42));
  int y = 3;
  if (y) { y = y * 10; } else { y = 0; }
  print(y);
  return 0;
}
