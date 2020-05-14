// Copyright Epic Games, Inc. All Rights Reserved.
#include "TraceServices/Model/Diagnostics.h"
#include "Model/DiagnosticsPrivate.h"

namespace Trace
{

FName FDiagnosticsProvider::ProviderName(TEXT("DiagnosticsProvider"));

FDiagnosticsProvider::FDiagnosticsProvider(IAnalysisSession& InSession)
	: Session(InSession)
{
}

void FDiagnosticsProvider::SetSessionInfo(const FSessionInfo& InSessionInfo)
{
	Session.WriteAccessCheck();
	SessionInfo = InSessionInfo;
	bIsSessionInfoAvailable = true;
}

bool FDiagnosticsProvider::IsSessionInfoAvailable() const
{
	Session.ReadAccessCheck();
	return bIsSessionInfoAvailable;
}

const FSessionInfo& FDiagnosticsProvider::GetSessionInfo() const
{
	Session.ReadAccessCheck();
	return SessionInfo;
}

const IDiagnosticsProvider& ReadDiagnosticsProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<IDiagnosticsProvider>(FDiagnosticsProvider::ProviderName);
}

} // namespace Trace
