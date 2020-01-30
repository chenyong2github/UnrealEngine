// Copyright Epic Games, Inc. All Rights Reserved.


#include "UnrealInsightsCLI.h"

#include "RequiredProgramMainCPPInclude.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"
#include "TraceServices/ModuleService.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealInsightsCLI, Log, All);

IMPLEMENT_APPLICATION(UnrealInsightsCLI, "UnrealInsightsCLI");

const char CommandlineHelpText[] = "\n\
Usage: UnrealInsightsCLI.exe Command [Arguments...]\n\
\n\
Commands:\n\
	ReportFromFile				Generate report from a saved trace file.\n\
	ReportFromConnection		Generate report by starting analysis server, waiting for\n\
								the first connection, then running the report on once \n\
								the session is closed.\n\
\n\
Arguments:\n\
	-inputfile=[file]			Trace file to read from.\n\
	-outputdir=[dir]			Directory to output from. Default is project 'Report' dir\n\
\n\
";


/**
 * Generate the report given a session URI.
 */
int GenerateReport(const TCHAR* SessionUri, Trace::IAnalysisService& AnalysisService, Trace::IModuleService& ModuleService, const FString& OutputDirectory)
{
	using namespace Trace; 
	
	TSharedPtr<const IAnalysisSession> Session = AnalysisService.Analyze(SessionUri);
	if (!Session)
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Trace file %s not found"), SessionUri);
		return 1;
	}

	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	if (FileSystem.CreateDirectory(*OutputDirectory))
	{
		FString OutputDirectoryAbs = FPaths::ConvertRelativePathToFull(OutputDirectory);
		UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Saving reports to %s"), *OutputDirectoryAbs);

		FAnalysisSessionReadScope _(*Session);
		ModuleService.GenerateReports(*Session, FCommandLine::Get(), *OutputDirectory);
	}
	else
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Failed to create directory %s"), *OutputDirectory);
		return 1;
	}

	return 0;
}

/**
 * Generate report from a saved trace file.
 */
int Command_ReportFromFile(const TArray<FString> Args)
{
	using namespace Trace;

	FString FileName;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-inputfile="), FileName))
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("No file given."));
		return 1;
	}

	FString OutputDirectory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-outputdir="), OutputDirectory))
	{
		// Fall back on project report directory
		OutputDirectory = FPaths::ProjectSavedDir() / TEXT("Reports") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	}

	if (!FPaths::ValidatePath(OutputDirectory))
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Outputdirectory %s is not valid path."), *OutputDirectory);
		return 1;
	}

	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Output directory set to %s."), *OutputDirectory);

	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	
	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Analyzing %s..."), *FileName);
	return GenerateReport(*FileName, *TraceServicesModule.GetAnalysisService(), *TraceServicesModule.GetModuleService(), OutputDirectory);
}

/**
 * Generate report by starting analysis server, waiting for the first connection, then running the report
 * on once the session is closed.
 */
int Command_ReportFromConnection(const TArray<FString>&  Args)
{
	using namespace Trace;

	FString OutputDirectory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("-outputdir="), OutputDirectory))
	{
		// Fall back on project report directory
		OutputDirectory = FPaths::ProjectSavedDir() / TEXT("Reports") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	}

	if (!FPaths::ValidatePath(OutputDirectory))
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Outputdirectory %s is not valid path."), *OutputDirectory);
		return 1;
	}

	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Output directory set to %s."), *OutputDirectory);

	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TSharedPtr<Trace::ISessionService> SessionService = TraceServicesModule.CreateSessionService(*OutputDirectory);

	if (!SessionService->StartRecorderServer())
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Failed sto start recording server."));
		return 1;
	}

	// Wait for the first connection to appear, then wait for it to close.
	FSessionHandle Session = 0;
	bool bDoRun = true;

	UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Waiting for connection..."));

	while (bDoRun)
	{
		TArray<FSessionHandle> LiveSessions;
		SessionService->GetLiveSessions(LiveSessions);
		if (!Session && LiveSessions.Num() > 0)
		{
			UE_LOG(LogUnrealInsightsCLI, Display, TEXT("Connection established."));
			Session = LiveSessions[0];
		}
		if (Session && LiveSessions.Num() == 0)
		{
			bDoRun = false;
		}
		FPlatformProcess::Sleep(0.2f);
		FTicker::GetCoreTicker().Tick(0.2f);
	}

	Trace::FSessionInfo SessionInfo;
	if (!SessionService->GetSessionInfo(Session, SessionInfo))
	{
		UE_LOG(LogUnrealInsightsCLI, Error, TEXT("Failed to load session"));
		return 1;
	}

	return GenerateReport(SessionInfo.Uri, *TraceServicesModule.GetAnalysisService(), *TraceServicesModule.GetModuleService(), OutputDirectory);
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	using namespace Trace;

	GEngineLoop.PreInit(ArgC, ArgV);

	int Result = 0;
	TArray<FString> Tokens, Switches;
	FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);
	if (Tokens.Num() < 1)
	{
		printf(CommandlineHelpText);
		Result = 2;
	}
	else
	{
		if (Tokens[0].Compare(TEXT("ReportFromFile"), ESearchCase::IgnoreCase) == 0)
		{
			Result = Command_ReportFromFile(Switches);
		}
		else if (Tokens[0].Compare(TEXT("ReportFromConnection"), ESearchCase::IgnoreCase) == 0)
		{
			Result = Command_ReportFromConnection(Switches);
		}
	}

	FModuleManager::Get().UnloadModulesAtShutdown();

	RequestEngineExit(TEXT("UnrealInsightsCLI finished"));
	return Result;
}

