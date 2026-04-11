CXXSRC = *.cpp trace_sim/*.cpp trace_sim/frontend/*.cpp trace_sim/mem/*.cpp
CXXINCLUDE = -I./include

IMG=./baremetal/memory

default: $(CXXSRC) 
	g++ $(CXXINCLUDE) $(CXXSRC) -O3 -lz -march=native -funroll-loops -mtune=native 

run: 
	./a.out $(IMG)

clean:
	rm -f a.out

gdb:
	g++ $(CXXINCLUDE) $(CXXSRC) -g -march=native -lz
	gdb --args ./a.out $(IMG)

.PHONY: all clean mem run

