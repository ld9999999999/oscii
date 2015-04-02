
LDFLAGS= -lSDL2 -pthread

OBJS=	oscii.o

all: ${OBJS}
	${CC} ${OBJS} -o oscii ${LDFLAGS}

oscii.o: oscii.c
	${CC} -c oscii.c
	
