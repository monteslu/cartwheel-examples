#!/usr/bin/env python3
# a dev helper for the coordinator: rewrites the SPEC of a map file between its markers, keeping the rest.
import sys,re
path,spec=sys.argv[1],open(sys.argv[2]).read()
s=open(path).read()
a=s.index("static const piece SPEC[] = {")
b=s.index("};",a)+2
s=s[:a]+"static const piece SPEC[] = {\n"+spec.strip("\n")+"\n};"+s[b:]
open(path,'w').write(s)
