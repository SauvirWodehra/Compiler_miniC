int print(int);
int side(int v) { print(100 + v); return v; }
int main() {
  int r = side(0) && side(1);
  print(r);
  r = side(1) || side(2);
  print(r);
  r = side(1) && side(3) && side(0) && side(4);
  print(r);
  r = side(0) || side(0) || side(5);
  print(r);
  if (!(side(0) || side(0))) print(777);
  return 0;
}
