// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace Trace
{

struct FSessionInfo
{
	FString Platform;
	FString AppName;
	FString CommandLine;
	EBuildConfiguration ConfigurationType;
	EBuildTargetType TargetType;
};

class IDiagnosticsProvider : public IProvider
{
public:
	virtual ~IDiagnosticsProvider() = default;

	virtual bool IsSessionInfoAvailable() const = 0;

	virtual const FSessionInfo& GetSessionInfo() const = 0;
};

TRACESERVICES_API const IDiagnosticsProvider& ReadDiagnosticsProvider(const IAnalysisSession& Session);

} // namespace Trace
