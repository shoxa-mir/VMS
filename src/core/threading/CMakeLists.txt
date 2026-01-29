# src/core/threading/CMakeLists.txt
# Phase 3: Threading components

set(THREADING_HEADERS
    thread_pool.h
    bounded_queue.h
)

set(THREADING_SOURCES
    thread_pool.cpp
)

# Note: bounded_queue.h is header-only, no .cpp file needed
