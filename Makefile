CLFLAGS=/Fo:build\ /GS- /GL /O1 /favor:AMD64 /nologo /DHASHMAP_PROCESS_HEAP
LINKFLAGS=Kernel32.lib /link /NODEFAULTLIB /SUBSYSTEM:CONSOLE /LTCG /entry:main
all: symbols.exe

build:
	-@ if NOT EXIST "build" mkdir "build"

build\args.obj: args.c args.h build
	cl /c $(CLFLAGS) args.c

build\printf.obj: printf.c printf.h build
	cl /c $(CLFLAGS) printf.c

build\hashmap.obj: hashmap.c hashmap.h build
	cl /c $(CLFLAGS) hashmap.c

build\ntdll.lib: build
	lib /DEF /NAME:ntdll.dll /OUT:build\ntdll.lib /MACHINE:X64\
		/EXPORT:_vsnwprintf=_vsnwprintf /EXPORT:_vsnprintf=_vsnprintf\
		/EXPORT:_vscwprintf=_vscwprintf /EXPORT:_vscprintf=_vscprintf\
		/EXPORT:strchr=strchr /EXPORT:memcpy=memcpy /EXPORT:strlen=strlen\
		/EXPORT:wcslen=wcslen /EXPORT:_wsplitpath_s=_wsplitpath_s\
		/EXPORT:_wmakepath_s=_wmakepath_s /EXPORT:memmove=memmove\
		/EXPORT:wcscmp=wcscmp /EXPORT:strncmp=strncmp /EXPORT:strcmp=strcmp

symbols.exe: build\args.obj build\printf.obj build\hashmap.obj build\ntdll.lib
	cl $(CLFLAGS) /Fe:symbols.exe build\args.obj build\printf.obj build\hashmap.obj build\ntdll.lib symbols.c $(LINKFLAGS)

clean:
	del build\* /Q
	del symbols.exe /Q

