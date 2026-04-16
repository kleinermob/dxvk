#include <fstream>
#include <iomanip>
#include <iostream>

#include "dxvk_state_cache.h"
#include "dxvk_device.h"
#include "dxvk_pipemanager.h"
#include "dxvk_graphics.h"
#include "dxvk_util.h"

#include "../util/util_string.h"

#include "../util/util_env.h"
#include "../util/log/log.h"

namespace dxvk {

  DxvkStateCache::DxvkStateCache(DxvkDevice* device, DxvkPipelineManager* manager)
  : m_device(device), m_manager(manager), m_cachePath(getCacheFilePath()) {
    if (isCacheEnabled()) {
      initializeCache();
      m_writerThread = dxvk::thread([this]() { runWriter(); });
    }
  }

  DxvkStateCache::~DxvkStateCache() {
    if (m_writerThread.joinable()) {
      { std::unique_lock lock(m_writeMutex);
        m_stopWriter = true;
        m_writeCond.notify_one();
      }
      m_writerThread.join();
    }
  }

  void DxvkStateCache::registerShader(const Rc<DxvkShader>& shader) {
    if (!isCacheEnabled() || !m_initialized)
      return;

    // No-op for now - could trigger async compilation
  }

  void DxvkStateCache::addGraphicsPipeline(
    const DxvkGraphicsPipelineShaders& shaders,
    const DxvkGraphicsPipelineStateInfo& state) {
    if (!isCacheEnabled() || !m_initialized)
      return;

    CacheEntry entry;
    entry.key.vsCookie = DxvkShader::getCookie(shaders.vs);
    entry.key.tcsCookie = DxvkShader::getCookie(shaders.tcs);
    entry.key.tesCookie = DxvkShader::getCookie(shaders.tes);
    entry.key.gsCookie = DxvkShader::getCookie(shaders.gs);
    entry.key.fsCookie = DxvkShader::getCookie(shaders.fs);
    entry.key.stateHash = state.hash();
    entry.state = state;

    { std::unique_lock lock(m_mutex);
      m_entries[entry.key].push_back(state);
    }

    { std::unique_lock lock(m_writeMutex);
      m_writeQueue.push(entry);
      m_writeCond.notify_one();
    }
  }

  void DxvkStateCache::compileCachedStates(
    DxvkGraphicsPipeline*                 pipeline,
    const DxvkGraphicsPipelineShaders&     shaders) {
    if (!isCacheEnabled() || !m_initialized || !pipeline)
      return;

    // Build lookup key from shaders (without state hash)
    DxvkStateCacheKey lookupKey;
    lookupKey.vsCookie = DxvkShader::getCookie(shaders.vs);
    lookupKey.tcsCookie = DxvkShader::getCookie(shaders.tcs);
    lookupKey.tesCookie = DxvkShader::getCookie(shaders.tes);
    lookupKey.gsCookie = DxvkShader::getCookie(shaders.gs);
    lookupKey.fsCookie = DxvkShader::getCookie(shaders.fs);

    std::unique_lock lock(m_mutex);

    // Find all entries matching this shader combo
    for (const auto& entry : m_entries) {
      const DxvkStateCacheKey& key = entry.first;

      // Check if shader cookies match (stateHash will differ)
      if (key.vsCookie == lookupKey.vsCookie &&
          key.tcsCookie == lookupKey.tcsCookie &&
          key.tesCookie == lookupKey.tesCookie &&
          key.gsCookie == lookupKey.gsCookie &&
          key.fsCookie == lookupKey.fsCookie) {

        // Found matching shaders - compile all cached states
        for (const auto& state : entry.second) {
          lock.unlock();
          pipeline->compilePipeline(state);
          lock.lock();
        }
      }
    }
  }

  std::string DxvkStateCache::getCacheFilePath() {
    std::string cachePath = env::getEnvVar("DXVK_STATE_CACHE_PATH");

    if (cachePath.empty()) {
      #ifdef _WIN32
      cachePath = env::getEnvVar("LOCALAPPDATA");
      #endif

      if (cachePath.empty())
        cachePath = env::getEnvVar("XDG_CACHE_HOME");

      if (cachePath.empty()) {
        cachePath = env::getEnvVar("HOME");
        if (!cachePath.empty()) {
          cachePath += env::PlatformDirSlash;
          cachePath += ".cache";
        }
      }

      if (!cachePath.empty()) {
        cachePath += env::PlatformDirSlash;
        cachePath += "dxvk";
      }
    }

    if (cachePath.empty())
      return "";

    std::string exePath = env::getExePath();
    if (exePath.empty())
      return "";

    size_t pathStart = exePath.find_last_of(env::PlatformDirSlash);
    if (pathStart != std::string::npos)
      pathStart = exePath.find_last_of(env::PlatformDirSlash, pathStart);
    if (pathStart == std::string::npos)
      pathStart = 0u;

    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = pathStart; i < exePath.size(); i++)
      hash = (hash ^ uint8_t(exePath[i])) * 0x100000001b3ull;

    return cachePath + env::PlatformDirSlash + 
      str::format(std::hex, std::setw(16), std::setfill('0'), hash) + ".dxvk-state-cache";
  }

  void DxvkStateCache::initializeCache() {
    if (m_cachePath.empty()) {
      Logger::warn("DXVK: State cache path not available");
      return;
    }

    Logger::info(str::format("DXVK: Using state cache: ", m_cachePath));
    loadCacheFromDisk();
    m_initialized = true;
  }

  void DxvkStateCache::loadCacheFromDisk() {
    std::ifstream file(m_cachePath, std::ios::binary);
    if (!file)
      return;

    DxvkStateCacheHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (std::memcmp(header.magic, "DXSC", 4) != 0 || header.version != 1) {
      Logger::warn("DXVK: State cache file invalid or version mismatch");
      return;
    }

    for (uint32_t i = 0; i < header.numEntries && file; i++) {
      CacheEntry entry;
      file.read(reinterpret_cast<char*>(&entry.key), sizeof(entry.key));
      file.read(reinterpret_cast<char*>(&entry.state), sizeof(entry.state));
      
      if (file) {
        std::unique_lock lock(m_mutex);
        m_entries[entry.key].push_back(entry.state);
      }
    }

    Logger::info(str::format("DXVK: Loaded ", header.numEntries, " state cache entries"));
  }

  void DxvkStateCache::saveEntryToDisk(const CacheEntry& entry) {
    // Check if file exists and has header
    bool fileExists = false;
    {
      std::ifstream check(m_cachePath, std::ios::binary);
      fileExists = check && check.peek() != std::ifstream::traits_type::eof();
    }

    std::ofstream file(m_cachePath, std::ios::binary | std::ios::app);
    if (!file)
      return;

    // Write header if new file
    if (!fileExists) {
      DxvkStateCacheHeader header;
      header.entrySize = sizeof(entry.key) + sizeof(entry.state);
      header.numEntries = 0; // Will be counted on load
      file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    file.write(reinterpret_cast<const char*>(&entry.key), sizeof(entry.key));
    file.write(reinterpret_cast<const char*>(&entry.state), sizeof(entry.state));
  }

  void DxvkStateCache::runWriter() {
    env::setThreadName("dxvk-state-cache-writer");

    while (!m_stopWriter) {
      std::unique_lock lock(m_writeMutex);
      m_writeCond.wait(lock, [this]() { return !m_writeQueue.empty() || m_stopWriter; });

      while (!m_writeQueue.empty()) {
        CacheEntry entry = m_writeQueue.front();
        m_writeQueue.pop();
        lock.unlock();

        saveEntryToDisk(entry);

        lock.lock();
      }
    }
  }

  bool DxvkStateCache::isCacheEnabled() const {
    return m_device->config().enableStateCache;
  }

}
