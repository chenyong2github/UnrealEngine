// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

////////////////////////////////////////////////////////////////////////////////
namespace Trace
{
	class IAnalysisSession;
	class FCallstacksProvider;
};

////////////////////////////////////////////////////////////////////////////////
class FCallstacksAnalyzer
	: public Trace::IAnalyzer
{
public:
					FCallstacksAnalyzer(Trace::IAnalysisSession& Session, Trace::FCallstacksProvider* Provider);
	virtual void	OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool 	OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum Routes 
	{
		RouteId_Callstack,
	};

	Trace::FCallstacksProvider* Provider;
};

