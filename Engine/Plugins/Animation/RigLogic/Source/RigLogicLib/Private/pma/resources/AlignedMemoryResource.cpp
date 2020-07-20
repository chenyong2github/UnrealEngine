#include "pma/resources/AlignedMemoryResource.h"

#include <cstddef>
#include <cstdlib>

#if defined(__linux__) || defined(__APPLE__)
    #include <stdlib.h>
#elif defined(_MSC_VER) || defined(__ANDROID__)
    #include <malloc.h>
#endif

#if defined(__cplusplus) && (__cplusplus >= 201703L) && (defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) || \
    defined(_LIBCPP_HAS_ALIGNED_ALLOC) || defined(_LIBCPP_HAS_C11_FEATURES))
    #define ALIGNED_ALLOC(ptr, alignment, size) ptr = std::aligned_alloc(alignment, size)
    #define ALIGNED_FREE(ptr) std::free(ptr)
#elif ((defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)) || defined(__linux__) || defined(__APPLE__))
    #define ALIGNED_ALLOC(ptr, alignment, size) if (::posix_memalign(&(ptr), alignment, size)) \
        (ptr) = nullptr
    #define ALIGNED_FREE(ptr) ::free(ptr)
#elif defined(_MSC_VER)
    #define ALIGNED_ALLOC(ptr, alignment, size) ptr = _aligned_malloc(size, alignment)
    #define ALIGNED_FREE(ptr) _aligned_free(ptr)
#elif defined(__ANDROID__)
    #define ALIGNED_ALLOC(ptr, alignment, size) ptr = ::memalign(alignment, size)
    #define ALIGNED_FREE(ptr) ::free(ptr)
#else
    #error No aligned allocation is possible.
#endif

namespace pma {

void* AlignedMemoryResource::allocate(std::size_t size, std::size_t alignment) {
    void* ptr;
    ALIGNED_ALLOC(ptr, alignment, size);
    return ptr;
}

void AlignedMemoryResource::deallocate(void* ptr, std::size_t  /*unused*/, std::size_t  /*unused*/) {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
    ALIGNED_FREE(ptr);
}

}  // namespace pma
