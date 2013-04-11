CC 	= gcc
CFLAGS 	= -g -Wall
OBJ	= mpirun.o
TARGET	= mpirun
LIB	= libsompi.so

build: $(OBJ)
	$(CC) -shared $(OBJ) -o $(LIB) -lrt
	$(CC) mpirun.c -o $(TARGET) -lrt
# -lsompi -L.	

$(OBJ): mpirun.c
	$(CC) -fPIC -c mpirun.c -lrt

clean:
	rm -rf $(LIB) $(OBJ) $(TARGET) *~
