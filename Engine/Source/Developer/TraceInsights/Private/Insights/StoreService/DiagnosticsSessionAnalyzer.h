// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Trace/Analyzer.h"

namespace Insights
{

struct FDiagnosticsSessionAnalyzer : public Trace::IAnalyzer
{
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle, const FOnEventContext& Context) override;

	enum : uint16
	{
		RouteId_Session,
		RouteId_Session2,
	};

	FString Platform;
	FString AppName;
	FString CommandLine;
	EBuildConfiguration ConfigurationType;
	EBuildTargetType TargetType;
};

} // namespace Insights
