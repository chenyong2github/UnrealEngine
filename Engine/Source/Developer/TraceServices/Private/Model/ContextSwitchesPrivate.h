// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/ContextSwitches.h"
#include "Containers/Map.h"

namespace TraceServices
{

class FContextSwitchProvider
	: public IContextSwitchProvider
{
public:
	static const FName ProviderName;
	FContextSwitchProvider(IAnalysisSession& Session);
	~FContextSwitchProvider();

	int32 GetCoreNumber(uint32 ThreadId, double Time) const override;
	const TPagedArray<FContextSwitch>* GetContextSwitches(uint32 ThreadId) const override;

	void Add(uint32 ThreadId, double Start, double End, uint32 CoreNumber);

private:
	IAnalysisSession& Session;
	TMap<uint32, TPagedArray<FContextSwitch>*> Threads;
};

} // namespace TraceServices
