CC = gcc
CFLAGS = -Wall -g
THREAD= -pthread

all: manager console sensor

manager: system_Manager.c
	$(CC) $(CFLAGS) $(THREAD) -o home_iot system_Manager.c

console: userConsole.c
	$(CC) $(CFLAGS) $(THREAD) -o console userConsole.c

sensor: sensor.c
	$(CC) $(CFLAGS) $(THREAD) -o sensor sensor.c

clean:
	rm -f home_iot
	rm -f console
	rm -f sensor