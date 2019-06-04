// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"

namespace Trace
{

class IAnalyzer;
class IInDataStream;

////////////////////////////////////////////////////////////////////////////////
class TRACEANALYSIS_API FAnalysisProcessor
{
public:
	explicit	operator bool () const;
	void		Start(IInDataStream& DataStream);
	void		Stop();
	void		Wait();
	void		Pause(bool State);

private:
	friend		class FAnalysisContext;
	void*		Impl = nullptr;
};



////////////////////////////////////////////////////////////////////////////////
class TRACEANALYSIS_API FAnalysisContext
{
public:
	void				AddAnalyzer(IAnalyzer& Analyzer);
	FAnalysisProcessor	Process();

private:
	TArray<IAnalyzer*>	Analyzers;
};

} // namespace Trace
