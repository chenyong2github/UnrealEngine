// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/PagedArray.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
#include "TraceServices/Model/MetadataProvider.h"

namespace TraceServices
{

class IAnalysisSession;
class ILinearAllocator;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMetadataProviderLock
{
public:
	void ReadAccessCheck() const;
	void WriteAccessCheck() const;

	void BeginRead();
	void EndRead();

	void BeginWrite();
	void EndWrite();

private:
	FRWLock RWLock;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMetadataProvider : public IMetadataProvider
{
public:
	static constexpr uint32 MaxMetadataSize = 0xFFFF;
	static constexpr uint32 InvalidMetadataId = 0xFFFFFFFF;
	static constexpr uint16 InvalidMetadataType = 0xFFFF;

private:
	static constexpr uint32 MaxInlinedMetadataSize = 12;
	static constexpr uint32 MaxMetadataTypeId = 0xFFFF;
	static constexpr int32 MaxMetadataStackSize = 0xFF;
	static constexpr uint32 InvalidMetadataStoreIndex = 0xFFFFFFFF;

	struct FMetadataType
	{
		const TCHAR* Name;
		uint32 Size; // 0 for variable size data
	};

	struct FMetadataStoreEntry
	{
		union
		{
			void* Ptr;
			uint8 Value[8];
		};
		uint8 ValueEx[4];
		uint16 Size;
		uint16 Type;
	};
	static_assert(sizeof(FMetadataStoreEntry) == 16, "sizeof(FMetadataStoreEntry)");

	struct FMetadataStackEntry
	{
		FMetadataStoreEntry StoreEntry;
		uint32 StoreIndex;
		uint32 PinnedId;
	};
	static_assert(sizeof(FMetadataStackEntry) == 24, "sizeof(FMetadataStackEntry)");

	struct FMetadataEntry
	{
		uint32 StoreIndex;
		uint16 Type;
		uint8 StackSize;
		uint8 Unused;
	};
	static_assert(sizeof(FMetadataEntry) == 8, "sizeof(FMetadataEntry)");

	struct FMetadataThread
	{
		FMetadataThread(uint32 InThreadId, ILinearAllocator&);

		uint32 ThreadId;
		TArray<FMetadataStackEntry> CurrentStack;
		TPagedArray<FMetadataEntry> Metadata; // a metadata id is an index in this array
	};

public:
	explicit FMetadataProvider(IAnalysisSession& InSession);
	virtual ~FMetadataProvider();

	virtual void BeginEdit() const override { Lock.BeginWrite(); }
	virtual void EndEdit() const override { Lock.EndWrite(); }
	void EditAccessCheck() const { return Lock.WriteAccessCheck(); }

	virtual void BeginRead() const override { Lock.BeginRead(); }
	virtual void EndRead() const override { Lock.EndRead(); }
	void ReadAccessCheck() const { return Lock.ReadAccessCheck(); }

	//////////////////////////////////////////////////
	// Edit operations

	uint16 RegisterMetadataType(const TCHAR* Name, uint32 FixedSize = 0);

	void PushScopedMetadata(uint32 ThreadId, uint16 Type, void* Data, uint32 Size);
	void PopScopedMetadata(uint32 ThreadId, uint16 Type);

	// Pins the metadata stack and returns an id for it.
	uint32 PinAndGetId(uint32 ThreadId);

	//////////////////////////////////////////////////
	// Read operations

	virtual uint16 GetRegisteredMetadataType(const TCHAR* Name) const override;
	virtual const TCHAR* GetRegisteredMetadataName(uint16 Type) const override;

	virtual uint32 GetMetadataStackSize(uint32 InThreadId, uint32 InMetadataId) const override;
	virtual bool GetMetadata(uint32 InThreadId, uint32 InMetadataId, uint32 InStackDepth, uint16& OutType, const void*& OutData, uint32& OutSize) const override;
	virtual void EnumerateMetadata(uint32 InThreadId, uint32 InMetadataId, TFunctionRef<bool(uint32 StackDepth, uint16 Type, const void* Data, uint32 Size)> Callback) const override;

private:
	FMetadataThread& GetOrAddThread(uint32 ThreadId);
	FMetadataThread* GetThread(uint32 ThreadId);
	const FMetadataThread* GetThread(uint32 ThreadId) const;

private:
	IAnalysisSession& Session;

	mutable FMetadataProviderLock Lock;

	TArray<FMetadataType> RegisteredTypes;
	TMap<const TCHAR*, uint16> RegisteredTypesMap;

	TPagedArray<FMetadataStoreEntry> MetadataStore; // stores individual metadata values

	TMap<uint32, FMetadataThread*> Threads;

	uint64 EventCount = 0; // debug
	uint64 AllocationEventCount = 0; // debug
	uint64 AllocationCount = 0; // debug
	uint64 TotalAllocatedMemory = 0; // debug
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
