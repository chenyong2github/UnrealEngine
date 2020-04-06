/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @defgroup swappy_common Swappy common tools
 * Tools to be used with Swappy for OpenGL or Swappy for Vulkan.
 * @{
 */

#pragma once

#include <stdint.h>

/** @brief Swap interval for 60fps, in nanoseconds. */
#define SWAPPY_SWAP_60FPS (16666667L)

/** @brief Swap interval for 30fps, in nanoseconds. */
#define SWAPPY_SWAP_30FPS (33333333L)

/** @brief Swap interval for 20fps, in nanoseconds. */
#define SWAPPY_SWAP_20FPS (50000000L)

/** @cond INTERNAL */

#define SWAPPY_SYSTEM_PROP_KEY_DISABLE "swappy.disable"

// Internal macros to track Swappy version, do not use directly.
#define SWAPPY_MAJOR_VERSION 1
#define SWAPPY_MINOR_VERSION 2
#define SWAPPY_PACKED_VERSION ((SWAPPY_MAJOR_VERSION<<16)|(SWAPPY_MINOR_VERSION))

// Internal macros to generate a symbol to track Swappy version, do not use directly.
#define SWAPPY_VERSION_CONCAT_NX(PREFIX, MAJOR, MINOR) PREFIX ## _ ## MAJOR ## _ ## MINOR
#define SWAPPY_VERSION_CONCAT(PREFIX, MAJOR, MINOR) SWAPPY_VERSION_CONCAT_NX(PREFIX, MAJOR, MINOR)
#define SWAPPY_VERSION_SYMBOL SWAPPY_VERSION_CONCAT(Swappy_version, SWAPPY_MAJOR_VERSION, SWAPPY_MINOR_VERSION)

/** @endcond */

/** @brief Id of a thread returned by an external thread manager. */
typedef uint64_t SwappyThreadId;

/**
 * @brief A structure enabling you to set how Swappy starts and joins threads by calling
 * ::Swappy_setThreadFunctions.
 *
 * Usage of this functionality is optional.
 */
typedef struct SwappyThreadFunctions {
    /** @brief Thread start callback.
     *
     * This function is called by Swappy to start thread_func on a new thread.
     * @param user_data A value to be passed the thread function.
     * If the thread was started, this function should set the thread_id and return 0.
     * If the thread was not started, this function should return a non-zero value.
     */
    int (*start)(SwappyThreadId* thread_id, void *(*thread_func)(void*), void* user_data);

    /** @brief Thread join callback.
     *
     * This function is called by Swappy to join the thread with given id.
     */
    void (*join)(SwappyThreadId thread_id);

    /** @brief Thread joinable callback.
     *
     * This function is called by Swappy to discover whether the thread with the given id
     * is joinable.
     */
    bool (*joinable)(SwappyThreadId thread_id);
} SwappyThreadFunctions;

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL */

// Internal function to track Swappy version bundled in a binary. Do not call directly.
// If you are getting linker errors related to Swappy_version_x_y, you probably have a
// mismatch between the header used at compilation and the actually library used by the linker.
void SWAPPY_VERSION_SYMBOL();

/** @endcond */

/**
 * @brief Return the version of the Swappy library at runtime.
 */
uint32_t Swappy_version();

/**
 * @brief Call this before any other functions in order to use a custom thread manager.
 *
 * Usage of this function is entirely optional. Swappy uses std::thread by default.
 *
 */
void Swappy_setThreadFunctions(const SwappyThreadFunctions* thread_functions);

/**
 * @brief Get the supported refresh rates of this device. Up to allocated_entries refresh 
 * rates will be filled in, and the total number of rates suported is returned.
 * 
 */
int Swappy_getSupportedRefreshRates(uint64_t *out_refreshrates, int allocated_entries);

#ifdef __cplusplus
}  // extern "C"
#endif

/**
 * Pointer to a function that can be attached to SwappyTracer::preWait or SwappyTracer::postWait.
 * @param userData Pointer to arbitrary data, see SwappyTracer::userData.
 */
typedef void (*SwappyWaitCallback)(void*);

/**
 * Pointer to a function that can be attached to SwappyTracer::preSwapBuffers.
 * @param userData Pointer to arbitrary data, see SwappyTracer::userData.
 */
typedef void (*SwappyPreSwapBuffersCallback)(void*);

/**
 * Pointer to a function that can be attached to SwappyTracer::postSwapBuffers.
 * @param userData Pointer to arbitrary data, see SwappyTracer::userData.
 * @param desiredPresentationTimeMillis The target time, in milliseconds, at which the frame
 * would be presented on screen.
 */
typedef void (*SwappyPostSwapBuffersCallback)(void*, long desiredPresentationTimeMillis);

/**
 * Pointer to a function that can be attached to SwappyTracer::startFrame.
 * @param userData Pointer to arbitrary data, see SwappyTracer::userData.
 * @param desiredPresentationTimeMillis The time, in milliseconds, at which the frame is scheduled to be presented.
 */
typedef void (*SwappyStartFrameCallback)(void*, int currentFrame, long desiredPresentationTimeMillis);

/**
 * Pointer to a function that can be attached to SwappyTracer::swapIntervalChanged.
 * Call ::SwappyGL_getSwapIntervalNS or ::SwappyVk_getSwapIntervalNS to get the latest swapInterval.
 * @param userData Pointer to arbitrary data, see SwappyTracer::userData.
 */
typedef void (*SwappySwapIntervalChangedCallback)(void*);

/**
 * @brief Collection of callbacks to be called each frame to trace execution.
 *
 * Injection of these is optional.
 */
typedef struct SwappyTracer {
    /**
     * Callback called before waiting to queue the frame to the composer.
     */
    SwappyWaitCallback preWait;

    /**
     * Callback called after wait to queue the frame to the composer is done.
     */
    SwappyWaitCallback postWait;

    /**
     * Callback called before calling the function to queue the frame to the composer.
     */
    SwappyPreSwapBuffersCallback preSwapBuffers;

    /**
     * Callback called after calling the function to queue the frame to the composer.
     */
    SwappyPostSwapBuffersCallback postSwapBuffers;

    /**
     * Callback called at the start of a frame.
     */
    SwappyStartFrameCallback startFrame;

    /**
     * Pointer to some arbitrary data that will be passed as the first argument
     * of callbacks.
     */
    void* userData;

    /**
     * Callback called when the swap interval was changed.
     */
    SwappySwapIntervalChangedCallback swapIntervalChanged;
} SwappyTracer;

/** @} */
