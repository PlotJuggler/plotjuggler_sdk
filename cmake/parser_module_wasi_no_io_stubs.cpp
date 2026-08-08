// Copyright 2026 Davide Faconti
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

// The authoring kit performs no I/O, but wasi-libc's unreachable abort and
// stdio teardown paths otherwise leave three fd imports in a C++ reactor.
// Resolve them inside each authored module so the frozen host import allow-list
// remains empty. WASI errno 8 is BADF.
extern "C" uint32_t __imported_wasi_snapshot_preview1_fd_close(uint32_t fd) {
  (void)fd;
  return 8;
}

extern "C" uint32_t __imported_wasi_snapshot_preview1_fd_seek(
    uint32_t fd, uint64_t offset, uint32_t whence, uint32_t new_offset) {
  (void)fd;
  (void)offset;
  (void)whence;
  (void)new_offset;
  return 8;
}

extern "C" uint32_t __imported_wasi_snapshot_preview1_fd_write(
    uint32_t fd, uint32_t iovecs, uint32_t iovec_count, uint32_t written) {
  (void)fd;
  (void)iovecs;
  (void)iovec_count;
  (void)written;
  return 8;
}
