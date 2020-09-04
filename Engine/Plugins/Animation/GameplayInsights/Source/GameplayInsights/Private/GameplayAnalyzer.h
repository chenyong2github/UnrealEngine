// Copyright Epic Games, Inc. All Rights Reserved.

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
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_World,
		RouteId_Class,
		RouteId_Object,
		RouteId_ObjectEvent,
		RouteId_ClassPropertyStringId,
		RouteId_ClassProperty,
		RouteId_PropertiesStart,
		RouteId_PropertiesEnd,
		RouteId_PropertyValue,
	};

	Trace::IAnalysisSession& Session;
	FGameplayProvider& GameplayProvider;
};