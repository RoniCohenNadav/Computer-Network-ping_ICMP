CC=gcc
CFLAGS=-Wall -I.
OBJ=traceroute.o
TARGET=traceroute

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

$(OBJ): traceroute.c traceroute.h
	$(CC) -c traceroute.c $(CFLAGS)

clean:
	rm -f $(OBJ) $(TARGET) traceroute
