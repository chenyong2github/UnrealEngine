// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"

namespace Trace
{

class FStreamReader;

////////////////////////////////////////////////////////////////////////////////
class FThreads
{
public:
	struct FInfo
	{
		uint32			ThreadId;
		uint32			SystemId = 0;
		int32			SortHint = 0;
		TArray<uint16>	ScopeRoutes;
		TArray<uint8>	Name;
		TArray<uint8>	GroupName;
	};

						FThreads();
	FInfo*				GetInfo();
	FInfo&				GetInfo(uint32 ThreadId);
	void				SetGroupName(const ANSICHAR* InGroupName, uint32 Length);
	const TArray<uint8>*GetGroupName() const;

private:
	uint32				LastGetInfoId = ~0u;
	TArray<FInfo>		Infos;
	TArray<uint8>		GroupName;
};



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
	void				End();

private:
	typedef bool (FAnalysisEngine::*ProtocolHandlerType)();

	struct FRoute
	{
		uint32			Hash;
		uint16			AnalyzerIndex : 15;
		uint16			bScoped : 1;
		uint16			Id;
		union
		{
			uint32		ParentHash;
			struct
			{
				int16	Count;
				int16	Parent;
			};
		};
	};

	class				FDispatchBuilder;
	void				Begin();
	virtual bool		OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void		OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	void				OnNewTrace(const FOnEventContext& Context);
	void				OnThreadInfoInternal(const FOnEventContext& Context);
	void				OnThreadGroupBegin(const FOnEventContext& Context);
	void				OnThreadGroupEnd();
	void				OnTiming(const FOnEventContext& Context);
	void				OnNewEventInternal(const void* EventData);
	void				OnNewEventProtocol0(FDispatchBuilder& Builder, const void* EventData);
	void				OnNewEventProtocol1(FDispatchBuilder& Builder, const void* EventData);
	bool				EstablishTransport(FStreamReader& Reader);
	bool				OnDataProtocol0();
	bool				OnDataProtocol2();
	int32				OnDataProtocol2(FStreamReader& Reader, FThreads::FInfo& Info);
	int32				OnDataProtocol2Aux(FStreamReader& Reader, FAuxDataCollector& Collector);
	int32				OnDataProtocol4(FStreamReader& Reader, FThreads::FInfo& Info);
	int32				OnDataProtocol4Impl(FStreamReader& Reader, FThreads::FInfo& ThreadInfo, bool bScoped);
	bool				AddDispatch(FDispatch* Dispatch);
	template <typename ImplType>
	void				ForEachRoute(uint32 RouteIndex, bool bScoped, ImplType&& Impl);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, const ANSICHAR* Logger, const ANSICHAR* Event, bool bScoped);
	void				RetireAnalyzer(IAnalyzer* Analyzer);
	FSessionContext		SessionContext;
	TArray<FRoute>		Routes;
	TArray<IAnalyzer*>	Analyzers;
	TArray<FDispatch*>	Dispatches;
	class FTransport*	Transport = nullptr;
	ProtocolHandlerType	ProtocolHandler = nullptr;
	uint32				NextLogSerial = 0;
	uint32				UserUidBias = 1; // 1 because new-event events must exists be uid zero.
	uint8				ProtocolVersion = 0;
	FThreads			Threads;
};

} // namespace Trace
