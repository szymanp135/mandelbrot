CC=gcc
FLAGS=-std=gnu99 -Wall -O3
LIBS=-lm -pthread
PRGM=mandelbrot

.PHONY:
	all clean

${PRGM}: ${PRGM}.c
	${CC} ${FLAGS} -o ${PRGM} ${PRGM}.c ${LIBS}

clean:
	rm ${PRGM}
