# KawPow / Ethash static libs (Apache-2.0, RavenCommunity/cpp-kawpow)
set(KAWPOW_UPSTREAM ${CMAKE_CURRENT_SOURCE_DIR}/../kawpow-upstream)
set(KAWPOW_INCLUDE ${KAWPOW_UPSTREAM}/include)

add_library(hobbyhash_keccak STATIC EXCLUDE_FROM_ALL
  ${KAWPOW_UPSTREAM}/lib/keccak/keccak.c
  ${KAWPOW_UPSTREAM}/lib/keccak/keccakf800.c
  ${KAWPOW_UPSTREAM}/lib/keccak/keccakf1600.c
)
target_include_directories(hobbyhash_keccak PUBLIC ${KAWPOW_INCLUDE})
target_link_libraries(hobbyhash_keccak PRIVATE core_interface)

add_library(hobbyhash_ethash STATIC EXCLUDE_FROM_ALL
  ${KAWPOW_UPSTREAM}/lib/ethash/ethash.cpp
  ${KAWPOW_UPSTREAM}/lib/ethash/managed.cpp
  ${KAWPOW_UPSTREAM}/lib/ethash/progpow.cpp
  ${KAWPOW_UPSTREAM}/lib/ethash/primes.c
)
target_include_directories(hobbyhash_ethash PUBLIC ${KAWPOW_INCLUDE})
target_link_libraries(hobbyhash_ethash PUBLIC hobbyhash_keccak PRIVATE core_interface)
