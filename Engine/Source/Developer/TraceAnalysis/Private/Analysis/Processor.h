// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "HAL/Runnable.h"

class FEvent;
class FRunnableThread;

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class FAnalysisProcessor::FImpl
	: public FRunnable
{
public:
						FImpl(IInDataStream& DataStream, TArray<IAnalyzer*>&& InAnalyzers);
						~FImpl();
	virtual uint32		Run() override;
	bool				IsActive() const;
	void				StopAnalysis();
	void				WaitOnAnalysis();
	void				PauseAnalysis(bool bState);

private:
	FAnalysisEngine		AnalysisEngine;
	IInDataStream&		DataStream;
	FEvent*				StopEvent;
	FEvent*				UnpausedEvent;
	FRunnableThread*	Thread = nullptr;
};

} // namespace Trace
