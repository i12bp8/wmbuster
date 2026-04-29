# Host-side tests. Run with `make -C tests`.
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -I..
SRC      = test_link.c \
           ../protocol/wmbus_link.c \
           ../protocol/wmbus_manuf.c \
           ../protocol/wmbus_medium.c

test_link: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

check: test_link
	./test_link

clean:
	rm -f test_link

.PHONY: check clean
