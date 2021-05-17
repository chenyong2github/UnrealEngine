// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/ContextSwitches.h"
#include "Model/ContextSwitchesPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Algo/Sort.h"

namespace TraceServices
{

const FName FContextSwitchProvider::ProviderName = "ContextSwitchProvider";

FContextSwitchProvider::FContextSwitchProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
}

FContextSwitchProvider::~FContextSwitchProvider()
{
	for (const auto& ContextSwitches : Threads)
	{
		delete ContextSwitches.Value;
	}
}

int32 FContextSwitchProvider::GetCoreNumber(uint32 ThreadId, double Time) const
{
	Session.ReadAccessCheck();

	const TPagedArray<FContextSwitch>* ContextSwitches = GetContextSwitches(ThreadId);
	if (!ContextSwitches)
	{
		return -1;
	}

	uint64 PageIndex = Algo::LowerBoundBy(*ContextSwitches, Time, [](const TPagedArrayPage<FContextSwitch>& In) -> uint64
	{
		return GetFirstItem(In)->Start;
	});

	if (PageIndex < GetNum(*ContextSwitches))
	{
		const TPagedArrayPage<FContextSwitch>& Page = GetData(*ContextSwitches)[PageIndex];
		uint64 Index = Algo::LowerBoundBy(Page, Time, [](const FContextSwitch& In) -> uint64
		{
			return In.Start;
		});

		if (Index < GetNum(Page))
		{
			const FContextSwitch& ContextSwitch = GetData(Page)[Index];
			if (ContextSwitch.Start >= Time && ContextSwitch.End < Time)
			{
				return ContextSwitch.CoreNumber;
			}
		}
	}

	return -1;
}

const TPagedArray<FContextSwitch>* FContextSwitchProvider::GetContextSwitches(uint32 ThreadId) const
{
	Session.ReadAccessCheck();

	auto ContextSwitches = Threads.Find(ThreadId);
	return ContextSwitches ? *ContextSwitches : nullptr;
}

void FContextSwitchProvider::Add(uint32 ThreadId, double Start, double End, uint32 CoreNumber)
{
	Session.WriteAccessCheck();

	TPagedArray<FContextSwitch>** ContextSwitches = Threads.Find(ThreadId);
	if (!ContextSwitches)
	{
		ContextSwitches = &Threads.Add(ThreadId, new TPagedArray<FContextSwitch>(Session.GetLinearAllocator(), 4096));
	}

	FContextSwitch& ContextSwitch = (*ContextSwitches)->PushBack();
	ContextSwitch.Start = Start;
	ContextSwitch.End = End;
	ContextSwitch.CoreNumber = CoreNumber;
}

const IContextSwitchProvider& ReadContextSwitchProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IContextSwitchProvider>(FContextSwitchProvider::ProviderName);
}

} // namespace TraceServices
