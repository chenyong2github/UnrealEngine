// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Processor.h"
#include "DataStream.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "StreamReader.h"
#include "Templates/UnrealTemplate.h"
#include "Trace/Analysis.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAnalysisProcessor::FImpl::FImpl(IInDataStream& InDataStream, TArray<IAnalyzer*>&& InAnalyzers)
: AnalysisEngine(Forward<TArray<IAnalyzer*>>(InAnalyzers))
, DataStream(InDataStream)
, StopEvent(FPlatformProcess::GetSynchEventFromPool(true))
, UnpausedEvent(FPlatformProcess::GetSynchEventFromPool(true))
{
	Thread = FRunnableThread::Create(this, TEXT("TraceAnalysis"));
	PauseAnalysis(false);
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisProcessor::FImpl::~FImpl()
{
	StopAnalysis();
	FPlatformProcess::ReturnSynchEventToPool(UnpausedEvent);
	FPlatformProcess::ReturnSynchEventToPool(StopEvent);
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAnalysisProcessor::FImpl::Run()
{
	FStreamBuffer Buffer;

	while (!StopEvent->Wait(0, true))
	{
		UnpausedEvent->Wait();

		int32 BytesRead = Buffer.Fill([&] (uint8* Out, uint32 Size)
		{
			return DataStream.Read(Out, Size);
		});

		if (BytesRead < 0)
		{
			break;
		}

		if (!AnalysisEngine.OnData(Buffer))
		{
			break;
		}

		if (Buffer.IsEof())
		{
			break;
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
bool FAnalysisProcessor::FImpl::IsActive() const
{
	return (Thread != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisProcessor::FImpl::StopAnalysis()
{
	if (IsActive())
	{
		StopEvent->Trigger();
		WaitOnAnalysis();
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisProcessor::FImpl::WaitOnAnalysis()
{
	if (IsActive())
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAnalysisProcessor::FImpl::PauseAnalysis(bool bState)
{
	if (IsActive())
	{
		bState ? UnpausedEvent->Reset() : UnpausedEvent->Trigger();
	}
}



////////////////////////////////////////////////////////////////////////////////
bool FAnalysisProcessor::IsActive() const	{ return (Impl != nullptr) ? Impl->IsActive() : false; }
void FAnalysisProcessor::Stop()				{ if (Impl != nullptr) { Impl->StopAnalysis(); } }
void FAnalysisProcessor::Wait()				{ if (Impl != nullptr) { Impl->WaitOnAnalysis(); } }
void FAnalysisProcessor::Pause(bool bState) { if (Impl != nullptr) { Impl->PauseAnalysis(bState); } }

////////////////////////////////////////////////////////////////////////////////
FAnalysisProcessor::FAnalysisProcessor(FAnalysisProcessor&& Rhs)
{
	Swap(Impl, Rhs.Impl);
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisProcessor::~FAnalysisProcessor()
{
	delete Impl;
}

} // namespace Trace
