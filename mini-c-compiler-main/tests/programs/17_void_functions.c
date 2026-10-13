int print(int);
void show(int a, int b) { print(a); if (b) return; print(b); }
void nothing(void) { }
int main() {
  show(1, 0);
  show(2, 5);
  nothing();
  return 0;
}
