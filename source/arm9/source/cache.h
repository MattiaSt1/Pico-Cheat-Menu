#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Invalidates the entire instruction cache.
extern void ic_invalidateAll(void);

/// @brief Drains the write buffer.
extern void dc_drainWriteBuffer(void);

/// @brief Invalidates the entire data cache.
extern void dc_invalidateAll(void);

/// @brief Flushes the entire data cache.
extern void dc_flushAll(void);

/// @brief Invalidates the data cache in the given range.
/// @param ptr A pointer to the memory block to invalidate. Should be 32-byte aligned.
/// @param byteCount The number of bytes to invalidate. Will be rounded up to 32-byte multiples.
extern void dc_invalidateRange(void* ptr, u32 byteCount);

#ifdef __cplusplus
}
#endif
