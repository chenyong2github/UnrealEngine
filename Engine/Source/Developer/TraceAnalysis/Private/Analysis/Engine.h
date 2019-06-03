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
						FAnalysisEngine(TArray<IAnalyzer*>&& InAnalyzers);
						~FAnalysisEngine();
	bool				OnData(FStreamReader::FData& Data);

private:
	struct FDispatch
	{
		struct FField
		{
			uint32		Hash;
			uint16		Offset;
			uint16		Size;
			uint16		_Unused0;
			uint8		TypeInfo;
			uint8		_Unused1;
		};

		uint16			FirstRoute;
		uint16			FieldCount;
		uint16			EventSize;
		uint16			_Unused0;
		FField			Fields[];
	};

	struct FRoute
	{
		uint16			HashIndex;
		int16			Count;
		uint16			Id;
		uint16			AnalyzerIndex;
	};

	struct				FEventDataImpl;

	virtual void		OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void		OnAnalysisEnd() override;
	virtual void		OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

	bool				EstablishTransport(FStreamReader::FData& Data);
	FDispatch&			AddDispatch(uint16 Uid, uint16 FieldCount);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, const ANSICHAR* Logger, const ANSICHAR* Event);
	void				AddRoute(uint16 AnalyzerIndex, uint16 Id, uint32 Hash);
	void				OnNewTrace(const FOnEventContext& Context);
	void				OnTiming(const FOnEventContext& Context);
	void				OnNewEvent(const FOnEventContext& Context);
	FSessionContext		SessionContext;
	TArray<uint32>		Hashes;
	TArray<FRoute>		Routes;
	TArray<IAnalyzer*>	Analyzers;
	TArray<FDispatch*>	Dispatches;
	FTransportReader*	Transport = nullptr;
	FEventDataImpl*		EventDataImpl;
};

} // namespace Trace
