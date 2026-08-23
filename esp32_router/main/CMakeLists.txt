# Firmwares from one component:
#   PRODUCT=0 (default)  -> main.c       validation: parity vs host + benchmarks
#   -DPRODUCT=1          -> product.c    the device: UART in, GPIO/PWM out
#   MOGWAI_WIFI=1 (env)  -> product.c + network stack, TLS-sized lift reserve
#   MOGWAI_PROBE=1 (env) -> wifiprobe.c  measurement: heap cost of a WiFi stack
# main.c and product.c share router.c/ternary.c, which are SYMLINKS into
# ../../c/src, so the device runs the arithmetic the host measured — not a copy.
#
# Why the network flags are ENVIRONMENT variables while PRODUCT is a -D:
#
# ESP-IDF evaluates this file TWICE — an early expansion pass that collects
# component requirements, then the real one. Project-level `-D` cache variables
# are NOT visible in the early pass. A `-D`-conditional REQUIRES therefore takes
# the wrong branch silently: the build picks the right sources from the real
# pass but keeps the early pass's requirements, and fails as "esp_wifi.h: No
# such file or directory" — which reads as a missing component rather than a
# two-pass problem. `-DPRODUCT=1` gets away with it only because both of its
# branches declare identical requirements. $ENV{} survives both passes.
if(NOT DEFINED PRODUCT)
  set(PRODUCT 0)
endif()
set(PROBE "$ENV{MOGWAI_PROBE}")
set(WIFI  "$ENV{MOGWAI_WIFI}")

set(NET_REQ esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls mbedtls)

if(PROBE)
  idf_component_register(SRCS "wifiprobe.c" "router.c" "ternary.c" "lift.c"
                         EMBED_FILES "router.bin"
                         INCLUDE_DIRS "."
                         REQUIRES driver esp_timer ${NET_REQ})
elseif(WIFI)
  idf_component_register(SRCS "product.c" "router.c" "ternary.c" "lift.c"
                         INCLUDE_DIRS "."
                         REQUIRES driver esp_timer ${NET_REQ}
                         EMBED_FILES "router.bin")
else()
  if(PRODUCT)
    set(MAIN_SRC "product.c")
  else()
    set(MAIN_SRC "main.c")
  endif()
  idf_component_register(SRCS "${MAIN_SRC}" "router.c" "ternary.c" "lift.c"
                         INCLUDE_DIRS "."
                         REQUIRES driver esp_timer
                         EMBED_FILES "router.bin")
endif()

if(WIFI)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE MOGWAI_WIFI=1)
endif()

# These apply to every build and MUST NOT be dropped. Removing them does not
# fail loudly: main.c stops compiling (TPOPCNT undeclared), but product.c builds
# fine and silently runs `#if TPOPCNT == 1` as false — i.e. the firmware ships
# WITHOUT the popcount table, with nothing in the output to say so.
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
