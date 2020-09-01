// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

#if 0

namespace Trace
{

class IAnalysisSession;

};

////////////////////////////////////////////////////////////////////////////////
class FAllocationsAnalyzer
	: public Trace::IAnalyzer
{
public:
					FAllocationsAnalyzer(Trace::IAnalysisSession& Session);
	virtual void	OnAnalysisBegin(const FOnAnalysisContext& Context) override;
};

#endif // 0
