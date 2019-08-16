// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine.h"
#include "Trace/Analysis.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
void FAnalysisContext::AddAnalyzer(IAnalyzer& Analyzer)
{
	Analyzers.Add(&Analyzer);
}

////////////////////////////////////////////////////////////////////////////////
FAnalysisProcessor FAnalysisContext::Process()
{
	FAnalysisProcessor Processor;

	if (Analyzers.Num())
	{
		Processor.Impl = new FAnalysisEngine(MoveTemp(Analyzers));
	}

	return MoveTemp(Processor);
}

} // namespace Trace
