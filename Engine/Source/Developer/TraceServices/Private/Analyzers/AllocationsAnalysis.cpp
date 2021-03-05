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
	, BaseCycle(0)
	, MarkerPeriod(0)
	, LastMarkerCycle(0)
	, LastMarkerSeconds(0.0)
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

	Builder.RouteEvent(RouteId_Init,         "Memory", "Init");
	Builder.RouteEvent(RouteId_CoreAdd,      "Memory", "CoreAdd");
	Builder.RouteEvent(RouteId_CoreRemove,   "Memory", "CoreRemove");
	Builder.RouteEvent(RouteId_Alloc,        "Memory", "Alloc");
	Builder.RouteEvent(RouteId_Free,         "Memory", "Free");
	Builder.RouteEvent(RouteId_ReallocAlloc, "Memory", "ReallocAlloc");
	Builder.RouteEvent(RouteId_ReallocFree,  "Memory", "ReallocFree");
	Builder.RouteEvent(RouteId_Marker,       "Memory", "Marker");
	Builder.RouteEvent(RouteId_TagSpec,      "Memory", "TagSpec");

	Builder.RouteLoggerEvents(RouteId_MemScope, "Memory", true);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsAnalyzer::OnAnalysisEnd()
{
	double SessionTime;
	{
		FAnalysisSessionEditScope _(Session);
		SessionTime = Session.GetDurationSeconds();
		if (LastMarkerSeconds > SessionTime)
		{
			Session.UpdateDurationSeconds(LastMarkerSeconds);
		}
	}
	const double Time = FMath::Max(SessionTime, LastMarkerSeconds);

	FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
	AllocationsProvider.EditOnAnalysisCompleted(Time);
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

			BaseCycle = EventData.GetValue<uint64>("BaseCycle", 0);
			MarkerPeriod = EventData.GetValue<uint32>("MarkerPeriod");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditInit(Time, MinAlignment, SizeShift, SummarySizeShift);
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
		case RouteId_ReallocAlloc:
		{
			// TODO: Can we have a struct mapping over the EventData?
			//       something like: const auto& Ev = (const AllocEvent&)EventData.Get();
			const double Time = GetCurrentTime();
			const uint64 Owner = EventData.GetValue<uint64>("Owner");
			const uint64 Address = EventData.GetValue<uint64>("Address");
			const uint32 Size = EventData.GetValue<uint32>("Size");
			const uint8 Alignment_SizeLower = EventData.GetValue<uint8>("Alignment_SizeLower");

			const uint32 ThreadId = Context.ThreadInfo.GetSystemId();
			const uint8 Tracker = 0; // We only care about the default tracker for now.

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditAlloc(Time, Owner, Address, Size, Alignment_SizeLower, ThreadId, Tracker);
			break;
		}
		case RouteId_Free:
		case RouteId_ReallocFree:
		{
			const double Time = GetCurrentTime();
			const uint64 Address = EventData.GetValue<uint64>("Address");

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditFree(Time, Address);
			break;
		}
		case RouteId_Marker:
		{
			// If BaseCycle is set Cycle is a relative 32-bit value, otherwise a 64-bit absolute value.
			const uint64 Cycle = BaseCycle + (BaseCycle > 0 ? EventData.GetValue<uint32>("Cycle") : EventData.GetValue<uint64>("Cycle"));

			if (ensure(Cycle >= LastMarkerCycle))
			{
				const double Seconds = Context.EventTime.AsSeconds(Cycle);
				check(Seconds >= LastMarkerSeconds);
				if (ensure((Seconds - LastMarkerSeconds < 60.0) || LastMarkerSeconds == 0.0f))
				{
					LastMarkerCycle = Cycle;
					LastMarkerSeconds = Seconds;
					{
						FAnalysisSessionEditScope _(Session);
						double SessionTime = Session.GetDurationSeconds();
						if (LastMarkerSeconds > SessionTime)
						{
							Session.UpdateDurationSeconds(LastMarkerSeconds);
						}
					}
				}
			}
			break;
		}
		case RouteId_TagSpec:
		{
			const int32 Tag = Context.EventData.GetValue<int32>("Tag");
			const int32 Parent = Context.EventData.GetValue<int32>("Parent");

			FString Display;
			Context.EventData.GetString("Display", Display);
			const TCHAR* DisplayString = Session.StoreString(*Display);

			FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
			AllocationsProvider.EditAddTagSpec(Tag, Parent, DisplayString);
			break;
		}
		case RouteId_MemScope:
		{
			const uint32 ThreadId = Context.ThreadInfo.GetSystemId();
			const uint8 Tracker = 0; // We only care about the default tracker for now.

			if (Style == EStyle::EnterScope)
			{
				if (FCStringAnsi::Strlen(Context.EventData.GetTypeInfo().GetName()) == 11) // "MemoryScope"
				{
					const int32 Tag = Context.EventData.GetValue<int32>("Tag");
					FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
					AllocationsProvider.EditPushTag(ThreadId, Tracker, Tag);
				}
				else // "MemoryScopeRealloc"
				{
					const uint64 Ptr = Context.EventData.GetValue<int32>("Ptr");
					FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
					AllocationsProvider.EditPushRealloc(ThreadId, Tracker, Ptr);
				}
			}
			else // EStyle::LeaveScope
			{
				FAllocationsProvider::FEditScopeLock _(AllocationsProvider);
				if (AllocationsProvider.HasReallocScope(ThreadId, Tracker)) // Is realloc scope active?
				{
					AllocationsProvider.EditPopRealloc(ThreadId, Tracker);
				}
				else
				{
					AllocationsProvider.EditPopTag(ThreadId, Tracker);
				}
			}
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
