// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/PagedArray.h"
#include "Common/ProviderLock.h"
#include "TraceServices/Model/Regions.h"
#include "Templates/SharedPointer.h"


namespace TraceServices
{
extern thread_local FProviderLock::FThreadLocalState GRegionsProviderLockState;

class FAnalysisSessionLock;
class FStringStore;

class FRegionProvider
	: public IRegionProvider
	, public IEditableRegionProvider
{
public:
	static const FName ProviderName;

	// Provider locking API
	virtual void BeginEdit() const override       { Lock.BeginWrite(GRegionsProviderLockState); }
	virtual void EndEdit() const override         { Lock.EndWrite(GRegionsProviderLockState); }
	virtual void EditAccessCheck() const override { Lock.WriteAccessCheck(GRegionsProviderLockState); }
	virtual void BeginRead() const override       { Lock.BeginRead(GRegionsProviderLockState); }
	virtual void EndRead() const override         { Lock.EndRead(GRegionsProviderLockState); }
	virtual void ReadAccessCheck() const override { Lock.ReadAccessCheck(GRegionsProviderLockState); }

	explicit FRegionProvider(IAnalysisSession& Session);
	virtual ~FRegionProvider() override {}

	// implement IRegionProvider API
	virtual uint64 GetRegionCount() const override;
	
	virtual uint64 GetUpdateCounter() const override { ReadAccessCheck(); return UpdateCounter; }

	virtual bool EnumerateRegions(double IntervalStart, double IntervalEnd, TFunctionRef<bool(const FTimeRegion&)> Callback) const override;
	virtual void EnumerateLanes(TFunctionRef<void(const FRegionLane&, const int32)> Callback) const override;
	virtual int32 GetLaneCount()  const override { ReadAccessCheck(); return Lanes.Num(); }

	/**
	 * Direct Access to a certain lane at a given index/depth.
	 * Do not store the pointer returned from this function as it could be accessed outside the ProviderEditScope.
	 */
	virtual const FRegionLane* GetLane(int32 index) const override;


	// implement IEditableRegionProvider API
	virtual void AppendRegionBegin(const TCHAR* Name, double Time) override;
	virtual void AppendRegionEnd(const TCHAR* Name, double Time) override;
	virtual void OnAnalysisSessionEnded() override;

private:

	mutable FProviderLock Lock;
	
	// Update the depth member of a region to allow overlapping regions to be displayed on separate lanes.
	int32 CalculateRegionDepth(const FTimeRegion& Item) const;
	
	IAnalysisSession& Session;
	// open Regions inside lanes
	TMap<FStringView, FTimeRegion*> OpenRegions;
	// closed regions
	TArray<FRegionLane> Lanes;
	// counter incremented each time region data changes during analysis
	uint64 UpdateCounter = -1;
};

} // namespace TraceServices
