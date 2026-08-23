# Three firmwares from one component.
#   PRODUCT=0 (default)  -> main.c       validation: parity vs host + benchmarks
#   -DPRODUCT=1          -> product.c    the device: UART in, GPIO/PWM out
#   MOGWAI_PROBE=1 (env) -> wifiprobe.c  measurement: heap cost of a WiFi stack
# main.c and product.c share router.c/ternary.c, which are SYMLINKS into
# ../../c/src, so the device runs the arithmetic the host measured — not a copy.
#
# Why PROBE is an ENVIRONMENT variable while PRODUCT is a -D:
#
# ESP-IDF evaluates this file TWICE — an early expansion pass that collects
# component requirements, then the real one. Project-level `-D` cache variables
# are NOT visible in the early pass. A `-D`-conditional REQUIRES therefore takes
# the wrong branch silently: the build picks wifiprobe.c from the real pass but
# keeps `REQUIRES driver esp_timer` from the early one, and fails with
# "esp_wifi.h: No such file or directory" — which reads as a missing component
# rather than a two-pass problem. `-DPRODUCT=1` gets away with it only because
# both of its branches declare identical requirements. $ENV{} survives both.
if(NOT DEFINED PRODUCT)
  set(PRODUCT 0)
endif()
set(PROBE "$ENV{MOGWAI_PROBE}")

if(PROBE)
  # No router.c/ternary.c and no blob: the probe measures the network stack, and
  # linking the router in would only muddy the heap figures it exists to report.
  idf_component_register(SRCS "wifiprobe.c"
                         INCLUDE_DIRS "."
                         REQUIRES driver esp_timer esp_wifi esp_netif esp_event
                                  nvs_flash esp_http_client esp-tls mbedtls)
else()
  if(PRODUCT)
    set(MAIN_SRC "product.c")
  else()
    set(MAIN_SRC "main.c")
  endif()
  idf_component_register(SRCS "${MAIN_SRC}" "router.c" "ternary.c"
                         INCLUDE_DIRS "."
                         REQUIRES driver esp_timer
                         EMBED_FILES "router.bin")
endif()

# These apply to every build and MUST NOT be dropped. Removing them does not
# fail loudly: main.c stops compiling (TPOPCNT undeclared), but product.c builds
# fine and silently runs `#if TPOPCNT == 1` as false — i.e. the firmware ships
# WITHOUT the popcount table, 43.5 ms of work instead of the measured path, with
# nothing in the output to say so.
# popcount strategy: 0 = builtin SWAR, 1 = 8-bit table (DRAM), 2 = 16-bit table
if(NOT DEFINED TPOPCNT)
  set(TPOPCNT 1)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE TPOPCNT=${TPOPCNT})

# router dimension; blob and firmware must agree or parity is meaningless
if(NOT DEFINED RD)
  set(RD 256)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE RD=${RD})
