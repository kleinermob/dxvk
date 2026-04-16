#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>
#include <queue>
#include <string>
#include <atomic>
#include <thread>

#include "dxvk_state_cache_types.h"
#include "dxvk_graphics_state.h"
#include "dxvk_shader.h"

namespace dxvk {

  class DxvkDevice;
  class DxvkPipelineManager;
  class DxvkGraphicsPipeline;
  struct DxvkGraphicsPipelineShaders;

  /**
   * \brief Pipeline state cache
   *
   * Caches pipeline states to disk for pre-compilation
   * on subsequent runs. Reduces stutter on GPUs without GPL.
   */
  class DxvkStateCache {

  public:

    DxvkStateCache(DxvkDevice* device, DxvkPipelineManager* manager);
    ~DxvkStateCache();

    /**
     * \brief Registers a shader for state cache lookup
     * \param [in] shader Newly compiled shader
     */
    void registerShader(const Rc<DxvkShader>& shader);

    /**
     * \brief Adds a graphics pipeline to the cache
     * \param [in] shaders Pipeline shaders
     * \param [in] state Pipeline state
     */
    void addGraphicsPipeline(
      const DxvkGraphicsPipelineShaders& shaders,
      const DxvkGraphicsPipelineStateInfo& state);

    /**
     * \brief Compiles cached pipeline states for a graphics pipeline
     * \param [in] pipeline Graphics pipeline
     * \param [in] shaders Pipeline shaders
     */
    void compileCachedStates(DxvkGraphicsPipeline* pipeline,
      const DxvkGraphicsPipelineShaders& shaders);

    /**
     * \brief Gets default cache file path
     * \returns Path to cache file
     */
    static std::string getCacheFilePath();

  private:

    struct CacheEntry {
      DxvkStateCacheKey key;
      DxvkGraphicsPipelineStateInfo state;
    };

    DxvkDevice*           m_device;
    DxvkPipelineManager*  m_manager;

    std::string           m_cachePath;
    dxvk::mutex           m_mutex;

    std::unordered_map<DxvkStateCacheKey, std::vector<DxvkGraphicsPipelineStateInfo>,
      DxvkHash, DxvkEq> m_entries;

    std::atomic<bool>     m_initialized{false};
    std::atomic<bool>     m_stopWriter{false};

    dxvk::mutex           m_writeMutex;
    dxvk::condition_variable m_writeCond;
    std::queue<CacheEntry>  m_writeQueue;
    dxvk::thread          m_writerThread;

    void initializeCache();
    void loadCacheFromDisk();
    void saveEntryToDisk(const CacheEntry& entry);
    void runWriter();
    void compilePipelinesForShader(uint32_t shaderCookie);

    bool isCacheEnabled() const;

  };

}
