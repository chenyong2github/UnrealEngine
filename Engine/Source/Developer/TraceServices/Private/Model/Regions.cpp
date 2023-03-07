// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Regions.h"
#include "Common/FormatArgs.h"
#include "Model/RegionsPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "RegionProvider"

namespace TraceServices
{

thread_local FProviderLock::FThreadLocalState GRegionsProviderLockState;
	
const FName FRegionProvider::ProviderName("RegionProvider");

FRegionProvider::FRegionProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
}
	
uint64 FRegionProvider::GetRegionCount() const
{
	ReadAccessCheck();

	uint64 RegionCount = 0;
	for (const FRegionLane& Lane : Lanes)
	{
		RegionCount += Lane.Num();
	}
	return RegionCount;
}

const FRegionLane* FRegionProvider::GetLane(int32 index) const
{
	ReadAccessCheck();
	if (index < Lanes.Num())
	{
		return &(Lanes[index]);
	}
	return nullptr;
}

void FRegionProvider::AppendRegionBegin(const TCHAR* Name, double Time)
{
	EditAccessCheck();
	
	FTimeRegion Region;
	Region.BeginTime = Time;
	Region.Text = Session.StoreString(Name);
	Region.Depth = CalculateRegionDepth(Region);

	if (Region.Depth == Lanes.Num())
	{
		Lanes.Emplace(Session.GetLinearAllocator());
	}
	
	Lanes[Region.Depth].Regions.EmplaceBack(Region);
	FTimeRegion* OpenRegion = &(Lanes[Region.Depth].Regions.Last());
	OpenRegions.Add(Region.Text, OpenRegion);

	{
		FAnalysisSessionEditScope _(Session);
		Session.UpdateDurationSeconds(Time);
	}
	
	UpdateCounter++;
}

void FRegionProvider::AppendRegionEnd(const TCHAR* Name, double Time)
{
	EditAccessCheck();

	if (FTimeRegion** OpenRegion = OpenRegions.Find(Name))
	{
		(*OpenRegion)->EndTime = Time;
		OpenRegions.Remove(Name);
		{
			FAnalysisSessionEditScope _(Session);
			Session.UpdateDurationSeconds(Time);
		}
		UpdateCounter++;
	}
	else
	{
		if (FMessageLog* Log = Session.GetLog())
		{
			Log->Warning(LOCTEXT("StrayRegionEnd","A region end event was encountered without having seen a matching region start event first."));
		}
		else
		{
			UE_LOG(LogTraceServices, Warning, TEXT("A region end event (%s) was encountered without having seen a matching region start event first."), Name)
		}
	}
}

void FRegionProvider::OnAnalysisSessionEnded()
{
	EditAccessCheck();
	
	for (const auto& KV : OpenRegions)
	{
		const FTimeRegion* Region = KV.Value;
		if (FMessageLog* Log = Session.GetLog())
		{
			Log->Warning(LOCTEXT("StrayRegionBegin","A region begin event was never closed."));
		}
		else
		{
			UE_LOG(LogTraceServices, Warning, TEXT("A region begin event (%s) was never closed."), Region->Text)
		}		
	}
}

int32 FRegionProvider::CalculateRegionDepth(const FTimeRegion& Region) const
{
	TArray<const FTimeRegion*> Overlaps;

	constexpr int32 DepthLimit = 100;
	int32 NewDepth = 0;
	// find first free lane/depth
	while (NewDepth < DepthLimit)
	{
		if (!Lanes.IsValidIndex(NewDepth))
		{
			break;
		}
		
		const FTimeRegion& LastRegion = Lanes[NewDepth].Regions.Last();
		if (LastRegion.EndTime <= Region.BeginTime)
		{
			break;
		}
		NewDepth++;
	}

	ensureMsgf(NewDepth < DepthLimit, TEXT("Regions are nested too deep."));

	return NewDepth;
}
	
void FRegionProvider::EnumerateLanes(TFunctionRef<void(const FRegionLane&, int32)> Callback) const
{
	for (int i = 0 ; i < Lanes.Num(); ++i)
	{
		Callback(Lanes[i], i);
	}
}
	
bool FRegionProvider::EnumerateRegions(double IntervalStart, double IntervalEnd, TFunctionRef<bool(const FTimeRegion &)> Callback) const
{
	ReadAccessCheck();
	
	if (IntervalStart > IntervalEnd)
	{
		return false;
	}

	for (const FRegionLane& Lane : Lanes)
	{
		if (!Lane.EnumerateRegions(IntervalStart, IntervalEnd, Callback))
			return false;
	}

	return true;
}

bool FRegionLane::EnumerateRegions(double IntervalStart, double IntervalEnd, TFunctionRef<bool(const FTimeRegion&)> Callback) const
{
	const FInt32Interval OverlapRange = GetElementRangeOverlappingGivenRange<FTimeRegion>(Regions, IntervalStart, IntervalEnd,
		[](const FTimeRegion& r){return r.BeginTime;},
		[](const FTimeRegion& r){return r.EndTime;});
	
	if (OverlapRange.Min == -1 )
	{
		return true;
	}
	
	for (int32 Index = OverlapRange.Min; Index <= OverlapRange.Max; ++Index)
	{
		if (!Callback(Regions[Index]))
		{
			return false;
		}
	}

	return true;
}
	
const IRegionProvider& ReadRegionProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IRegionProvider>(FRegionProvider::ProviderName);
}

IEditableRegionProvider& EditRegionProvider(IAnalysisSession& Session)
{
	return *Session.EditProvider<IEditableRegionProvider>(FRegionProvider::ProviderName);
}

} // namespace TraceServices

#undef LOCTEXT_NAMESPACE