// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsAnalysis.h"
#include "Model/AllocationsProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsAnalyzer::FAllocationsAnalyzer(IAnalysisSession& InSession, FAllocationsProvider& InAllocationsProvider)
	: Session(InSession)
	, AllocationsProvider(InAllocationsProvider)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsAnalyzer::~FAllocationsAnalyzer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Init,       "Memory", "Init");
	Builder.RouteEvent(RouteId_CoreAdd,    "Memory", "CoreAdd");
	Builder.RouteEvent(RouteId_CoreRemove, "Memory", "CoreRemove");
	Builder.RouteEvent(RouteId_Alloc,      "Memory", "Alloc");
	Builder.RouteEvent(RouteId_Realloc,    "Memory", "Realloc");
	Builder.RouteEvent(RouteId_Free,       "Memory", "Free");
	Builder.RouteEvent(RouteId_Marker,	   "Memory", "Marker");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsAnalyzer::OnAnalysisEnd()
{
	//AllocationsProvider.DebugPrint();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FAllocationsAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
		case RouteId_Init:
		{
			const double Time = GetCurrentTime();
			const uint8 MinAlignment = EventData.GetValue<uint8>("MinAlignment");
			const uint8 SizeShift = EventData.GetValue<uint8>("SizeShift");
			const uint8 SummarySizeShift = EventData.GetValue<uint8>("SummarySizeShift");
			const uint8 Mode = EventData.GetValue<uint8>("Mode");

			BaseCycle = EventData.GetValue<uint64>("BaseCycle");
			MarkerPeriod = EventData.GetValue<uint32>("MarkerPeriod");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditInit(Time, MinAlignment, SizeShift, SummarySizeShift, Mode);
			break;
		}
		case RouteId_CoreAdd:
		{
			const double Time = GetCurrentTime();
			const uint64 Owner = EventData.GetValue<uint64>("Owner");
			const uint64 Base = EventData.GetValue<uint64>("Base");
			const uint32 Size = EventData.GetValue<uint32>("Size");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditAddCore(Time, Owner, Base, Size);
			break;
		}
		case RouteId_CoreRemove:
		{
			const double Time = GetCurrentTime();
			const uint64 Owner = EventData.GetValue<uint64>("Owner");
			const uint64 Base = EventData.GetValue<uint64>("Base");
			const uint32 Size = EventData.GetValue<uint32>("Size");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditRemoveCore(Time, Owner, Base, Size);
			break;
		}
		case RouteId_Alloc:
		{
			const double Time = GetCurrentTime();
			const uint64 Owner = EventData.GetValue<uint64>("Owner");
			const uint64 Address = EventData.GetValue<uint64>("Address");
			const uint32 Size = EventData.GetValue<uint32>("Size");
			const uint8 Alignment = EventData.GetValue<uint8>("Alignment");
			const uint8 Waste = EventData.GetValue<uint8>("Waste");
			uint32 Tag = 0;//TODO

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditAlloc(Time, Owner, Address, Size, Alignment, Waste, Tag);
			break;
		}
		case RouteId_Realloc:
		{
			// TODO: Can we have a struct mapping over the EventData?
			//       something like: const auto& Ev = (const ReallocEvent&)EventData.Get();
			const double Time = GetCurrentTime();
			const uint64 Owner = EventData.GetValue<uint64>("Owner");
			const uint64 FreeAddress = EventData.GetValue<uint64>("FreeAddress");
			const uint64 Address = EventData.GetValue<uint64>("Address");
			const uint32 Size = EventData.GetValue<uint32>("Size");
			const uint8 Alignment = EventData.GetValue<uint8>("Alignment");
			const uint8 Waste = EventData.GetValue<uint8>("Waste");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditRealloc(Time, Owner, FreeAddress, Address, Size, Alignment, Waste);
			break;
		}
		case RouteId_Free:
		{
			const double Time = GetCurrentTime();
			const uint64 Owner = EventData.GetValue<uint64>("Owner");
			const uint64 FreeAddress = EventData.GetValue<uint64>("FreeAddress");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditFree(Time, Owner, FreeAddress);
			break;
		}
		case RouteId_Marker:
		{
			const uint64 LastCycle = BaseCycle + EventData.GetValue<uint32>("Cycle");
			LastMarkerSeconds = Context.EventTime.AsSeconds(LastCycle);
			break;
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double FAllocationsAnalyzer::GetCurrentTime() const
{
	return LastMarkerSeconds;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
