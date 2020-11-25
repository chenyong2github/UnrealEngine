// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AllocationsProvider.h"

namespace TraceServices
{

class ILinearAllocator;
class FSbTree;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FAllocationsProviderLock
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

struct FAllocationItem
{
	uint32 StartEventIndex;
	uint32 EndEventIndex;
	double StartTime;
	double EndTime;
	uint64 Owner;
	uint64 Address;
	//uint64 SizeAlignmentWaste; // 48 bits (Size) + 8 bits (Alignment) + 8 bits (Waste)
	uint32 Size;
	uint8 Alignment;
	uint8 Waste;
	uint16 Tag;
	uint64 Reserved1;
	uint64 Reserved2;
};

static_assert(sizeof(FAllocationItem) == 64, "struct FAllocationItem needs packing");

////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0
struct FAllocationCoreItem
{
	uint32 StartEventIndex;
	uint32 EndEventIndex;
	double StartTime;
	double EndTime;
	uint64 Owner;
	uint64 Base;
	uint32 Size;
};
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////

class FAllocationsProvider : public IAllocationsProvider
{
public:
	struct FEditScopeLock
	{
		FEditScopeLock(FAllocationsProvider& InAllocationsProvider)
			: AllocationsProvider(InAllocationsProvider)
		{
			AllocationsProvider.BeginEdit();
		}

		~FEditScopeLock()
		{
			AllocationsProvider.EndEdit();
		}

		FAllocationsProvider& AllocationsProvider;
	};

	struct FReadScopeLock
	{
		FReadScopeLock(FAllocationsProvider& InAllocationsProvider)
			: AllocationsProvider(InAllocationsProvider)
		{
			AllocationsProvider.BeginRead();
		}

		~FReadScopeLock()
		{
			AllocationsProvider.EndRead();
		}

		FAllocationsProvider& AllocationsProvider;
	};

public:
	FAllocationsProvider(ILinearAllocator& InAllocator);
	virtual ~FAllocationsProvider();

	static FName GetName();

	virtual bool IsInitialized() const override { /*Lock.ReadAccessCheck();*/ return bInitialized; }

	virtual FQueryHandle StartQuery(const FQueryParams& Params) const override;
	virtual void CancelQuery(FQueryHandle Query) const override;
	virtual const FQueryStatus PollQuery(FQueryHandle Query) const override;

	void BeginEdit() { Lock.BeginWrite(); }
	void EndEdit() { Lock.EndWrite(); }
	void EditAccessCheck() const { return Lock.WriteAccessCheck(); }

	void BeginRead() const { Lock.BeginRead(); }
	void EndRead() const { Lock.EndRead(); }
	void ReadAccessCheck() const { return Lock.ReadAccessCheck(); }

	void EditInit(double Time, uint8 MinAlignment, uint8 SizeShift, uint8 SummarySizeShift, uint8 Mode);
	void EditAddCore(double Time, uint64 Owner, uint64 Base, uint32 Size);
	void EditRemoveCore(double Time, uint64 Owner, uint64 Base, uint32 Size);
	void EditAlloc(double Time, uint64 Owner, uint64 Address, uint32 Size, uint8 Alignment, uint8 Waste, uint32 Tag);
	void EditRealloc(double Time, uint64 Owner, uint64 FreeAddress, uint64 Address, uint32 Size, uint8 Alignment, uint8 Waste);
	void EditFree(double Time, uint64 Owner, uint64 FreeAddress);

	void DebugPrint() const;

	const FSbTree* GetSbTree() const { return SbTree; }

private:
	void UpdateHistogramByAllocSize(uint32 Size, uint8 Alignment, uint8 Waste);
	void UpdateHistogramByEventDistance(uint32 EventDistance);

private:
	ILinearAllocator& Allocator;

	mutable FAllocationsProviderLock Lock;

	double InitTime = 0;
	uint8 MinAlignment = 0;
	uint8 SizeShift = 0;
	uint8 SummarySizeShift = 0;
	uint8 Mode = 0;
	bool bInitialized = false;

	uint32 EventIndex = 0;

	uint64 AllocCount = 0;

	uint64 MaxLiveAllocCount = 0;
	TMap<uint64, FAllocationItem> LiveAllocs;

	uint64 AllocErrors = 0;
	uint64 ReallocErrors = 0;
	uint64 FreeErrors = 0;

	uint32 MaxAllocSize = 0;
	uint32 AllocSizeHistogramPow2[33] = { 0 };

	uint32 MaxEventDistance = 0;
	uint32 EventDistanceHistogramPow2[65] = { 0 };

	FSbTree* SbTree;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
