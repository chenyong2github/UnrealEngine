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

	explicit FContextSwitchProvider(IAnalysisSession& Session);
	virtual ~FContextSwitchProvider();

	int32 GetCoreNumber(uint32 ThreadId, double Time) const override;
	const void EnumerateContextSwitches(uint32 ThreadId, double StartTime, double EndTime, ContextSwitchCallback Callback) const override;

	void Add(uint32 ThreadId, double Start, double End, uint32 CoreNumber);
	void AddThreadInfo(uint32 TraceThreadId, uint32 SystemThreadId);

	virtual bool HasData() const override;

private:
	const TPagedArray<FContextSwitch>* GetContextSwitches(uint32 ThreadId) const;

	IAnalysisSession& Session;
	// System Thread Id -> PagedArray
	TMap<uint32, TPagedArray<FContextSwitch>*> Threads;
	// Trace Thread Id -> System Thread Id
	TMap<uint32, uint32> ThreadIdMap;
};

} // namespace TraceServices
