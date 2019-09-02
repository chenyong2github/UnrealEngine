// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "DataStream.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"

class IFileHandle;

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
struct FNewEventEvent;
class FTransportReader;

////////////////////////////////////////////////////////////////////////////////
class FAnalysisEngine
	: public IAnalyzer
{
public:
	struct				FDispatch;
	struct				FEventDataInfo;
						FAnalysisEngine(TArray<IAnalyzer*>&& InAnalyzers);
						~FAnalysisEngine();
	bool				OnData(FStreamReader::FData& Data);

private:
	struct FRoute
	{
		uint32			Hash;
		int16			Count;
		uint16			Id;
		uint16			AnalyzerIndex;
		uint16			_Unused0;
	};

	virtual void		OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

	bool				EstablishTransport(FStreamReader::FData& Data);
	FDispatch&			AddDispatch(uint16 Uid, uint16 FieldCount=0, uint16 ExtraData=0);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, const ANSICHAR* Logger, const ANSICHAR* Event);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, uint32 Hash);
	void				OnNewTrace(const FOnEventContext& Context);
	void				OnTiming(const FOnEventContext& Context);
	void				OnNewEventInternal(const FOnEventContext& Context);
	FSessionContext		SessionContext;
	TArray<FRoute>		Routes;
	TArray<IAnalyzer*>	Analyzers;
	TArray<FDispatch*>	Dispatches;
	FTransportReader*	Transport = nullptr;
};

} // namespace Trace
