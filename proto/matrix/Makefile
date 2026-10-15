# Local host build for the matrix prototype (not part of the ROM build).
CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
SRC = mx_core.c mx_mob.c mx_world.c mx_build.c sim.c
mxsim: $(SRC) mx.h mx_build.h
	$(CC) $(CFLAGS) -o $@ $(SRC)
report: mxsim
	./mxsim > report.md
clean:
	rm -f mxsim report.md
.PHONY: report clean
