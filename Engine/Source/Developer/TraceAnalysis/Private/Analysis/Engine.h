// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"

namespace Trace
{

class FStreamReader;

////////////////////////////////////////////////////////////////////////////////
class FAnalysisEngine
	: public IAnalyzer
{
public:
	struct				FDispatch;
	struct				FAuxDataCollector;
	struct				FEventDataInfo;
						FAnalysisEngine(TArray<IAnalyzer*>&& InAnalyzers);
						~FAnalysisEngine();
	bool				OnData(FStreamReader& Reader);

private:
	typedef bool (FAnalysisEngine::*ProtocolHandlerType)();

	struct FRoute
	{
		uint32			Hash;
		int16			Count;
		uint16			Id;
		uint16			AnalyzerIndex;
		uint16			_Unused0;
	};

	class				FDispatchBuilder;
	virtual bool		OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	void				OnNewTrace(const FOnEventContext& Context);
	void				OnTiming(const FOnEventContext& Context);
	void				OnNewEventInternal(const FOnEventContext& Context);
	void				OnNewEventProtocol0(FDispatchBuilder& Builder, const void* EventData);
	void				OnNewEventProtocol1(FDispatchBuilder& Builder, const void* EventData);
	void				OnChannelAnnounceInternal(const FOnEventContext& Context);
	void				OnChannelToggleInternal(const FOnEventContext& Context);

	bool				EstablishTransport(FStreamReader& Reader);
	bool				OnDataProtocol0();
	bool				OnDataProtocol2();
	int32				OnDataProtocol2(uint32 ThreadId, FStreamReader& Reader);
	int32				OnDataProtocol2Aux(FStreamReader& Reader, FAuxDataCollector& Collector);
	bool				AddDispatch(FDispatch* Dispatch);
	template <typename ImplType>
	void				ForEachRoute(const FDispatch* Dispatch, ImplType&& Impl);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, const ANSICHAR* Logger, const ANSICHAR* Event);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, uint32 Hash);
	void				RetireAnalyzer(IAnalyzer* Analyzer);
	FSessionContext		SessionContext;
	TArray<FRoute>		Routes;
	TArray<IAnalyzer*>	Analyzers;
	TArray<FDispatch*>	Dispatches;
	class FTransport*	Transport = nullptr;
	ProtocolHandlerType	ProtocolHandler = nullptr;
	uint32				NextLogSerial = 0;
	uint8				ProtocolVersion = 0;
};

} // namespace Trace
