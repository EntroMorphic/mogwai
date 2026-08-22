# Two firmwares from one component.
#   PRODUCT=0 (default) -> main.c     validation: parity vs host + benchmarks
#   PRODUCT=1           -> product.c  the device: UART in, GPIO/PWM out
# They share router.c/ternary.c, which are SYMLINKS into ../../c/src, so the
# device runs the arithmetic the host measured — not a copy of it.
if(NOT DEFINED PRODUCT)
  set(PRODUCT 0)
endif()
if(PRODUCT)
  set(MAIN_SRC "product.c")
else()
  set(MAIN_SRC "main.c")
endif()

idf_component_register(SRCS "${MAIN_SRC}" "router.c" "ternary.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_timer
                       EMBED_FILES "router.bin")

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
