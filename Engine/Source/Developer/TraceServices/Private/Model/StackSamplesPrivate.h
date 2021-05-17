// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/StackSamples.h"
#include "Containers/Map.h"

namespace TraceServices
{

class FStackSampleProvider
	: public IStackSampleProvider
{
public:
	static const FName ProviderName;
	FStackSampleProvider(IAnalysisSession& Session);
	~FStackSampleProvider();

	const TPagedArray<FStackSample>* GetStackSamples(uint32 ThreadId) const override;

	void Add(uint32 ThreadId, double Time, uint32 Count, const uint64* Addresses);

private:
	IAnalysisSession& Session;
	TMap<uint32, TPagedArray<FStackSample>*> Threads;
	TPagedArray<uint64> AddressValues;
};

} // namespace TraceServices
