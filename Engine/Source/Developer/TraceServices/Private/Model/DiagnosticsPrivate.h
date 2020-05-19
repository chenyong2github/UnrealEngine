// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/Diagnostics.h"

namespace Trace
{

class FDiagnosticsProvider : public IDiagnosticsProvider
{
public:
	FDiagnosticsProvider(IAnalysisSession& Session);
	virtual ~FDiagnosticsProvider() {}

public:
	static FName ProviderName;

	void SetSessionInfo(const FSessionInfo& InSessionInfo);
	virtual const FSessionInfo& GetSessionInfo() const override;
	virtual bool IsSessionInfoAvailable() const override;

private:
	IAnalysisSession& Session;
	FSessionInfo SessionInfo;
	bool bIsSessionInfoAvailable = false;
};

} // namespace Trace
