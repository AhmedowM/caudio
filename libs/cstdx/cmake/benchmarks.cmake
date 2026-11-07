# cstdx benchmarks
#
# Placeholder: Google Benchmark targets comparing cx_vector/cx_string against
# std::vector/std::string, plus allocator and base64 throughput. Added in
# Phase 1.
#
# Benchmark targets are not implemented yet, so enabling the option currently
# only emits this warning instead of silently creating nothing.

message(WARNING
    "CSTDX_BUILD_BENCHMARKS is not implemented yet; no benchmark targets will be built")
