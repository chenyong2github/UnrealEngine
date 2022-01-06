// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if WITH_EDITORONLY_DATA

#include "Containers/StringFwd.h"
#include "Memory/MemoryFwd.h"
#include "Templates/UniquePtr.h"

class FCompressedBuffer;
class FIoRequestImpl;

namespace UE::DerivedData { class FBuildDefinition; }
namespace UE::DerivedData { class FRequestOwner; }
namespace UE::DerivedData { struct FValueId; }
namespace UE::DerivedData::IoStore { class FDerivedDataIoRequestQueue; }

namespace UE::DerivedData::IoStore
{

class FDerivedDataIoRequest
{
public:
	FDerivedDataIoRequest(FIoRequestImpl* Request, FDerivedDataIoRequestQueue* Queue);
	/** Create a request owner for requests that do not complete immediately. */
	FRequestOwner& GetOwner();
	/** Create a buffer of the given size and return a view to write into it. */
	FMutableMemoryView CreateBuffer(uint64 Size);
	/** Offset to start reading from. */
	uint64 GetOffset() const;
	/** Maximum number of bytes to read. */
	uint64 GetSize() const;
	/** Mark the request as complete. */
	void SetComplete();
	/** Mark the request as failed. */
	void SetFailed();

private:
	FIoRequestImpl* Request;
	FDerivedDataIoRequestQueue* Queue;
};

} // UE::DerivedData::IoStore

namespace UE::DerivedData::Private
{

class FEditorDerivedData
{
public:
	virtual ~FEditorDerivedData() = default;
	virtual TUniquePtr<FEditorDerivedData> Clone() const = 0;
	virtual void Read(IoStore::FDerivedDataIoRequest Request) const = 0;
	virtual bool TryGetSize(uint64& OutSize) const = 0;
};

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FSharedBuffer& Data);
TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FCompositeBuffer& Data);
TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FCompressedBuffer& Data);

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(FStringView CacheKey, FStringView CacheContext);

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(
	const DerivedData::FBuildDefinition& BuildDefinition,
	const DerivedData::FValueId& ValueId);

} // UE::DerivedData::Private

#endif // WITH_EDITORONLY_DATA
