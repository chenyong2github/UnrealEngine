// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Trace/Analyzer.h"

namespace Insights
{

struct FDiagnosticsSessionAnalyzer : public Trace::IAnalyzer
{
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16, const FOnEventContext& Context) override;

	FString Platform;
	FString AppName;
	FString CommandLine;
	int8 ConfigurationType = 0;
	int8 TargetType = 0;
};

} // namespace Insights
