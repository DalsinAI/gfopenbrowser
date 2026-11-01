# The AmiCradle Team. Lab only: build.ninja tweaks for the m68k-linux jsc host build
# (rerun after every cmake regen). JSJIT_BUILD, ICU_LIB and DEFSYMS set the paths.
# defsyms.txt maps the asm's _names to C names both ways; make it from the LLInt object:
#   m68k-linux-gnu-nm LowLevelInterpreter.cpp.o > nm.txt
#   awk '$1=="U"{u[$2]=1} NF==3 && $2~/[TDBR]/{d[$3]=1} END{for(s in u) if(("_" s) in d) print "-Wl,--defsym," s "=_" s}' nm.txt > defsyms.txt
#   awk '$1=="U" && $2~/^_[^Z_]/{print "-Wl,--defsym," $2 "=" substr($2,2)}' nm.txt >> defsyms.txt
import os, re
H=os.path.expanduser('~')
p=os.environ.get('JSJIT_BUILD', H+'/jsjit-host/build')+'/build.ninja'; s=open(p).read()
I=os.environ.get('ICU_LIB', H+'/icu/m68k-install/lib')
for n in ('data','i18n','uc'):
    s=s.replace('/usr/lib/x86_64-linux-gnu/libicu%s.so'%n, '%s/libicu%s.a'%(I,n))
s=s.replace('%s/libicudata.a  %s/libicui18n.a  %s/libicuuc.a'%(I,I,I), '%s/libicui18n.a  %s/libicuuc.a  %s/libicudata.a'%(I,I,I))
# LLInt object: no PIC, bare register names.
m=re.search(r'(build Source/JavaScriptCore/CMakeFiles/LowLevelInterpreterLib.dir/llint/LowLevelInterpreter.cpp.o:.*?\n(?:  .*\n)*?  FLAGS = )([^\n]*)', s)
flags=m.group(2).replace(' -fPIC','')
if '--register-prefix-optional' not in flags: flags+=' -Wa,--register-prefix-optional'
s=s[:m.start(2)]+flags+s[m.end(2):]
# jsc link: C names -> the asm's underscored names.
D=open(os.environ.get('DEFSYMS', 'defsyms.txt')).read().split()
m=re.search(r'(build bin/jsc:.*?\n(?:  .*\n)*?  LINK_LIBRARIES = )', s)
if D[0] not in s[m.end():m.end()+200]:
    s=s[:m.end()]+' '.join(D)+' '+s[m.end():]
open(p,'w').write(s)
