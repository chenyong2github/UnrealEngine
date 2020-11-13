// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

#if 0

namespace TraceServices
{

class IAnalysisSession;

};

////////////////////////////////////////////////////////////////////////////////
class FAllocationsAnalyzer
	: public Trace::IAnalyzer
{
public:
					FAllocationsAnalyzer(IAnalysisSession& Session);
	virtual void	OnAnalysisBegin(const FOnAnalysisContext& Context) override;
};

} // namespace TraceServices

#endif // 0
