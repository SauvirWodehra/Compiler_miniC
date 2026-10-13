int print(int);
int main() {
  int s = 0;
  for (int i = 0; i < 10; i++) s = s + i;
  return s;   /* exit status 45 is compared too */
}
