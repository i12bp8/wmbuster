# Host-side tests. Run with `make -C tests`.
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -I..
SRC      = test_link.c \
           ../protocol/wmbus_link.c \
           ../protocol/wmbus_manuf.c \
           ../protocol/wmbus_medium.c

test_link: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

IZAR_SRC = test_izar.c \
           ../drivers/europe/diehl/izar.c \
           ../drivers/engine/driver.c \
           ../protocol/wmbus_link.c \
           ../protocol/wmbus_manuf.c \
           ../protocol/wmbus_medium.c \
           ../protocol/wmbus_app_layer.c

test_izar: $(IZAR_SRC)
	$(CC) $(CFLAGS) $(IZAR_SRC) -o $@

APATOR_HCA_SRC = test_apator_hca.c \
           ../drivers/europe/apator/apator.c \
           ../drivers/engine/driver.c \
           ../protocol/wmbus_link.c \
           ../protocol/wmbus_manuf.c \
           ../protocol/wmbus_medium.c \
           ../protocol/wmbus_app_layer.c

test_apator_hca: $(APATOR_HCA_SRC)
	$(CC) $(CFLAGS) $(APATOR_HCA_SRC) -o $@

PORTS_SRC = test_ports.c \
            ../drivers/europe/bmeters/hydrodigit.c \
            ../drivers/europe/engelmann/hydroclima.c \
            ../drivers/europe/sontex/rfmtx1.c \
            ../drivers/europe/zenner/zenner0b.c \
            ../drivers/europe/techem/mkradio3a.c \
            ../drivers/europe/misc/gwfwater.c \
            ../drivers/engine/driver.c \
            ../protocol/wmbus_link.c \
            ../protocol/wmbus_manuf.c \
            ../protocol/wmbus_medium.c \
            ../protocol/wmbus_app_layer.c

test_ports: $(PORTS_SRC)
	$(CC) $(CFLAGS) $(PORTS_SRC) -o $@

COMMON_SRC = ../drivers/europe/techem/_compact.c \
             ../drivers/engine/driver.c \
             ../protocol/wmbus_link.c \
             ../protocol/wmbus_manuf.c \
             ../protocol/wmbus_medium.c \
             ../protocol/wmbus_app_layer.c

TECHEM_WATER_SRC = test_techem_water.c \
             ../drivers/europe/techem/mkradio_water.c \
             $(COMMON_SRC)

test_techem_water: $(TECHEM_WATER_SRC)
	$(CC) $(CFLAGS) $(TECHEM_WATER_SRC) -o $@

TECHEM_OTHER_SRC = test_techem_other.c \
             ../drivers/europe/techem/fhkv_hca.c \
             ../drivers/europe/techem/compact5_heat.c \
             ../drivers/europe/techem/vario_heat.c \
             ../drivers/europe/techem/smoke.c \
             $(COMMON_SRC)

test_techem_other: $(TECHEM_OTHER_SRC)
	$(CC) $(CFLAGS) $(TECHEM_OTHER_SRC) -o $@

check: test_link test_izar test_apator_hca test_ports test_techem_water test_techem_other
	./test_link
	./test_izar
	./test_apator_hca
	./test_ports
	./test_techem_water
	./test_techem_other

clean:
	rm -f test_link test_izar test_apator_hca test_ports test_techem_water test_techem_other

.PHONY: check clean
