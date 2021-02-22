// Copyright Epic Games, Inc. All Rights Reserved.

#include "dna/stream/StreamReaderImpl.h"

#include "dna/TypeDefs.h"
#include "dna/types/Limits.h"

#include <status/Provider.h>
#include <trio/utils/StreamScope.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cstddef>
#include <limits>
#include <tuple>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace dna {

namespace {

constexpr std::size_t operator"" _KB(unsigned long long size) {
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wuseless-cast"
    #endif
    return static_cast<std::size_t>(size * 1024ul);
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
}

constexpr std::size_t operator"" _MB(unsigned long long size) {
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wuseless-cast"
    #endif
    return static_cast<std::size_t>(size * 1024ul * 1024ul);
    #if !defined(__clang__) && defined(__GNUC__)
        #pragma GCC diagnostic pop
    #endif
}

struct ArenaFactory {

    #ifdef _MSC_VER
        struct AllocationOverhead {
            enum Sizes : std::size_t {
                // All and Geometry denote overheads relative to the stream size itself
                All = 20_MB,
                Geometry = 16_MB,
                // Rest of the overheads are absolute values
                AllWithoutBlendShapes = 30_MB,
                GeometryWithoutBlendShapes = 26_MB,
                Behavior = 5_MB,
                Definition = 256_KB,
                Descriptor = 64_KB
            };
        };
    #else
        struct AllocationOverhead {
            enum Sizes : std::size_t {
                // All and Geometry denote overheads relative to the stream size itself
                All = 16_MB,
                Geometry = 12_MB,
                // Rest of the overheads are absolute values
                AllWithoutBlendShapes = 26_MB,
                GeometryWithoutBlendShapes = 22_MB,
                Behavior = 5_MB,
                Definition = 256_KB,
                Descriptor = 64_KB
            };
        };
    #endif

    static MemoryResource* create(dna::DataLayer layer, std::size_t streamSize, MemoryResource* upstream) {
        // upstream may be nullptr, so we rely on PolyAllocator's fallback to a default memory resource in such cases
        auto createArena = [upstream](std::size_t initialSize, std::size_t regionSize) {
                // In the unlikely case that the arena runs out of memory, this growth factor will prevent the arena
                // to be stuck in an infinite loop for allocations of single chunks that are greater than regionSize
                static constexpr float arenaGrowthFactor = 1.1f;
                PolyAllocator<ArenaMemoryResource> alloc{upstream};
                return alloc.newObject(initialSize, regionSize, arenaGrowthFactor, alloc.getMemoryResource());
            };
        if (layer == dna::DataLayer::All) {
            return createArena(streamSize + AllocationOverhead::All, 4_MB);
        } else if (layer == dna::DataLayer::Geometry) {
            return createArena(streamSize + AllocationOverhead::Geometry, 4_MB);
        } else if (layer == dna::DataLayer::AllWithoutBlendShapes) {
            return createArena(AllocationOverhead::AllWithoutBlendShapes, 2_MB);
        } else if (layer == dna::DataLayer::GeometryWithoutBlendShapes) {
            return createArena(AllocationOverhead::GeometryWithoutBlendShapes, 2_MB);
        } else if (layer == dna::DataLayer::Behavior) {
            return createArena(AllocationOverhead::Behavior, 2_MB);
        } else if (layer == dna::DataLayer::Definition) {
            return createArena(AllocationOverhead::Definition, 64_KB);
        } else if (layer == dna::DataLayer::Descriptor) {
            return createArena(AllocationOverhead::Descriptor, 64_KB);
        }
        // Unreachable, unless a new layer is added
        return createArena(streamSize, 4_MB);
    }

    static void destroy(MemoryResource* instance) {
        auto arena = static_cast<ArenaMemoryResource*>(instance);
        PolyAllocator<ArenaMemoryResource> alloc{arena->getUpstreamMemoryResource()};
        alloc.deleteObject(arena);
    }

};

}  // namespace

const sc::StatusCode StreamReader::SignatureMismatchError{200, "DNA signature mismatched, expected %.3s, got %.3s"};
const sc::StatusCode StreamReader::VersionMismatchError{201, "DNA version mismatched, expected %hu.%hu, got %hu.%hu"};
const sc::StatusCode StreamReader::InvalidDataError{202, "Invalid data in DNA"};

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
sc::StatusProvider StreamReaderImpl::status{SignatureMismatchError, VersionMismatchError, InvalidDataError};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

StreamReader::~StreamReader() = default;

StreamReader* StreamReader::create(BoundedIOStream* stream, DataLayer layer, std::uint16_t maxLOD, MemoryResource* memRes) {
    if (maxLOD == LODLimits::max()) {
        #if !defined(__clang__) && defined(__GNUC__)
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wuseless-cast"
        #endif
        memRes = ArenaFactory::create(layer, static_cast<std::size_t>(stream->size()), memRes);
        #if !defined(__clang__) && defined(__GNUC__)
            #pragma GCC diagnostic pop
        #endif
    }
    PolyAllocator<StreamReaderImpl> alloc{memRes};
    return alloc.newObject(stream, layer, maxLOD, LODLimits::min(), memRes);
}

StreamReader* StreamReader::create(BoundedIOStream* stream,
                                   DataLayer layer,
                                   std::uint16_t maxLOD,
                                   std::uint16_t minLOD,
                                   MemoryResource* memRes) {
    PolyAllocator<StreamReaderImpl> alloc{memRes};
    return alloc.newObject(stream, layer, maxLOD, minLOD, memRes);
}

StreamReader* StreamReader::create(BoundedIOStream* stream,
                                   DataLayer layer,
                                   std::uint16_t* lods,
                                   std::uint16_t lodCount,
                                   MemoryResource* memRes) {
    PolyAllocator<StreamReaderImpl> alloc{memRes};
    return alloc.newObject(stream, layer, ConstArrayView<std::uint16_t>{lods, lodCount}, memRes);
}

void StreamReader::destroy(StreamReader* instance) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto reader = static_cast<StreamReaderImpl*>(instance);
    // In the presence of LOD constraints, ArenaMemoryResource is not used,
    // as the approximations for the memory overhead wouldn't be accurate
    const bool usesArena = !(reader->isLODConstrained());
    MemoryResource* memRes = reader->getMemoryResource();
    PolyAllocator<StreamReaderImpl> readerAlloc{memRes};
    readerAlloc.deleteObject(reader);
    // Delete DNA arena if it was used
    if (usesArena) {
        auto arena = static_cast<ArenaMemoryResource*>(memRes);
        auto upstream = arena->getUpstreamMemoryResource();
        PolyAllocator<ArenaMemoryResource> arenaAlloc{upstream};
        arenaAlloc.deleteObject(arena);
    }
}

StreamReaderImpl::StreamReaderImpl(BoundedIOStream* stream_,
                                   DataLayer layer_,
                                   std::uint16_t maxLOD_,
                                   std::uint16_t minLOD_,
                                   MemoryResource* memRes_) :
    BaseImpl{memRes_},
    ReaderImpl{memRes_},
    stream{stream_},
    dnaInputArchive{stream_, layer_, maxLOD_, minLOD_, memRes_},
    lodConstrained{(maxLOD_ != LODLimits::max()) || (minLOD_ != LODLimits::min())} {
}

StreamReaderImpl::StreamReaderImpl(BoundedIOStream* stream_,
                                   DataLayer layer_,
                                   ConstArrayView<std::uint16_t> lods_,
                                   MemoryResource* memRes_) :
    BaseImpl{memRes_},
    ReaderImpl{memRes_},
    stream{stream_},
    dnaInputArchive{stream_, layer_, lods_, memRes_},
    lodConstrained{true} {
}

bool StreamReaderImpl::isLODConstrained() const {
    return lodConstrained;
}

void StreamReaderImpl::read() {
    // Due to possible usage of custom stream implementations, the status actually must be cleared at this point
    // as external streams do not have access to the status reset API
    status.reset();

    trio::StreamScope scope{stream};
    if (!sc::Status::isOk()) {
        return;
    }

    dnaInputArchive >> dna;
    if (!sc::Status::isOk()) {
        return;
    }

    if (!dna.signature.matches()) {
        status.set(SignatureMismatchError, dna.signature.value.expected.data(), dna.signature.value.got.data());
        return;
    }
    if (!dna.version.matches()) {
        status.set(VersionMismatchError,
                   dna.version.generation.expected,
                   dna.version.version.expected,
                   dna.version.generation.got,
                   dna.version.version.got);
        return;
    }
}

}  // namespace dna
