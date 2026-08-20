idf_component_register(SRCS "main.c" "router.c" "ternary.c"
                       INCLUDE_DIRS "."
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
