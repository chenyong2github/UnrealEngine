// Copyright Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/StackSamples.h"
#include "Model/StackSamplesPrivate.h"
#include "AnalysisServicePrivate.h"
#include "Algo/Sort.h"

namespace TraceServices
{

const FName FStackSampleProvider::ProviderName = "StackSampleProvider";

FStackSampleProvider::FStackSampleProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, AddressValues(Session.GetLinearAllocator(), 4096)
{
}

FStackSampleProvider::~FStackSampleProvider()
{
	for (const auto& ThreadSamples : Threads)
	{
		delete ThreadSamples.Value;
	}
}

const TPagedArray<FStackSample>* FStackSampleProvider::GetStackSamples(uint32 ThreadId) const
{
	Session.ReadAccessCheck();

	auto StackSamples = Threads.Find(ThreadId);
	return StackSamples ? *StackSamples : nullptr;
}

void FStackSampleProvider::Add(uint32 ThreadId, double Time, uint32 Count, const uint64* Addresses)
{
	Session.WriteAccessCheck();

	TPagedArray<FStackSample>** StackSamples = Threads.Find(ThreadId);
	if (!StackSamples)
	{
		StackSamples = &Threads.Add(ThreadId, new TPagedArray<FStackSample>(Session.GetLinearAllocator(), 4096));
	}
	
	FStackSample& StackSample = (*StackSamples)->PushBack();
	StackSample.Time = Time;
	StackSample.Count = Count;

	if (AddressValues.NumPages() == 0)
	{
		for (uint32 Index = 0; Index < Count; Index++)
		{
			AddressValues.EmplaceBack(Addresses[Index]);
		}
		StackSample.Addresses = &AddressValues.First();
		return;
	}

	uint64 Available = AddressValues.GetPageSize() - AddressValues.GetLastPage()->Count;
	if (Available < Count)
	{
		for (uint64 Temp = 0; Temp < Available; Temp++)
		{
			AddressValues.PushBack();
		}
	}

	for (uint32 Index = 0; Index < Count; Index++)
	{
		AddressValues.EmplaceBack(Addresses[Index]);
	}
	StackSample.Addresses = &AddressValues.Last() - Count;
}

const IStackSampleProvider& ReadStackSampleProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IStackSampleProvider>(FStackSampleProvider::ProviderName);
}

} // namespace TraceServices
