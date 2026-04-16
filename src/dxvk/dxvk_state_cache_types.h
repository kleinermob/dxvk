#pragma once

#include <cstdint>
#include <cstring>

namespace dxvk {

  /**
   * \brief State cache file header
   */
  struct DxvkStateCacheHeader {
    char     magic[4]           = { 'D', 'X', 'S', 'C' };
    uint32_t version            = 1;
    uint32_t entrySize          = 0;
    uint32_t numEntries         = 0;
  };

  /**
   * \brief Cache entry key
   * 
   * Stores shader cookies + pipeline state hash
   */
  struct DxvkStateCacheKey {
    uint32_t vsCookie = 0;
    uint32_t tcsCookie = 0;
    uint32_t tesCookie = 0;
    uint32_t gsCookie = 0;
    uint32_t fsCookie = 0;
    uint64_t stateHash = 0;

    bool eq(const DxvkStateCacheKey& other) const {
      return std::memcmp(this, &other, sizeof(*this)) == 0;
    }

    size_t hash() const {
      uint64_t h = stateHash;
      h ^= vsCookie * 0x100000001b3ull;
      h ^= tcsCookie * 0x100000001b3ull;
      h ^= tesCookie * 0x100000001b3ull;
      h ^= gsCookie * 0x100000001b3ull;
      h ^= fsCookie * 0x100000001b3ull;
      return size_t(h);
    }
  };

  /**
   * \brief Cache entry data
   * 
   * Stores full pipeline state for compilation
   */
  struct DxvkStateCacheEntry {
    DxvkStateCacheKey key;
    // Pipeline state data follows
  };

}
