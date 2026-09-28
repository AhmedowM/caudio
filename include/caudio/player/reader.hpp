/**
 * @file reader.hpp
 * @brief Input reader abstractions for audio data sources
 * @ingroup caudio_player
 *
 * This header provides the Reader abstract interface and concrete implementations
 * for reading audio data from various sources. It mirrors the C ca_reader.c
 * interface and provides:
 * - Reader: Abstract base class for all input sources
 * - FileReader: File-based reading with 64-bit seek support
 * - MemoryReader: In-memory buffer reading
 *
 * Thread Safety:
 * - Reader interface: Thread-compatible (single-threaded use expected)
 * - FileReader: Thread-safe for concurrent read/seek/tell (internal mutex)
 * - MemoryReader: Not thread-safe (no internal synchronization)
 *
 * Error Codes (via Expected<void>):
 * - InvalidArg: Null file, bad whence, seek out of range, empty path
 * - NotFound: File not found or cannot be opened
 * - Internal: fseek/ftell failure, size not cached
 * - NoMem: Allocation failure
 */

#pragma once

#if defined(_WIN32)
#include <cstdio>
#else
#include <cstdio>
#endif
#include <caudio/utils.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace caudio::player {

/**
 * @class Reader
 * @brief Abstract base class for audio data input sources
 * @ingroup caudio_player
 *
 * Defines the interface for reading audio data. All concrete readers
 * (FileReader, MemoryReader, etc.) must implement this interface.
 * Mirrors the C ca_reader.c virtual table.
 *
 * Thread Safety: Thread-compatible. The Player uses a single reader
 * from the decode thread. Concurrent access requires external synchronization
 * (except FileReader which has internal mutex for seek/tell).
 *
 * NOTE: The `data()` method from C ca_reader.c was removed to avoid
 * GCC 14.2 module ICE. Decoders needing direct memory access (e.g., Vorbis)
 * should use MemoryReader which provides contiguous memory access.
 */
class Reader {
  public:
    virtual ~Reader() = default;

    /**
     * @brief Read data into the provided buffer
     * @param dst Destination buffer
     * @return Number of bytes read (0 = EOF or error)
     *
     * Reads up to dst.size() bytes from the current position.
     * Advances the internal position by the number of bytes read.
     * Returns 0 on EOF or if the source is invalid.
     */
    virtual std::size_t read(std::span<std::byte> dst) = 0;

    /**
     * @brief Seek to a new position
     * @param offset Byte offset from whence
     * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
     * @return void on success, Error on failure
     *
     * Sets the read position relative to whence. Validates that the
     * resulting position is within [0, size()].
     *
     * @retval StatusCode::InvalidArg whence invalid or seek out of range
     * @retval StatusCode::Internal fseek/ftell failed
     */
    [[nodiscard]] virtual caudio::utils::Expected<void> seek(int64_t offset, int whence) = 0;

    /**
     * @brief Get current read position
     * @return Current byte offset from start, or -1 on error
     */
    [[nodiscard]] virtual int64_t tell() noexcept = 0;

    /**
     * @brief Get total size of the data source
     * @return Total size in bytes, or -1 if unknown
     */
    [[nodiscard]] virtual int64_t size() noexcept = 0;
};

/**
 * @class FileReader
 * @brief File-based reader with 64-bit seek support
 * @ingroup caudio_player
 *
 * Reads audio data from a file on disk. Uses platform-specific 64-bit
 * file APIs (_ftelli64/_fseeki64 on Windows, ftello/fseeko on POSIX)
 * to support files larger than 2GB.
 *
 * Thread Safety: Thread-safe for concurrent read/seek/tell operations
 * via internal mutex. The FILE* is owned exclusively by this instance.
 *
 * Error Handling:
 * - open(): Returns Error on file not found, permission denied, or seek failure
 * - read(): Returns 0 on EOF or read error
 * - seek(): Returns Error on invalid whence, out of range, or fseek failure
 * - tell(): Returns -1 on error
 * - size(): Returns cached size or -1
 */
class FileReader final : public Reader {
  public:
    FileReader() = delete;

    /**
     * @brief Open a file for reading
     * @param path Path to the audio file
     * @return Expected containing unique_ptr<Reader> on success, Error on failure
     *
     * Opens the file in binary mode, seeks to end to determine size,
     * then seeks back to start. On Windows, tries wide-char path first
     * then falls back to narrow string.
     *
     * @retval StatusCode::InvalidArg Empty path
     * @retval StatusCode::NotFound Cannot open file
     * @retval StatusCode::Internal fseek/ftell failed during size detection
     *
     * Thread Safety: Thread-safe (no shared state during construction)
     */
    static caudio::utils::Expected<std::unique_ptr<Reader>> open(const std::filesystem::path& path);

    ~FileReader() override;

    std::size_t read(std::span<std::byte> dst) override;

    [[nodiscard]] caudio::utils::Expected<void> seek(int64_t offset, int whence) override;

    [[nodiscard]] int64_t tell() noexcept override;

    [[nodiscard]] int64_t size() noexcept override;

  private:
    explicit FileReader(FILE* f);
    FILE* file_{nullptr};
    int64_t fileSize_{-1};
    mutable std::mutex seekMutex_{};
};

/**
 * @class MemoryReader
 * @brief In-memory buffer reader
 * @ingroup caudio_player
 *
 * Reads audio data from an in-memory buffer. Provides fast, seekable
 * access without file I/O. Copies input data to an owned buffer for
 * lifetime safety.
 *
 * Thread Safety: NOT thread-safe. No internal synchronization. Intended
 * for single-threaded use (e.g., decode thread).
 *
 * Use Cases:
 * - Embedded audio data
 * - Network streams buffered in memory
 * - Decoders requiring direct memory access (Vorbis, etc.)
 */
class MemoryReader final : public Reader {
  public:
    MemoryReader() = delete;

    /**
     * @brief Open a memory reader from a byte span
     * @param data Input data (copied to owned buffer)
     * @return Expected containing unique_ptr<Reader> on success
     *
     * Copies the input data to an internal buffer. The original data
     * can be freed after this call returns.
     */
    static caudio::utils::Expected<std::unique_ptr<Reader>> open(std::span<const std::byte> data);

    /**
     * @brief Open a memory reader from a vector
     * @param data Input vector (copied)
     * @return Expected containing unique_ptr<Reader> on success
     */
    static caudio::utils::Expected<std::unique_ptr<Reader>>
    open(const std::vector<std::byte>& data);

    /**
     * @brief Open a memory reader from raw pointer and length
     * @param data Pointer to data
     * @param len Length in bytes
     * @return Expected containing unique_ptr<Reader> on success, Error if null data with len>0
     */
    static caudio::utils::Expected<std::unique_ptr<Reader>> open(const std::byte* data,
                                                                 std::size_t len);

    std::size_t read(std::span<std::byte> dst) override;

    [[nodiscard]] caudio::utils::Expected<void> seek(int64_t offset, int whence) override;

    [[nodiscard]] int64_t tell() noexcept override;
    [[nodiscard]] int64_t size() noexcept override;

  private:
    explicit MemoryReader(std::span<const std::byte> src);
    std::vector<std::byte> buf_;
    std::size_t pos_{0};
};

} // namespace caudio::player
