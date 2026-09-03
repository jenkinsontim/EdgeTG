# EdgeTG â€” unified build
CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -fsanitize=address,undefined -g
ASAN    = ASAN_OPTIONS=detect_leaks=1

CORE    = ts_core.c
EXT     = ts_core_ext.c
LAYERS  = ts_layers.c
PRIOR1  = ts_intern.c ts_norm.c
PRIOR23 = ts_roles.c ts_boltzmann.c ts_enum.c ts_metric.c
DEVICE  = device.c
PACKED  = ts_packed.c

.PHONY: all test clean

all: test_layers test_extended test_priority1 test_priority23 device_bench test_integration \
     test_packed test_dual_wire_format mcu_executor test_evolution_experiment

test_layers: $(CORE) $(LAYERS) test_layers.c
	$(CC) $(CFLAGS) $^ -o $@

test_extended: $(CORE) $(EXT) test_extended.c
	$(CC) $(CFLAGS) $^ -o $@

test_priority1: $(CORE) $(EXT) $(LAYERS) $(PRIOR1) test_priority1.c
	$(CC) $(CFLAGS) $^ -o $@

test_priority23: $(CORE) $(EXT) $(LAYERS) $(PRIOR23) test_priority23.c
	$(CC) $(CFLAGS) $^ -o $@

device_bench: $(CORE) $(EXT) $(DEVICE) device_bench.c
	$(CC) $(CFLAGS) $^ -o $@

test_integration: $(CORE) $(EXT) $(LAYERS) $(PRIOR1) $(PRIOR23) $(DEVICE) test_integration.c
	$(CC) $(CFLAGS) $^ -o $@

test_packed: $(CORE) $(EXT) $(PACKED) test_packed.c
	$(CC) $(CFLAGS) $^ -o $@

test_dual_wire_format: $(CORE) $(PACKED) test_dual_wire_format.c
	$(CC) $(CFLAGS) $^ -o $@

mcu_executor: $(CORE) $(LAYERS) $(PACKED) mcu_executor.c
	$(CC) $(CFLAGS) $^ -o $@

test_evolution_experiment: $(CORE) $(EXT) $(LAYERS) $(PRIOR1) $(PRIOR23) $(DEVICE) test_evolution_experiment.c
	$(CC) $(CFLAGS) $^ -o $@

test: all
	@echo "=== test_layers ==="     && $(ASAN) ./test_layers
	@echo "=== test_extended ==="   && $(ASAN) ./test_extended
	@echo "=== test_priority1 ==="  && $(ASAN) ./test_priority1
	@echo "=== test_priority23 ===" && $(ASAN) ./test_priority23
	@echo "=== device_bench ==="    && $(ASAN) ./device_bench
	@echo "=== test_integration ==="&& $(ASAN) ./test_integration
	@echo "=== test_packed ==="     && $(ASAN) ./test_packed
	@echo "=== test_dual_wire_format ===" && $(ASAN) ./test_dual_wire_format
	@echo "ALL SUITES PASSED"

clean:
	rm -f test_layers test_extended test_priority1 test_priority23 device_bench test_integration \
	      test_packed test_dual_wire_format mcu_executor test_evolution_experiment \
	      wire_packet.bin wire_values.bin reply_packet.bin reply_values.bin \
	      wire_payload.bin wire_reply.bin wire_reply_packet.bin wire_reply_values.bin
