// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformStackWalk.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Internationalization/Culture.h"
#include "Misc/Optional.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineBuildSettings.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Stats/Stats.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"
#include "HAL/PlatformOutputDevices.h"
#include "Misc/OutputDeviceArchiveWrapper.h"

#ifndef NOINITCRASHREPORTER
#define NOINITCRASHREPORTER 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCrashContext, Display, All);

extern CORE_API bool GIsGPUCrashed;

/*-----------------------------------------------------------------------------
	FGenericCrashContext
-----------------------------------------------------------------------------*/

const ANSICHAR* const FGenericCrashContext::CrashContextRuntimeXMLNameA = "CrashContext.runtime-xml";
const TCHAR* const FGenericCrashContext::CrashContextRuntimeXMLNameW = TEXT( "CrashContext.runtime-xml" );

const ANSICHAR* const FGenericCrashContext::CrashConfigFileNameA = "CrashReportClient.ini";
const TCHAR* const FGenericCrashContext::CrashConfigFileNameW = TEXT("CrashReportClient.ini");
const TCHAR* const FGenericCrashContext::CrashConfigExtension = TEXT(".ini");
const TCHAR* const FGenericCrashContext::ConfigSectionName = TEXT("CrashReportClient");
const TCHAR* const FGenericCrashContext::CrashConfigPurgeDays = TEXT("CrashConfigPurgeDays");
const TCHAR* const FGenericCrashContext::CrashGUIDRootPrefix = TEXT("UE4CC-");

const TCHAR* const FGenericCrashContext::CrashContextExtension = TEXT(".runtime-xml");
const TCHAR* const FGenericCrashContext::RuntimePropertiesTag = TEXT( "RuntimeProperties" );
const TCHAR* const FGenericCrashContext::PlatformPropertiesTag = TEXT( "PlatformProperties" );
const TCHAR* const FGenericCrashContext::EngineDataTag = TEXT( "EngineData" );
const TCHAR* const FGenericCrashContext::GameDataTag = TEXT( "GameData" );
const TCHAR* const FGenericCrashContext::EnabledPluginsTag = TEXT("EnabledPlugins");
const TCHAR* const FGenericCrashContext::UE4MinidumpName = TEXT( "UE4Minidump.dmp" );
const TCHAR* const FGenericCrashContext::NewLineTag = TEXT( "&nl;" );

const TCHAR* const FGenericCrashContext::CrashTypeCrash = TEXT("Crash");
const TCHAR* const FGenericCrashContext::CrashTypeAssert = TEXT("Assert");
const TCHAR* const FGenericCrashContext::CrashTypeEnsure = TEXT("Ensure");
const TCHAR* const FGenericCrashContext::CrashTypeGPU = TEXT("GPUCrash");
const TCHAR* const FGenericCrashContext::CrashTypeHang = TEXT("Hang");

const TCHAR* const FGenericCrashContext::EngineModeExUnknown = TEXT("Unset");
const TCHAR* const FGenericCrashContext::EngineModeExDirty = TEXT("Dirty");
const TCHAR* const FGenericCrashContext::EngineModeExVanilla = TEXT("Vanilla");

bool FGenericCrashContext::bIsInitialized = false;
bool FGenericCrashContext::bIsOutOfProcess = false;
int32 FGenericCrashContext::StaticCrashContextIndex = 0;

const FGuid FGenericCrashContext::ExecutionGuid = FGuid::NewGuid();

namespace NCached
{
	static FSessionContext Session;
	static TArray<FString> EnabledPluginsList;
	static TMap<FString, FString> EngineData;
	static TMap<FString, FString> GameData;
}

void FGenericCrashContext::Initialize()
{
#if !NOINITCRASHREPORTER
	NCached::Session.bIsInternalBuild = FEngineBuildSettings::IsInternalBuild();
	NCached::Session.bIsPerforceBuild = FEngineBuildSettings::IsPerforceBuild();
	NCached::Session.bIsSourceDistribution = FEngineBuildSettings::IsSourceDistribution();
	NCached::Session.ProcessId = FPlatformProcess::GetCurrentProcessId();

	FCString::Strcpy(NCached::Session.GameName, *FString::Printf( TEXT("UE4-%s"), FApp::GetProjectName() ));
	FCString::Strcpy(NCached::Session.GameSessionID, TEXT(""));
	FCString::Strcpy(NCached::Session.GameStateName, TEXT(""));
	FCString::Strcpy(NCached::Session.UserActivityHint, TEXT(""));
	FCString::Strcpy(NCached::Session.ExecutableName, FPlatformProcess::ExecutableName());
	FCString::Strcpy(NCached::Session.BaseDir, FPlatformProcess::BaseDir());
	FCString::Strcpy(NCached::Session.RootDir, FPlatformMisc::RootDir());
	FCString::Strcpy(NCached::Session.EpicAccountId, *FPlatformMisc::GetEpicAccountId());
	FCString::Strcpy(NCached::Session.LoginIdStr, *FPlatformMisc::GetLoginId());

	FString OsVersion, OsSubVersion;
	FPlatformMisc::GetOSVersions(OsVersion, OsSubVersion);
	FCString::Strcpy(NCached::Session.OsVersion, *OsVersion);
	FCString::Strcpy(NCached::Session.OsSubVersion, *OsSubVersion);

	NCached::Session.NumberOfCores = FPlatformMisc::NumberOfCores();
	NCached::Session.NumberOfCoresIncludingHyperthreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	FCString::Strcpy(NCached::Session.CPUVendor, *FPlatformMisc::GetCPUVendor());
	FCString::Strcpy(NCached::Session.CPUBrand, *FPlatformMisc::GetCPUBrand());
	FCString::Strcpy(NCached::Session.PrimaryGPUBrand, *FPlatformMisc::GetPrimaryGPUBrand());
	FCString::Strcpy(NCached::Session.UserName, FPlatformProcess::UserName());
	FCString::Strcpy(NCached::Session.DefaultLocale, *FPlatformMisc::GetDefaultLocale());

	// Information that cannot be gathered if command line is not initialized (e.g. crash during static init)
	if (FCommandLine::IsInitialized())
	{
		NCached::Session.bIsUE4Release = FApp::IsEngineInstalled();
		FCString::Strcpy(NCached::Session.CommandLine, (FCommandLine::IsInitialized() ? FCommandLine::GetOriginalForLogging() : TEXT("")));
		FCString::Strcpy(NCached::Session.EngineMode, FGenericPlatformMisc::GetEngineMode());
		FCString::Strcpy(NCached::Session.EngineModeEx, EngineModeExString());

		// Use -epicapp value from the commandline to start. This will also be set by the game
		FParse::Value(FCommandLine::Get(), TEXT("EPICAPP="), NCached::Session.DeploymentName, CR_MAX_GENERIC_FIELD_CHARS, true);

		// Using the -fullcrashdump parameter will cause full memory minidumps to be created for crashes
		NCached::Session.CrashDumpMode = (int32)ECrashDumpMode::Default;
		if (FPlatformMisc::SupportsFullCrashDumps() && FCommandLine::IsInitialized())
		{
			const TCHAR* CmdLine = FCommandLine::Get();
			if (FParse::Param(CmdLine, TEXT("fullcrashdumpalways")))
			{
				NCached::Session.CrashDumpMode = (int32)ECrashDumpMode::FullDumpAlways;
			}
			else if (FParse::Param(CmdLine, TEXT("fullcrashdump")))
			{
				NCached::Session.CrashDumpMode = (int32)ECrashDumpMode::FullDump;
			}
		}
	}

	// Create a unique base guid for bug report ids
	const FGuid Guid = FGuid::NewGuid();
	const FString IniPlatformName(FPlatformProperties::IniPlatformName());
	FCString::Strcpy(NCached::Session.CrashGUIDRoot, *FString::Printf(TEXT("%s%s-%s"), CrashGUIDRootPrefix, *IniPlatformName, *Guid.ToString(EGuidFormats::Digits)));

	if (GIsRunning)
	{
		if (FInternationalization::IsAvailable())
		{
			NCached::Session.LanguageLCID = FInternationalization::Get().GetCurrentCulture()->GetLCID();
		}
		else
		{
			FCulturePtr DefaultCulture = FInternationalization::Get().GetCulture(TEXT("en"));
			if (DefaultCulture.IsValid())
			{
				NCached::Session.LanguageLCID = DefaultCulture->GetLCID();
			}
			else
			{
				const int DefaultCultureLCID = 1033;
				NCached::Session.LanguageLCID = DefaultCultureLCID;
			}
		}
	}

	// Initialize delegate for updating SecondsSinceStart, because FPlatformTime::Seconds() is not POSIX safe.
	const float PollingInterval = 1.0f;
	FTicker::GetCoreTicker().AddTicker( FTickerDelegate::CreateLambda( []( float DeltaTime )
	{
        QUICK_SCOPE_CYCLE_COUNTER(STAT_NCachedCrashContextProperties_LambdaTicker);

		NCached::Session.SecondsSinceStart = int32(FPlatformTime::Seconds() - GStartTime);
		return true;
	} ), PollingInterval );

	FCoreDelegates::UserActivityStringChanged.AddLambda([](const FString& InUserActivity)
	{
		FCString::Strcpy(NCached::Session.UserActivityHint, *InUserActivity);
	});

	FCoreDelegates::GameSessionIDChanged.AddLambda([](const FString& InGameSessionID)
	{
		FCString::Strcpy(NCached::Session.GameSessionID, *InGameSessionID);
	});

	FCoreDelegates::GameStateClassChanged.AddLambda([](const FString& InGameStateName)
	{
		FCString::Strcpy(NCached::Session.GameStateName, *InGameStateName);
	});

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FCoreDelegates::CrashOverrideParamsChanged.AddLambda([](const FCrashOverrideParameters& InParams)
	{
		if (InParams.bSetCrashReportClientMessageText)
		{
			FCString::Strcpy(NCached::Session.CrashReportClientRichText, *InParams.CrashReportClientMessageText);
		}
		if (InParams.bSetGameNameSuffix)
		{
			FCString::Strcpy(NCached::Session.GameName, *(FString(TEXT("UE4-")) + FApp::GetProjectName() + InParams.GameNameSuffix));
		}
	});
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FCoreDelegates::IsVanillaProductChanged.AddLambda([](bool bIsVanilla)
	{
		NCached::Session.bIsVanilla = bIsVanilla;
	});

	FCoreDelegates::ConfigReadyForUse.AddStatic(FGenericCrashContext::InitializeFromConfig);

	bIsInitialized = true;
#endif	// !NOINITCRASHREPORTER
}

void FGenericCrashContext::InitializeFromContext(const FSessionContext& Session, const TCHAR* EnabledPluginsStr, const TCHAR* EngineDataStr, const TCHAR* GameDataStr)
{
	static const TCHAR* TokenDelim[] = { TEXT(","), TEXT("=") };

	// Copy the session struct which should be all pod types and fixed size buggers
	FMemory::Memcpy(NCached::Session, Session);
	
	// Parse the loaded plugins string, assume comma delimited values.
	if (EnabledPluginsStr)
	{
		TArray<FString> Tokens;
		FString(EnabledPluginsStr).ParseIntoArray(Tokens, TokenDelim, 2, true);
		NCached::EnabledPluginsList.Append(Tokens);
	}

	// Parse engine data, comma delimited key=value pairs.
	if (EngineDataStr)
	{
		TArray<FString> Tokens;
		FString(EngineDataStr).ParseIntoArray(Tokens, TokenDelim, 2, true);
		int32 i = 0;
		while ((i + 1) < Tokens.Num())
		{
			const FString& Key = Tokens[i++];
			const FString& Value = Tokens[i++];
			NCached::EngineData.Add(Key, Value);
		}
	}

	// Parse engine data, comma delimited key=value pairs.
	if (GameDataStr)
	{
		TArray<FString> Tokens;
		FString(GameDataStr).ParseIntoArray(Tokens, TokenDelim, 2, true);
		int32 i = 0;
		while ((i + 1) < Tokens.Num())
		{
			const FString& Key = Tokens[i++];
			const FString& Value = Tokens[i++];
			NCached::GameData.Add(Key, Value);
		}
	}

	bIsInitialized = true;
}

void FGenericCrashContext::CopySharedCrashContext(FSharedCrashContext& Dst)
{
	//Copy the session
	FMemory::Memcpy(Dst.SessionContext, NCached::Session);

	TCHAR* DynamicDataStart = &Dst.DynamicData[0];
	TCHAR* DynamicDataPtr = DynamicDataStart;

	Dst.EnabledPluginsOffset = DynamicDataPtr - DynamicDataStart;
	Dst.EnabledPluginsNum = NCached::EnabledPluginsList.Num();
	for (const FString& Plugin : NCached::EnabledPluginsList)
	{
		FCString::Strcat(DynamicDataPtr, Plugin.Len(), *Plugin);
		FCString::Strcat(DynamicDataPtr, 1, TEXT(","));
	}
	DynamicDataPtr += FCString::Strlen(DynamicDataPtr) + 1;

	Dst.EngineDataOffset = DynamicDataPtr - DynamicDataStart;
	Dst.EngineDataNum = NCached::EngineData.Num();
	for (const TPair<FString, FString>& Pair : NCached::EngineData)
	{
		FCString::Strcat(DynamicDataPtr, Pair.Key.Len(), *Pair.Key);
		FCString::Strcat(DynamicDataPtr, 1, TEXT("="));
		FCString::Strcat(DynamicDataPtr, Pair.Value.Len(), *Pair.Value);
		FCString::Strcat(DynamicDataPtr, 1, TEXT(","));
	}
	DynamicDataPtr += FCString::Strlen(DynamicDataPtr) + 1;

	Dst.GameDataOffset = DynamicDataPtr - DynamicDataStart;
	Dst.GameDataNum = NCached::GameData.Num();
	for (const TPair<FString, FString>& Pair : NCached::GameData)
	{
		FCString::Strcat(DynamicDataPtr, Pair.Key.Len(), *Pair.Key);
		FCString::Strcat(DynamicDataPtr, 1, TEXT("="));
		FCString::Strcat(DynamicDataPtr, Pair.Value.Len(), *Pair.Value);
		FCString::Strcat(DynamicDataPtr, 1, TEXT(","));
	}
	DynamicDataPtr += FCString::Strlen(DynamicDataPtr) + 1;
}

void FGenericCrashContext::SetMemoryStats(const FPlatformMemoryStats& InMemoryStats)
{
	NCached::Session.MemoryStats = InMemoryStats;

	// Update cached OOM stats
	NCached::Session.bIsOOM = FPlatformMemory::bIsOOM;
	NCached::Session.OOMAllocationSize = FPlatformMemory::OOMAllocationSize;
	NCached::Session.OOMAllocationAlignment = FPlatformMemory::OOMAllocationAlignment;
}

void FGenericCrashContext::InitializeFromConfig()
{
#if !NOINITCRASHREPORTER
	PurgeOldCrashConfig();

	const bool bForceGetSection = false;
	const bool bConstSection = true;
	FConfigSection* CRCConfigSection = GConfig->GetSectionPrivate(ConfigSectionName, bForceGetSection, bConstSection, GEngineIni);

	if (CRCConfigSection != nullptr)
	{
		// Create a config file and save to a temp location. This file will be copied to
		// the crash folder for all crash reports create by this session.
		FConfigFile CrashConfigFile;

		FConfigSection CRCConfigSectionCopy(*CRCConfigSection);
		CrashConfigFile.Add(ConfigSectionName, CRCConfigSectionCopy);

		CrashConfigFile.Dirty = true;
		CrashConfigFile.Write(FString(GetCrashConfigFilePath()));
	}

	// Read the initial un-localized crash context text
	UpdateLocalizedStrings();

	// Make sure we get updated text once the localized version is loaded
	FTextLocalizationManager::Get().OnTextRevisionChangedEvent.AddStatic(&UpdateLocalizedStrings);
#endif	// !NOINITCRASHREPORTER
}

void FGenericCrashContext::UpdateLocalizedStrings()
{
#if !NOINITCRASHREPORTER
	// Allow overriding the crash text
	FText CrashReportClientRichText;
	if (GConfig->GetText(TEXT("CrashContextProperties"), TEXT("CrashReportClientRichText"), CrashReportClientRichText, GEngineIni))
	{
		FCString::Strcpy(NCached::Session.CrashReportClientRichText, *CrashReportClientRichText.ToString());
	}
#endif
}

FGenericCrashContext::FGenericCrashContext(ECrashContextType InType, const TCHAR* InErrorMessage)
	: Type(InType)
	, CrashedThreadId(~uint32(0))
	, ErrorMessage(InErrorMessage)
	, NumMinidumpFramesToIgnore(0)
{
	CommonBuffer.Reserve( 32768 );
	CrashContextIndex = StaticCrashContextIndex++;
}

void FGenericCrashContext::SerializeContentToBuffer() const
{
	TCHAR CrashGUID[CrashGUIDLength];
	GetUniqueCrashName(CrashGUID, CrashGUIDLength);

	// Must conform against:
	// https://www.securecoding.cert.org/confluence/display/seccode/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers
	AddHeader();

	BeginSection( RuntimePropertiesTag );
	AddCrashProperty( TEXT( "CrashVersion" ), (int32)ECrashDescVersions::VER_3_CrashContext );
	AddCrashProperty( TEXT( "ExecutionGuid" ), *ExecutionGuid.ToString() );
	AddCrashProperty( TEXT( "CrashGUID" ), (const TCHAR*)CrashGUID);
	AddCrashProperty( TEXT( "ProcessId" ), NCached::Session.ProcessId );
	AddCrashProperty( TEXT( "IsInternalBuild" ), NCached::Session.bIsInternalBuild );
	AddCrashProperty( TEXT( "IsPerforceBuild" ), NCached::Session.bIsPerforceBuild );
	AddCrashProperty( TEXT( "IsSourceDistribution" ), NCached::Session.bIsSourceDistribution );
	AddCrashProperty( TEXT( "IsEnsure" ), (Type == ECrashContextType::Ensure) );
	AddCrashProperty( TEXT( "IsAssert" ), (Type == ECrashContextType::Assert) );
	AddCrashProperty( TEXT( "CrashType" ), GetCrashTypeString(Type) );

	AddCrashProperty( TEXT( "SecondsSinceStart" ), NCached::Session.SecondsSinceStart );

	// Add common crash properties.
	if (FCString::Strlen(NCached::Session.GameName) > 0)
	{
		AddCrashProperty(TEXT("GameName"), NCached::Session.GameName);
	}
	else
	{
		const TCHAR* ProjectName = FApp::GetProjectName();
		if (ProjectName != nullptr && ProjectName[0] != 0)
		{
			AddCrashProperty(TEXT("GameName"), *FString::Printf(TEXT("UE4-%s"), ProjectName));
		}
		else
		{
			AddCrashProperty(TEXT("GameName"), TEXT(""));
		}
	}
	AddCrashProperty( TEXT( "ExecutableName" ), NCached::Session.ExecutableName );
	AddCrashProperty( TEXT( "BuildConfiguration" ), LexToString( FApp::GetBuildConfiguration() ) );
	AddCrashProperty( TEXT( "GameSessionID" ), NCached::Session.GameSessionID );
	
	// Unique string specifying the symbols to be used by CrashReporter
	FString Symbols = FString::Printf( TEXT( "%s" ), FApp::GetBuildVersion());
#ifdef UE_APP_FLAVOR
	Symbols = FString::Printf(TEXT( "%s-%s" ), *Symbols, *FString(UE_APP_FLAVOR));
#endif
	Symbols = FString::Printf(TEXT("%s-%s-%s"), *Symbols, FPlatformMisc::GetUBTPlatform(), LexToString(FApp::GetBuildConfiguration())).Replace( TEXT( "+" ), TEXT( "*" ));
#ifdef UE_BUILD_FLAVOR
	Symbols = FString::Printf(TEXT( "%s-%s" ), *Symbols, *FString(UE_BUILD_FLAVOR));
#endif
#ifdef UE_APP_FLAVOR
	Symbols = FString::Printf(TEXT( "%s-%s" ), *Symbols, *FString(UE_APP_FLAVOR));
#endif

	AddCrashProperty( TEXT( "Symbols" ), Symbols);

	AddCrashProperty( TEXT( "PlatformName" ), FPlatformProperties::PlatformName() );
	AddCrashProperty( TEXT( "PlatformNameIni" ), FPlatformProperties::IniPlatformName());
	AddCrashProperty( TEXT( "EngineMode" ), NCached::Session.EngineMode);
	AddCrashProperty( TEXT( "EngineModeEx" ), NCached::Session.EngineModeEx);

	AddCrashProperty( TEXT( "DeploymentName"), NCached::Session.DeploymentName );

	AddCrashProperty( TEXT( "EngineVersion" ), *FEngineVersion::Current().ToString() );
	AddCrashProperty( TEXT( "CommandLine" ), NCached::Session.CommandLine );
	AddCrashProperty( TEXT( "LanguageLCID" ), NCached::Session.LanguageLCID );
	AddCrashProperty( TEXT( "AppDefaultLocale" ), NCached::Session.DefaultLocale );
	AddCrashProperty( TEXT( "BuildVersion" ), FApp::GetBuildVersion() );
	AddCrashProperty( TEXT( "IsUE4Release" ), NCached::Session.bIsUE4Release );
	AddCrashProperty( TEXT( "IsRequestingExit" ), IsEngineExitRequested() );

	// Remove periods from user names to match AutoReporter user names
	// The name prefix is read by CrashRepository.AddNewCrash in the website code
	const bool bSendUserName = NCached::Session.bIsInternalBuild;
	FString SanitizedUserName = FString(NCached::Session.UserName).Replace(TEXT("."), TEXT(""));
	AddCrashProperty( TEXT( "UserName" ), bSendUserName ? *SanitizedUserName : TEXT(""));

	AddCrashProperty( TEXT( "BaseDir" ), NCached::Session.BaseDir );
	AddCrashProperty( TEXT( "RootDir" ), NCached::Session.RootDir );
	AddCrashProperty( TEXT( "MachineId" ), *FString(NCached::Session.LoginIdStr).ToUpper() );
	AddCrashProperty( TEXT( "LoginId" ), NCached::Session.LoginIdStr );
	AddCrashProperty( TEXT( "EpicAccountId" ), NCached::Session.EpicAccountId );

	// Legacy callstack element for current crash reporter
	AddCrashProperty( TEXT( "NumMinidumpFramesToIgnore"), NumMinidumpFramesToIgnore );
	AddCrashProperty( TEXT( "CallStack" ), TEXT("") );

	// Add new portable callstack element with crash stack
	AddPortableCallStack();
	AddPortableCallStackHash();

	AddCrashProperty( TEXT( "SourceContext" ), TEXT( "" ) );
	AddCrashProperty( TEXT( "UserDescription" ), TEXT( "" ) );
	AddCrashProperty( TEXT( "UserActivityHint" ), NCached::Session.UserActivityHint );
	AddCrashProperty( TEXT( "ErrorMessage" ), ErrorMessage );
	AddCrashProperty( TEXT( "CrashDumpMode" ), NCached::Session.CrashDumpMode );
	AddCrashProperty( TEXT( "CrashReporterMessage" ), NCached::Session.CrashReportClientRichText );

	// Add misc stats.
	AddCrashProperty( TEXT( "Misc.NumberOfCores" ), NCached::Session.NumberOfCores );
	AddCrashProperty( TEXT( "Misc.NumberOfCoresIncludingHyperthreads" ), NCached::Session.NumberOfCoresIncludingHyperthreads );
	AddCrashProperty( TEXT( "Misc.Is64bitOperatingSystem" ), (int32)FPlatformMisc::Is64bitOperatingSystem() );

	AddCrashProperty( TEXT( "Misc.CPUVendor" ), NCached::Session.CPUVendor );
	AddCrashProperty( TEXT( "Misc.CPUBrand" ), NCached::Session.CPUBrand );
	AddCrashProperty( TEXT( "Misc.PrimaryGPUBrand" ), NCached::Session.PrimaryGPUBrand );
	AddCrashProperty( TEXT( "Misc.OSVersionMajor" ), NCached::Session.OsVersion );
	AddCrashProperty( TEXT( "Misc.OSVersionMinor" ), NCached::Session.OsSubVersion );

	AddCrashProperty(TEXT("GameStateName"), NCached::Session.GameStateName);

	// FPlatformMemory::GetConstants is called in the GCreateMalloc, so we can assume it is always valid.
	{
		// Add memory stats.
		const FPlatformMemoryConstants& MemConstants = FPlatformMemory::GetConstants();

		AddCrashProperty( TEXT( "MemoryStats.TotalPhysical" ), (uint64)MemConstants.TotalPhysical );
		AddCrashProperty( TEXT( "MemoryStats.TotalVirtual" ), (uint64)MemConstants.TotalVirtual );
		AddCrashProperty( TEXT( "MemoryStats.PageSize" ), (uint64)MemConstants.PageSize );
		AddCrashProperty( TEXT( "MemoryStats.TotalPhysicalGB" ), MemConstants.TotalPhysicalGB );
	}

	AddCrashProperty( TEXT( "MemoryStats.AvailablePhysical" ), (uint64)NCached::Session.MemoryStats.AvailablePhysical );
	AddCrashProperty( TEXT( "MemoryStats.AvailableVirtual" ), (uint64)NCached::Session.MemoryStats.AvailableVirtual );
	AddCrashProperty( TEXT( "MemoryStats.UsedPhysical" ), (uint64)NCached::Session.MemoryStats.UsedPhysical );
	AddCrashProperty( TEXT( "MemoryStats.PeakUsedPhysical" ), (uint64)NCached::Session.MemoryStats.PeakUsedPhysical );
	AddCrashProperty( TEXT( "MemoryStats.UsedVirtual" ), (uint64)NCached::Session.MemoryStats.UsedVirtual );
	AddCrashProperty( TEXT( "MemoryStats.PeakUsedVirtual" ), (uint64)NCached::Session.MemoryStats.PeakUsedVirtual );
	AddCrashProperty( TEXT( "MemoryStats.bIsOOM" ), (int) NCached::Session.bIsOOM );
	AddCrashProperty( TEXT( "MemoryStats.OOMAllocationSize"), NCached::Session.OOMAllocationSize );
	AddCrashProperty( TEXT( "MemoryStats.OOMAllocationAlignment"), NCached::Session.OOMAllocationAlignment );

	{
		FString AllThreadStacks;
		if (GetPlatformAllThreadContextsString(AllThreadStacks))
		{
			CommonBuffer += TEXT("<Threads>");
			CommonBuffer += AllThreadStacks;
			CommonBuffer += TEXT("</Threads>");
			CommonBuffer += LINE_TERMINATOR;
		}
	}

	EndSection( RuntimePropertiesTag );

	// Add platform specific properties.
	BeginSection( PlatformPropertiesTag );
	AddPlatformSpecificProperties();
	// The name here is a bit cryptic, but we keep it to avoid breaking backend stuff.
	AddCrashProperty(TEXT("PlatformCallbackResult"), NCached::Session.CrashType);
	EndSection( PlatformPropertiesTag );

	// Add the engine data
	BeginSection( EngineDataTag );
	for (const TPair<FString, FString>& Pair : NCached::EngineData)
	{
		AddCrashProperty(*Pair.Key, *Pair.Value);
	}
	EndSection( EngineDataTag );

	// Add the game data
	BeginSection( GameDataTag );
	for (const TPair<FString, FString>& Pair : NCached::GameData)
	{
		AddCrashProperty(*Pair.Key, *Pair.Value);
	}
	EndSection( GameDataTag );

	// Writing out the list of plugin JSON descriptors causes us to run out of memory
	// in GMallocCrash on console, so enable this only for desktop platforms.
#if PLATFORM_DESKTOP
	if(NCached::EnabledPluginsList.Num() > 0)
	{
		BeginSection(EnabledPluginsTag);

		for (const FString& Str : NCached::EnabledPluginsList)
		{
			AddCrashProperty(TEXT("Plugin"), *Str);
		}

		EndSection(EnabledPluginsTag);
	}
#endif // PLATFORM_DESKTOP

	AddFooter();
}

void FGenericCrashContext::SetNumMinidumpFramesToIgnore(int InNumMinidumpFramesToIgnore)
{
	NumMinidumpFramesToIgnore = InNumMinidumpFramesToIgnore;
}

void FGenericCrashContext::SetDeploymentName(const FString& EpicApp)
{
	FCString::Strcpy(NCached::Session.DeploymentName, *EpicApp);
}

void FGenericCrashContext::SetCrashTrigger(ECrashTrigger Type)
{
	NCached::Session.CrashType = (int32)Type;
}

void FGenericCrashContext::GetUniqueCrashName(TCHAR* GUIDBuffer, int32 BufferSize) const
{
	FCString::Snprintf(GUIDBuffer, BufferSize, TEXT("%s_%04i"), NCached::Session.CrashGUIDRoot, CrashContextIndex);
}

const bool FGenericCrashContext::IsFullCrashDump() const
{
	if(Type == ECrashContextType::Ensure)
	{
		return (NCached::Session.CrashDumpMode == (int32)ECrashDumpMode::FullDumpAlways);
	}
	else
	{
		return (NCached::Session.CrashDumpMode == (int32)ECrashDumpMode::FullDump) ||
			(NCached::Session.CrashDumpMode == (int32)ECrashDumpMode::FullDumpAlways);
	}
}

void FGenericCrashContext::SerializeAsXML( const TCHAR* Filename ) const
{
	SerializeContentToBuffer();
	// Use OS build-in functionality instead.
	FFileHelper::SaveStringToFile( CommonBuffer, Filename, FFileHelper::EEncodingOptions::AutoDetect );
}

void FGenericCrashContext::AddCrashProperty( const TCHAR* PropertyName, const TCHAR* PropertyValue ) const
{
	CommonBuffer += TEXT( "<" );
	CommonBuffer += PropertyName;
	CommonBuffer += TEXT( ">" );


	AppendEscapedXMLString(CommonBuffer, PropertyValue );

	CommonBuffer += TEXT( "</" );
	CommonBuffer += PropertyName;
	CommonBuffer += TEXT( ">" );
	CommonBuffer += LINE_TERMINATOR;
}

void FGenericCrashContext::AddPlatformSpecificProperties() const
{
	// Nothing really to do here. Can be overridden by the platform code.
	// @see FWindowsPlatformCrashContext::AddPlatformSpecificProperties
}

void FGenericCrashContext::AddPortableCallStackHash() const
{
	if (CallStack.Num() == 0)
	{
		AddCrashProperty(TEXT("PCallStackHash"), TEXT(""));
		return;
	}

	// This may allocate if its the first time calling into this function
	const TCHAR* ExeName = FPlatformProcess::ExecutableName();

	// We dont want this to be thrown into an FString as it will alloc memory
	const TCHAR* UE4EditorName = TEXT("UE4Editor");

	FSHA1 Sha;
	FSHAHash Hash;

	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		// If we are our own module or our module contains UE4Editor we assume we own these. We cannot depend on offsets of system libs
		// as they may have different versions
		if (It->ModuleName == ExeName || It->ModuleName.Contains(UE4EditorName))
		{
			Sha.Update(reinterpret_cast<const uint8*>(&It->Offset), sizeof(It->Offset));
		}
	}

	Sha.Final();
	Sha.GetHash(Hash.Hash);

	FString EscapedPortableHash;

	// Allocations here on both the ToString and AppendEscapedXMLString it self adds to the out FString
	AppendEscapedXMLString(EscapedPortableHash, *Hash.ToString());

	AddCrashProperty(TEXT("PCallStackHash"), *EscapedPortableHash);
}

void FGenericCrashContext::AddPortableCallStack() const
{	

	if (CallStack.Num() == 0)
	{
		AddCrashProperty(TEXT("PCallStack"), TEXT(""));
		return;
	}

	FString CrashStackBuffer = LINE_TERMINATOR;

	// Get the max module name length for padding
	int32 MaxModuleLength = 0;
	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		MaxModuleLength = FMath::Max(MaxModuleLength, It->ModuleName.Len());
	}

	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		CrashStackBuffer += FString::Printf(TEXT("%-*s 0x%016x + %-8x"),MaxModuleLength + 1, *It->ModuleName, It->BaseAddress, It->Offset);
		CrashStackBuffer += LINE_TERMINATOR;
	}

	FString EscapedStackBuffer;

	AppendEscapedXMLString(EscapedStackBuffer, *CrashStackBuffer);

	AddCrashProperty(TEXT("PCallStack"), *EscapedStackBuffer);
}

void FGenericCrashContext::AddHeader() const
{
	CommonBuffer += TEXT( "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" ) LINE_TERMINATOR;
	BeginSection( TEXT("FGenericCrashContext") );
}

void FGenericCrashContext::AddFooter() const
{
	EndSection( TEXT( "FGenericCrashContext" ) );
}

void FGenericCrashContext::BeginSection( const TCHAR* SectionName ) const
{
	CommonBuffer += TEXT( "<" );
	CommonBuffer += SectionName;
	CommonBuffer += TEXT( ">" );
	CommonBuffer += LINE_TERMINATOR;
}

void FGenericCrashContext::EndSection( const TCHAR* SectionName ) const
{
	CommonBuffer += TEXT( "</" );
	CommonBuffer += SectionName;
	CommonBuffer += TEXT( ">" );
	CommonBuffer += LINE_TERMINATOR;
}

void FGenericCrashContext::AppendEscapedXMLString(FString& OutBuffer, const TCHAR* Text)
{
	if (!Text)
	{
		return;
	}

	while (*Text)
	{
		switch (*Text)
		{
		case TCHAR('&'):
			OutBuffer += TEXT("&amp;");
			break;
		case TCHAR('"'):
			OutBuffer += TEXT("&quot;");
			break;
		case TCHAR('\''):
			OutBuffer += TEXT("&apos;");
			break;
		case TCHAR('<'):
			OutBuffer += TEXT("&lt;");
			break;
		case TCHAR('>'):
			OutBuffer += TEXT("&gt;");
			break;
		case TCHAR('\r'):
			break;
		default:
			OutBuffer += *Text;
		};

		Text++;
	}
}

FString FGenericCrashContext::UnescapeXMLString( const FString& Text )
{
	return Text
		.Replace(TEXT("&amp;"), TEXT("&"))
		.Replace(TEXT("&quot;"), TEXT("\""))
		.Replace(TEXT("&apos;"), TEXT("'"))
		.Replace(TEXT("&lt;"), TEXT("<"))
		.Replace(TEXT("&gt;"), TEXT(">"));
}

FString FGenericCrashContext::GetCrashGameName()
{
	return FString(NCached::Session.GameName);
}

const TCHAR* FGenericCrashContext::GetCrashTypeString(ECrashContextType Type)
{
	switch (Type)
	{
	case ECrashContextType::Hang:
		return CrashTypeHang;
	case ECrashContextType::GPUCrash:
		return CrashTypeGPU;
	case ECrashContextType::Ensure:
		return CrashTypeEnsure;
	case ECrashContextType::Assert:
		return CrashTypeAssert;
	default:
		return CrashTypeCrash;
	}
}

const TCHAR* FGenericCrashContext::EngineModeExString()
{
	return !NCached::Session.bIsVanilla.IsSet() ? FGenericCrashContext::EngineModeExUnknown :
		(NCached::Session.bIsVanilla.GetValue() ? FGenericCrashContext::EngineModeExVanilla : FGenericCrashContext::EngineModeExDirty);
}

const TCHAR* FGenericCrashContext::GetCrashConfigFilePath()
{
	if (FCString::Strlen(NCached::Session.CrashConfigFilePath) == 0)
	{
		FString CrashConfigFilePath = FPaths::Combine(GetCrashConfigFolder(), NCached::Session.CrashGUIDRoot, FGenericCrashContext::CrashConfigFileNameW);
		FCString::Strcpy(NCached::Session.CrashConfigFilePath, *CrashConfigFilePath);
	}
	return NCached::Session.CrashConfigFilePath;
}

const TCHAR* FGenericCrashContext::GetCrashConfigFolder()
{
	static FString CrashConfigFolder;
	if (CrashConfigFolder.IsEmpty())
	{
		CrashConfigFolder = FPaths::Combine(*FPaths::GeneratedConfigDir(), TEXT("CrashReportClient"));
	}
	return *CrashConfigFolder;
}

void FGenericCrashContext::PurgeOldCrashConfig()
{
	int32 PurgeDays = 2;
	GConfig->GetInt(ConfigSectionName, CrashConfigPurgeDays, PurgeDays, GEngineIni);

	if (PurgeDays > 0)
	{
		IFileManager& FileManager = IFileManager::Get();

		// Delete items older than PurgeDays
		TArray<FString> Directories;
		FileManager.FindFiles(Directories, *(FPaths::Combine(GetCrashConfigFolder(), CrashGUIDRootPrefix) + TEXT("*")), false, true);

		for (const FString& Dir : Directories)
		{
			const FString CrashConfigDirectory = FPaths::Combine(GetCrashConfigFolder(), *Dir);
			const FDateTime DirectoryAccessTime = FileManager.GetTimeStamp(*CrashConfigDirectory);
			if (FDateTime::Now() - DirectoryAccessTime > FTimespan::FromDays(PurgeDays))
			{
				FileManager.DeleteDirectory(*CrashConfigDirectory, false, true);
			}
		}
	}
}

void FGenericCrashContext::ResetEngineData()
{
	NCached::EngineData.Reset();
}

void FGenericCrashContext::SetEngineData(const FString& Key, const FString& Value)
{
	if (Value.Len() == 0)
	{
		// for testing purposes, only log values when they change, but don't pay the lookup price normally.
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (NCached::EngineData.Find(Key))
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetEngineData(%s, <RemoveKey>)"), *Key);
			}
		});
		NCached::EngineData.Remove(Key);
	}
	else
	{
		FString& OldVal = NCached::EngineData.FindOrAdd(Key);
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (OldVal != Value)
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetEngineData(%s, %s)"), *Key, *Value);
			}
		});
		OldVal = Value;
	}
}

void FGenericCrashContext::ResetGameData()
{
	NCached::GameData.Reset();
}

void FGenericCrashContext::SetGameData(const FString& Key, const FString& Value)
{
	if (Value.Len() == 0)
	{
		// for testing purposes, only log values when they change, but don't pay the lookup price normally.
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (NCached::GameData.Find(Key))
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetGameData(%s, <RemoveKey>)"), *Key);
			}
		});
		NCached::GameData.Remove(Key);
	}
	else
	{
		FString& OldVal = NCached::GameData.FindOrAdd(Key);
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (OldVal != Value)
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetGameData(%s, %s)"), *Key, *Value);
			}
		});
		OldVal = Value;
	}
}

void FGenericCrashContext::AddPlugin(const FString& PluginDesc)
{
	NCached::EnabledPluginsList.Add(PluginDesc);
}

void FGenericCrashContext::DumpLog(const FString& CrashFolderAbsolute)
{
	// Copy log
	const FString LogSrcAbsolute = FPlatformOutputDevices::GetAbsoluteLogFilename();
	FString LogFilename = FPaths::GetCleanFilename(LogSrcAbsolute);
	const FString LogDstAbsolute = FPaths::Combine(*CrashFolderAbsolute, *LogFilename);

	// If we have a memory only log, make sure it's dumped to file before we attach it to the report
#if !NO_LOGGING
	bool bMemoryOnly = FPlatformOutputDevices::GetLog()->IsMemoryOnly();
	bool bBacklogEnabled = FOutputDeviceRedirector::Get()->IsBacklogEnabled();

	if (bMemoryOnly || bBacklogEnabled)
	{
		TUniquePtr<FArchive> LogFile(IFileManager::Get().CreateFileWriter(*LogDstAbsolute, FILEWRITE_AllowRead));
		if (LogFile)
		{
			if (bMemoryOnly)
			{
				FPlatformOutputDevices::GetLog()->Dump(*LogFile);
			}
			else
			{
				FOutputDeviceArchiveWrapper Wrapper(LogFile.Get());
				GLog->SerializeBacklog(&Wrapper);
			}

			LogFile->Flush();
		}
	}
	else
	{
		const bool bReplace = true;
		const bool bEvenIfReadOnly = false;
		const bool bAttributes = false;
		FCopyProgress* const CopyProgress = nullptr;
		static_cast<void>(IFileManager::Get().Copy(*LogDstAbsolute, *LogSrcAbsolute, bReplace, bEvenIfReadOnly, bAttributes, CopyProgress, FILEREAD_AllowWrite, FILEWRITE_AllowRead));	// best effort, so don't care about result: couldn't copy -> tough, no log
	}
#endif // !NO_LOGGING
}



FORCENOINLINE void FGenericCrashContext::CapturePortableCallStack(int32 NumStackFramesToIgnore, void* Context)
{
	// If the callstack is for the executing thread, ignore this function
	if(Context == nullptr)
	{
		NumStackFramesToIgnore++;
	}

	// Capture the stack trace
	static const int StackTraceMaxDepth = 100;
	uint64 StackTrace[StackTraceMaxDepth];
	FMemory::Memzero(StackTrace);
	int32 StackTraceDepth = FPlatformStackWalk::CaptureStackBackTrace(StackTrace, StackTraceMaxDepth, Context);

	// Make sure we don't exceed the current stack depth
	NumStackFramesToIgnore = FMath::Min(NumStackFramesToIgnore, StackTraceDepth);

	// Generate the portable callstack from it
	SetPortableCallStack(StackTrace + NumStackFramesToIgnore, StackTraceDepth - NumStackFramesToIgnore);
}

void FGenericCrashContext::SetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames)
{
	GetPortableCallStack(StackFrames, NumStackFrames, CallStack);
}

void FGenericCrashContext::GetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames, TArray<FCrashStackFrame>& OutCallStack)
{
	// Get all the modules in the current process
	uint32 NumModules = (uint32)FPlatformStackWalk::GetProcessModuleCount();

	TArray<FStackWalkModuleInfo> Modules;
	Modules.AddUninitialized(NumModules);

	NumModules = FPlatformStackWalk::GetProcessModuleSignatures(Modules.GetData(), NumModules);
	Modules.SetNum(NumModules);

	// Update the callstack with offsets from each module
	OutCallStack.Reset(NumStackFrames);
	for(int32 Idx = 0; Idx < NumStackFrames; Idx++)
	{
		const uint64 StackFrame = StackFrames[Idx];

		// Try to find the module containing this stack frame
		const FStackWalkModuleInfo* FoundModule = nullptr;
		for(const FStackWalkModuleInfo& Module : Modules)
		{
			if(StackFrame >= Module.BaseOfImage && StackFrame < Module.BaseOfImage + Module.ImageSize)
			{
				FoundModule = &Module;
				break;
			}
		}

		// Add the callstack item
		if(FoundModule == nullptr)
		{
			OutCallStack.Add(FCrashStackFrame(TEXT("Unknown"), 0, StackFrame));
		}
		else
		{
			OutCallStack.Add(FCrashStackFrame(FPaths::GetBaseFilename(FoundModule->ImageName), FoundModule->BaseOfImage, StackFrame - FoundModule->BaseOfImage));
		}
	}
}

void FGenericCrashContext::AddPortableThreadCallStack(uint32 ThreadId, const TCHAR* ThreadName, const uint64* StackFrames, int32 NumStackFrames)
{
	// Not implemented for generic class
}

void FGenericCrashContext::CopyPlatformSpecificFiles(const TCHAR* OutputDirectory, void* Context)
{
	// If present, include the crash report config file to pass config values to the CRC
	const TCHAR* CrashConfigSrcPath = GetCrashConfigFilePath();
	if (IFileManager::Get().FileExists(CrashConfigSrcPath))
	{
		FString CrashConfigFilename = FPaths::GetCleanFilename(CrashConfigSrcPath);
		const FString CrashConfigDstAbsolute = FPaths::Combine(OutputDirectory, *CrashConfigFilename);
		IFileManager::Get().Copy(*CrashConfigDstAbsolute, CrashConfigSrcPath);	// best effort, so don't care about result: couldn't copy -> tough, no config
	}
}

/**
 * Attempts to create the output report directory.
 */
bool FGenericCrashContext::CreateCrashReportDirectory(const TCHAR* CrashGUIDRoot, const TCHAR* AppName, int32 CrashIndex, FString& OutCrashDirectoryAbsolute)
{
	// Generate Crash GUID
	TCHAR CrashGUID[FGenericCrashContext::CrashGUIDLength];
	FCString::Snprintf(CrashGUID, FGenericCrashContext::CrashGUIDLength, TEXT("%s_%04i"), CrashGUIDRoot, CrashIndex);

	// The FPaths commands usually checks for command line override, if FCommandLine not yet
	// initialized we cannot create a directory. Also there is no way of knowing if the file manager
	// has been created.
	if (!FCommandLine::IsInitialized())
	{
		return false;
	}

	FString CrashFolder = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Crashes"), CrashGUID);
	OutCrashDirectoryAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*CrashFolder);
	return IFileManager::Get().MakeDirectory(*OutCrashDirectoryAbsolute, true);
}

FProgramCounterSymbolInfoEx::FProgramCounterSymbolInfoEx( FString InModuleName, FString InFunctionName, FString InFilename, uint32 InLineNumber, uint64 InSymbolDisplacement, uint64 InOffsetInModule, uint64 InProgramCounter ) :
	ModuleName( InModuleName ),
	FunctionName( InFunctionName ),
	Filename( InFilename ),
	LineNumber( InLineNumber ),
	SymbolDisplacement( InSymbolDisplacement ),
	OffsetInModule( InOffsetInModule ),
	ProgramCounter( InProgramCounter )
{
}

FString RecoveryService::GetRecoveryServerName()
{
	return FString::Printf(TEXT("RecoverySvr_%d"), FPlatformProcess::GetCurrentProcessId());
}
