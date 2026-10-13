int print(int);
int main() {
  int total = 0;
  for (int i = 0; i < 300; i++)
    for (int j = 0; j < 300; j++)
      total = total + (i * j) % 7 - (i + j) % 3 + i * 4;
  print(total);
  return 0;
}
