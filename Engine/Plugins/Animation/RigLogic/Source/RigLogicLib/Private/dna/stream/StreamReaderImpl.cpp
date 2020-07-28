// Copyright Epic Games, Inc. All Rights Reserved.

#include "dna/stream/StreamReaderImpl.h"

#include "dna/TypeDefs.h"

#include <status/Provider.h>
#include <trio/utils/StreamScope.h>

#include <cstddef>
#include <limits>
#include <tuple>

namespace dna {

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
    PolyAllocator<StreamReaderImpl> alloc{memRes};
    return alloc.newObject(stream, layer, maxLOD, std::numeric_limits<std::uint16_t>::max(), memRes);
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
    PolyAllocator<StreamReaderImpl> alloc{reader->getMemoryResource()};
    alloc.deleteObject(reader);
}

StreamReaderImpl::StreamReaderImpl(BoundedIOStream* stream_,
                                   DataLayer layer_,
                                   std::uint16_t maxLOD_,
                                   std::uint16_t minLOD_,
                                   MemoryResource* memRes_) :
    BaseImpl{memRes_},
    ReaderImpl{memRes_},
    stream{stream_},
    dnaInputArchive{stream_, layer_, maxLOD_, minLOD_, memRes_} {
}

StreamReaderImpl::StreamReaderImpl(BoundedIOStream* stream_,
                                   DataLayer layer_,
                                   ConstArrayView<std::uint16_t> lods_,
                                   MemoryResource* memRes_) :
    BaseImpl{memRes_},
    ReaderImpl{memRes_},
    stream{stream_},
    dnaInputArchive{stream_, layer_, lods_, memRes_} {
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
