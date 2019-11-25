// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

class FGameplayProvider;
namespace Trace { class IAnalysisSession; }

class FGameplayAnalyzer : public Trace::IAnalyzer
{
public:
	FGameplayAnalyzer(Trace::IAnalysisSession& InSession, FGameplayProvider& InGameplayProvider);

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override {}
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_Class,
		RouteId_Object,
		RouteId_ObjectEvent,
	};

	Trace::IAnalysisSession& Session;
	FGameplayProvider& GameplayProvider;
};