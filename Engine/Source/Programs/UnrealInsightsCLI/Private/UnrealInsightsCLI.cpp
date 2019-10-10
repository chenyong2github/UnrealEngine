// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "UnrealInsightsCLI.h"

#include "RequiredProgramMainCPPInclude.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"
#include "TraceServices/ModuleService.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealInsightsCLI, Log, All);

IMPLEMENT_APPLICATION(UnrealInsightsCLI, "UnrealInsightsCLI");

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	using namespace Trace;

	GEngineLoop.PreInit(ArgC, ArgV);
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TSharedPtr<IAnalysisService> AnalysisService = TraceServicesModule.GetAnalysisService();
	TSharedPtr<Trace::ISessionService> SessionService = TraceServicesModule.GetSessionService();
	TSharedPtr<IModuleService> ModuleService = TraceServicesModule.GetModuleService();

	TArray<FString> Tokens, Switches;
	FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);
	if (Tokens.Num() < 1)
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Usage: UnrealInsightsCLI.exe TraceFile [Arguments...]"));
		return 1;
	}
	const TCHAR* FileName = *Tokens[0];
	TUniquePtr<IInDataStream> DataStream(SessionService->OpenSessionFromFile(FileName));
	if (!DataStream)
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Trace file %s not found"), FileName);
		return 1;
	}
	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Analyzing %s..."), FileName);
	double StartTime = FPlatformTime::Seconds();
	TSharedPtr<const IAnalysisSession> Session = AnalysisService->Analyze(TEXT("Session"), MoveTemp(DataStream));
	double Duration = FPlatformTime::Seconds() - StartTime;
	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Analysis done! Took %f seconds"), Duration);

	FString OutputDirectory = FPaths::ProjectSavedDir() / TEXT("Reports") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	FileSystem.CreateDirectory(*OutputDirectory);
	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Saving reports to %s"), *OutputDirectory);

	FAnalysisSessionReadScope _(*Session);
	ModuleService->GenerateReports(*Session, FCommandLine::Get(), *OutputDirectory);

	GIsRequestingExit = true;
	return 0;
}

