proc my_expand(D, off) {
  return D.expand(off);
}

proc dit(D, lo, hi) {
  writeln("D: ", D);
  for i in lo..hi do
    writeln(my_expand(D, i));
}

config const n = 10:int(8);
{
  const D = {-n/2..n};
  writeln(typeToString(n.type), ":");
  dit(D, -n/2, n/2);
}


config const un = 10:uint(8);
{
  const uD = {-un/2..un};
  writeln(typeToString(un.type), ":");
  dit(uD, -n/2, n/2);
}
