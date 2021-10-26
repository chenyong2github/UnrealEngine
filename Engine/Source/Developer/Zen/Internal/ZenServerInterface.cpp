// Copyright Epic Games, Inc. All Rights Reserved.
#include "ZenServerInterface.h"

#if UE_WITH_ZEN

#include "ZenBackendUtils.h"
#include "ZenServerHttp.h"
#include "ZenSerialization.h"

#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "String/LexFromString.h"

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include <shellapi.h>
#	include <Windows.h>
#	include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UE::Zen {

DEFINE_LOG_CATEGORY_STATIC(LogZenServiceInstance, Log, All);

FZenServiceInstance& GetDefaultServiceInstance()
{
	static FZenServiceInstance DefaultServiceInstance;
	return DefaultServiceInstance;
}

FScopeZenService::FScopeZenService()
	: FScopeZenService(FStringView())
{
}

FScopeZenService::FScopeZenService(FStringView InstanceURL)
{
	if (!InstanceURL.IsEmpty() && !InstanceURL.Equals(TEXT("<DefaultInstance>")))
	{
		UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(Default, InstanceURL);
		ServiceInstance = UniqueNonDefaultInstance.Get();
	}
	else
	{
		ServiceInstance = &GetDefaultServiceInstance();
	}
}

FScopeZenService::FScopeZenService(FStringView InstanceHostName, uint16 InstancePort)
{
	if (!InstanceHostName.IsEmpty() && !InstanceHostName.Equals(TEXT("<DefaultInstance>")))
	{
		TStringBuilder<64> URL;
		URL.AppendAnsi("http://");
		URL.Append(InstanceHostName);
		URL.AppendAnsi(":");
		URL << InstancePort;

		UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(Default, URL);
		ServiceInstance = UniqueNonDefaultInstance.Get();
	}
	else
	{
		ServiceInstance = &GetDefaultServiceInstance();
	}
}

FScopeZenService::FScopeZenService(FStringView AutoLaunchExecutablePath, FStringView AutoLaunchArguments, uint16 DesiredPort)
{
	UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(ForcedLaunch, AutoLaunchExecutablePath, AutoLaunchArguments, DesiredPort);
	ServiceInstance = UniqueNonDefaultInstance.Get();
}

FScopeZenService::FScopeZenService(EServiceMode Mode)
{
	switch (Mode)
	{
		case Default:
		{
			ServiceInstance = &GetDefaultServiceInstance();
		}
		break;
		case DefaultNoLaunch:
		{
			UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(Mode, FStringView());
			ServiceInstance = UniqueNonDefaultInstance.Get();
		}
		break;
		default:
		unimplemented();
	}

}

FScopeZenService::~FScopeZenService()
{}

FZenServiceInstance::FZenServiceInstance()
: FZenServiceInstance(Default, FStringView())
{
}

FZenServiceInstance::FZenServiceInstance(EServiceMode Mode, FStringView AutoLaunchExecutablePath, FStringView AutoLaunchArguments, uint16 DesiredPort)
{
	if (Mode == ForcedLaunch)
	{
		Settings.bAutoLaunch = true;
		HostName = TEXT("localhost");
		Port = DesiredPort;
		bHasLaunchedLocal = AutoLaunch(AutoLaunchExecutablePath, AutoLaunchArguments, Port);
		
		if (bHasLaunchedLocal)
		{
			AutoLaunchedPort = Port;
		}

		TStringBuilder<128> URLBuilder;
		URLBuilder << TEXT("http://") << HostName << TEXT(":") << Port << TEXT("/");
		URL = URLBuilder.ToString();
	}
	else
	{
		unimplemented();
	}
}

FZenServiceInstance::FZenServiceInstance(EServiceMode Mode, FStringView InstanceURL)
{
	Settings.AutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineVersionAgnosticUserDir(), TEXT("Zen")));
	Settings.AutoLaunchSettings.WorkspaceDataPath = Settings.AutoLaunchSettings.DataPath;
	PopulateSettings(InstanceURL);

	if (Settings.bAutoLaunch)
	{
		if (Mode == DefaultNoLaunch)
		{
			Settings.bAutoLaunch = false;
			HostName = TEXT("localhost");
			Port = (AutoLaunchedPort != 0) ? AutoLaunchedPort : Settings.AutoLaunchSettings.DesiredPort;
		}
		else
		{
			bHasLaunchedLocal = AutoLaunch();
			if (bHasLaunchedLocal)
			{
				AutoLaunchedPort = Port;
			}
		}
	}
	else
	{
		HostName = Settings.ConnectExistingSettings.HostName;
		Port = Settings.ConnectExistingSettings.Port;
	}
	TStringBuilder<128> URLBuilder;
	URLBuilder << TEXT("http://") << HostName << TEXT(":") << Port << TEXT("/");
	URL = URLBuilder.ToString();
}

FZenServiceInstance::~FZenServiceInstance()
{
}

bool 
FZenServiceInstance::IsServiceRunning()
{
	return !Settings.bAutoLaunch || bHasLaunchedLocal;
}

bool 
FZenServiceInstance::IsServiceReady()
{
	if (IsServiceRunning())
	{
		TStringBuilder<128> ZenDomain;
		ZenDomain << HostName << TEXT(":") << Port;
		Zen::FZenHttpRequest Request(ZenDomain.ToString(), false);
		Zen::FZenHttpRequest::Result Result = Request.PerformBlockingDownload(TEXT("health/ready"_SV), nullptr, Zen::EContentType::Text);
		
		if (Result == Zen::FZenHttpRequest::Result::Success && Zen::IsSuccessCode(Request.GetResponseCode()))
		{
			UE_LOG(LogZenServiceInstance, Display, TEXT("Z$ HTTP DDC service status: %s."), *Request.GetResponseAsString());
			return true;
		}
		else
		{
			UE_LOG(LogZenServiceInstance, Warning, TEXT("Unable to reach Z$ HTTP DDC service at %s. Status: %d . Response: %s"), ZenDomain.ToString(), Request.GetResponseCode(), *Request.GetResponseAsString());
		}
	}
	return false;
}

static void DetermineDataPath(const TCHAR* ConfigSection, FString& DataPath)
{
	GConfig->GetString(ConfigSection, TEXT("DataPath"), DataPath, GEngineIni);

	bool bUsedEnvOverride = false;
	bool bUsedEditorOverride = false;
	// Much of the logic here is meant to mirror the way that DDC backend paths can be specified to allow
	// us to match behavior and not behave differently in the presence of custom config
	FString DataPathEnvOverride;
	if (GConfig->GetString(ConfigSection, TEXT("DataPathEnvOverride"), DataPathEnvOverride, GEngineIni))
	{
		FString DataPathEnvOverrideValue = FPlatformMisc::GetEnvironmentVariable(*DataPathEnvOverride);
		if(!DataPathEnvOverrideValue.IsEmpty())
		{
			DataPath = DataPathEnvOverrideValue;
			bUsedEnvOverride = true;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found environment variable %s=%s"), *DataPathEnvOverride, *DataPathEnvOverrideValue);
		}

		if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *DataPathEnvOverride, DataPathEnvOverrideValue))
		{
			if (!DataPathEnvOverrideValue.IsEmpty())
			{
				DataPath = DataPathEnvOverrideValue;
				bUsedEnvOverride = true;
				UE_LOG(LogZenServiceInstance, Log, TEXT("Found registry key GlobalDataCachePath %s=%s"), *DataPathEnvOverride, *DataPath);
			}
		}
	}

	FString DataPathEditorOverrideSetting;
	if (GConfig->GetString(ConfigSection, TEXT("DataPathEditorOverrideSetting"), DataPathEditorOverrideSetting, GEngineIni))
	{
		FString Setting = GConfig->GetStr(TEXT("/Script/UnrealEd.EditorSettings"), *DataPathEditorOverrideSetting, GEditorSettingsIni);
		if(!Setting.IsEmpty())
		{
			FString SettingPath;
			if(FParse::Value(*Setting, TEXT("Path="), SettingPath))
			{
				SettingPath = SettingPath.TrimQuotes();
				if(!SettingPath.IsEmpty())
				{
					DataPath = SettingPath;
					bUsedEditorOverride = true;
					UE_LOG(LogZenServiceInstance, Log, TEXT("Found editor setting /Script/UnrealEd.EditorSettings.Path=%s"), *DataPath);
				}
			}
		}
	}

	if (bUsedEnvOverride || bUsedEditorOverride)
	{
		DataPath = FPaths::Combine(DataPath, TEXT("Zen"));
	}

	{
		FString CommandLineOverrideValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("ZenDataPath="), CommandLineOverrideValue))
		{
			DataPath = CommandLineOverrideValue;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found command line override ZenDataPath=%s"), *CommandLineOverrideValue);
		}
	}
}

static void
ReadUInt16FromConfig(const TCHAR* Section, const TCHAR* Key, uint16& Value, const FString& ConfigFile)
{
	int32 ValueInt32 = Value;
	GConfig->GetInt(Section, Key, ValueInt32, ConfigFile);
	Value = (uint16)ValueInt32;
}


static void
ParseURLToHostNameAndPort(FStringView URL, FString& OutHostName, uint16& OutPort)
{
	if (URL.StartsWith(TEXT("http://")))
	{
		URL.RightChopInline(7);
	}

	FStringView::SizeType PortDelimIndex = INDEX_NONE;
	URL.FindChar(TEXT(':'), PortDelimIndex);
	if (PortDelimIndex != INDEX_NONE)
	{
		OutHostName = URL.Left(PortDelimIndex);
		LexFromString(OutPort, URL.RightChop(PortDelimIndex + 1));
	}
	else
	{
		OutHostName = URL;
		OutPort = 1337;
	}
}

void
FZenServiceInstance::PopulateSettings(FStringView InstanceURL)
{
	// Allow a provided InstanceURL to override everything
	if (!InstanceURL.IsEmpty())
	{
		Settings.bAutoLaunch = false;
		ParseURLToHostNameAndPort(InstanceURL, Settings.ConnectExistingSettings.HostName, Settings.ConnectExistingSettings.Port);
		return;
	}

	check(GConfig && GConfig->IsReadyForUse());
	const TCHAR* ConfigSection = TEXT("Zen");
	GConfig->GetBool(ConfigSection, TEXT("AutoLaunch"), Settings.bAutoLaunch, GEngineIni);

	// AutoLaunch settings
	{
		const TCHAR* AutoLaunchConfigSection = TEXT("Zen.AutoLaunch");

		// Workspace path is for marker files that we don't want in a shared location out of tree.
		GConfig->GetString(AutoLaunchConfigSection, TEXT("WorkspaceDataPath"), Settings.AutoLaunchSettings.WorkspaceDataPath, GEngineIni);
		DetermineDataPath(AutoLaunchConfigSection, Settings.AutoLaunchSettings.DataPath);
		Settings.AutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(Settings.AutoLaunchSettings.DataPath);
		GConfig->GetString(AutoLaunchConfigSection, TEXT("ExtraArgs"), Settings.AutoLaunchSettings.ExtraArgs, GEngineIni);

		ReadUInt16FromConfig(AutoLaunchConfigSection, TEXT("DesiredPort"), Settings.AutoLaunchSettings.DesiredPort, GEngineIni);
		GConfig->GetBool(AutoLaunchConfigSection, TEXT("Hidden"), Settings.AutoLaunchSettings.bHidden, GEngineIni);

		FString LogCommandLineOverrideValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("ZenLogPath="), LogCommandLineOverrideValue))
		{
			Settings.AutoLaunchSettings.LogPath = LogCommandLineOverrideValue;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found command line override ZenLogPath=%s"), *LogCommandLineOverrideValue);
		}
	}

	// ConnectExisting settings
	{
		const TCHAR* ConnectExistingConfigSection = TEXT("Zen.ConnectExisting");
		GConfig->GetString(ConnectExistingConfigSection, TEXT("HostName"), Settings.ConnectExistingSettings.HostName, GEngineIni);
		ReadUInt16FromConfig(ConnectExistingConfigSection, TEXT("Port"), Settings.ConnectExistingSettings.Port, GEngineIni);
	}
}

void
FZenServiceInstance::PromptUserToStopRunningServerInstance(const FString& ServerFilePath)
{
	if (FApp::IsUnattended())
	{
		// Do not ask if there is no one to show a message
		return;
	}

	FString UserPrompt = FString::Printf(TEXT("Please shutdown currently running Unreal editor and tools using zenserver at '%s' so that it can be upgraded to a new version."), *ServerFilePath);
	FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *UserPrompt, TEXT("Upgrade required"));
}

FString
FZenServiceInstance::ConditionalUpdateLocalInstall()
{
	FString InTreeFilePath = FPaths::ConvertRelativePathToFull(FPlatformProcess::GenerateApplicationPath(TEXT("zenserver"), EBuildConfiguration::Development));

	FString InstallFilePath = GetAutoLaunchExecutablePath(FPathViews::GetCleanFilename(InTreeFilePath));

	IFileManager& FileManager = IFileManager::Get();
	FDateTime InTreeFileTime;
	FDateTime InstallFileTime;
	FileManager.GetTimeStampPair(*InTreeFilePath, *InstallFilePath, InTreeFileTime, InstallFileTime);
	if (InTreeFileTime > InstallFileTime)
	{
		if (FPlatformProcess::IsApplicationRunning(*FPaths::GetCleanFilename(InstallFilePath)))
		{
			PromptUserToStopRunningServerInstance(InstallFilePath);
		}

		uint32 CopyResult = FileManager.Copy(*InstallFilePath, *InTreeFilePath, true, true, false);
		checkf(CopyResult == COPY_OK, TEXT("Failed to copy zenserver to install location '%s'."), *InstallFilePath);

#if PLATFORM_WINDOWS
		FString InstallSymbolFilePath = FPaths::ChangeExtension(InstallFilePath, TEXT("pdb"));
		CopyResult = FileManager.Copy(*InstallSymbolFilePath, *FPaths::ChangeExtension(InTreeFilePath, TEXT("pdb")), true, true, false);
		checkf(CopyResult == COPY_OK, TEXT("Failed to copy zenserver symbols to install location '%s'."), *InstallSymbolFilePath);
#endif
	}

	return InstallFilePath;
}

FString
FZenServiceInstance::GetAutoLaunchExecutablePath(FStringView CleanExecutableFileName) const
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), TEXT("ZenServer"), FString(CleanExecutableFileName)));
}

FString
FZenServiceInstance::GetAutoLaunchExecutablePath() const
{
	FString ExecutableFileName = FPlatformProcess::GenerateApplicationPath(TEXT("zenserver"), EBuildConfiguration::Development);

	return GetAutoLaunchExecutablePath(FPathViews::GetCleanFilename(ExecutableFileName));
}

FString
FZenServiceInstance::GetAutoLaunchArguments() const
{
	FString Parms;

	Parms.Appendf(TEXT("--port %d --data-dir \"%s\""),
		Settings.AutoLaunchSettings.DesiredPort,
		*Settings.AutoLaunchSettings.DataPath);

	if (!Settings.AutoLaunchSettings.LogPath.IsEmpty())
	{
		Parms.Appendf(TEXT(" --abslog \"%s\""),
			*FPaths::ConvertRelativePathToFull(Settings.AutoLaunchSettings.LogPath));
	}

	if (!Settings.AutoLaunchSettings.ExtraArgs.IsEmpty())
	{
		Parms.AppendChar(TEXT(' '));
		Parms.Append(Settings.AutoLaunchSettings.ExtraArgs);
	}
	return Parms;
}

bool
FZenServiceInstance::AutoLaunch()
{
	// TODO: Install and update will be delegated to be the responsibility of the launched zenserver process in the future.
	FString MainFilePath = ConditionalUpdateLocalInstall();

	FString Parms = GetAutoLaunchArguments();

	return AutoLaunch(MainFilePath, Parms, Settings.AutoLaunchSettings.DesiredPort);
}

bool
FZenServiceInstance::AutoLaunch(FStringView AutoLaunchExecutablePath, FStringView AutoLaunchArguments, uint16 DesiredPort)
{
	HostName = TEXT("localhost");
	// For now assuming that we get to run on the port we want
	Port = DesiredPort;

	FString FinalExecutablePath(MoveTemp(AutoLaunchExecutablePath));
	FString FinalParms;
	FinalParms.Appendf(TEXT("--owner-pid %d "),
		FPlatformProcess::GetCurrentProcessId());
#if PLATFORM_WINDOWS
	const TCHAR* ReadyEventName = TEXT("Local\\ZenAutoLaunchReadyCheck");
	HANDLE ReadyEvent = ::CreateEvent(NULL, false, false, ReadyEventName);
	FinalParms.Appendf(TEXT(" --child-id \"%s\" "),
		ReadyEventName);
#endif
	FinalParms += MoveTemp(AutoLaunchArguments);

	FProcHandle Proc;
#if PLATFORM_WINDOWS
	{
		// Attempt non-elevated launch
		STARTUPINFO StartupInfo = {
			sizeof(STARTUPINFO),
			NULL, NULL, NULL,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)CW_USEDEFAULT,
			(::DWORD)0, (::DWORD)0, (::DWORD)0,
			(::DWORD) STARTF_USESHOWWINDOW,
			(::WORD)(Settings.AutoLaunchSettings.bHidden ? SW_HIDE : SW_SHOWMINNOACTIVE),
			0, NULL,
			HANDLE(nullptr),
			HANDLE(nullptr),
			HANDLE(nullptr)
		};

		FString CommandLine = FString::Printf(TEXT("\"%s\" %s"), *FinalExecutablePath, *FinalParms);
		PROCESS_INFORMATION ProcInfo;
		if (CreateProcess(NULL, CommandLine.GetCharArray().GetData(), nullptr, nullptr, false, (::DWORD)(NORMAL_PRIORITY_CLASS | DETACHED_PROCESS), nullptr, nullptr, &StartupInfo, &ProcInfo))
		{
			::CloseHandle( ProcInfo.hThread );
			Proc = FProcHandle(ProcInfo.hProcess);
		}

	}
	if (!Proc.IsValid())
	{
		// Fall back to elevated launch
		SHELLEXECUTEINFO ShellExecuteInfo;
		ZeroMemory(&ShellExecuteInfo, sizeof(ShellExecuteInfo));
		ShellExecuteInfo.cbSize = sizeof(ShellExecuteInfo);
		ShellExecuteInfo.fMask = SEE_MASK_UNICODE | SEE_MASK_NOCLOSEPROCESS;
		ShellExecuteInfo.lpFile = *FinalExecutablePath;
		ShellExecuteInfo.lpVerb = TEXT("runas");
		ShellExecuteInfo.nShow = Settings.AutoLaunchSettings.bHidden ? SW_HIDE : SW_SHOWMINNOACTIVE;
		ShellExecuteInfo.lpParameters = *FinalParms;

		if (ShellExecuteEx(&ShellExecuteInfo))
		{
			Proc = FProcHandle(ShellExecuteInfo.hProcess);
		}
	}
#else
	{
		bool bLaunchDetached = true;
		bool bLaunchHidden = true;
		bool bLaunchReallyHidden = Settings.AutoLaunchSettings.bHidden;
		uint32* OutProcessID = nullptr;
		int32 PriorityModifier = 0;
		const TCHAR* OptionalWorkingDirectory = nullptr;
		void* PipeWriteChild = nullptr;
		void* PipeReadChild = nullptr;
		Proc = FPlatformProcess::CreateProc(
			*MainFilePath,
			*Parms,
			bLaunchDetached,
			bLaunchHidden,
			bLaunchReallyHidden,
			OutProcessID,
			PriorityModifier,
			OptionalWorkingDirectory,
			PipeWriteChild,
			PipeReadChild);
	}
#endif

	if (!Proc.IsValid())
	{
		return false;
	}
#if PLATFORM_WINDOWS
	if (ReadyEvent)
	{
		UE_LOG(LogZenServiceInstance, Display, TEXT("Waiting for Zen service to come online..."));
		bool bIsReady = WAIT_OBJECT_0 == ::WaitForSingleObject(ReadyEvent, 4000);
		::CloseHandle(ReadyEvent);
		UE_CLOG(!bIsReady, LogZenServiceInstance, Warning, TEXT("AutoLaunched Zen service was not ready after 4 seconds"));
	}
#endif
	return true;
}

bool 
FZenServiceInstance::GetStats( FZenStats& stats ) const
{
	TStringBuilder<128> ZenDomain;
	ZenDomain << HostName << TEXT(":") << Port;
	UE::Zen::FZenHttpRequest Request(ZenDomain.ToString(), false);

	TArray64<uint8> GetBuffer;
	FZenHttpRequest::Result Result = Request.PerformBlockingDownload(TEXT("/stats/z$"_SV), &GetBuffer, Zen::EContentType::CbObject);

	if (Result == Zen::FZenHttpRequest::Result::Success && Request.GetResponseCode() == 200)
	{
		FCbObjectView RootObjectView(GetBuffer.GetData());

		FCbObjectView RequestsObjectView = RootObjectView["requests"].AsObjectView();
		FZenRequestStats& RequestStats = stats.RequestStats;
		
		RequestStats.Count = RequestsObjectView["count"].AsInt64();
		RequestStats.RateMean = RequestsObjectView["rate_mean"].AsDouble();
		RequestStats.TAverage = RequestsObjectView["t_avg"].AsDouble();
		RequestStats.TMin = RequestsObjectView["t_min"].AsDouble();
		RequestStats.TMax = RequestsObjectView["t_max"].AsDouble();

		FCbObjectView CacheObjectView = RootObjectView["cache"].AsObjectView();
		FZenCacheStats& CacheStats = stats.CacheStats;

		CacheStats.Hits = CacheObjectView["hits"].AsInt64();
		CacheStats.Misses = CacheObjectView["misses"].AsInt64();
		CacheStats.HitRatio = CacheObjectView["hit_ratio"].AsDouble();
		CacheStats.UpstreamHits = CacheObjectView["upstream_hits"].AsInt64();
		CacheStats.UpstreamRatio = CacheObjectView["upstream_ratio"].AsDouble();

		FCbObjectView UpstreamObjectView = RootObjectView["upstream"].AsObjectView();
		FZenUpstreamStats& UpstreamStats = stats.UpstreamStats;

		UpstreamStats.Reading = UpstreamObjectView["reading"].AsBool();
		UpstreamStats.Writing = UpstreamObjectView["writing"].AsBool();
		UpstreamStats.WorkerThreads = UpstreamObjectView["worker_threads"].AsInt64();
		UpstreamStats.QueueCount = UpstreamObjectView["queue_count"].AsInt64();
		UpstreamStats.TotalUploadedMB = 0.0;
		UpstreamStats.TotalDownloadedMB = 0.0;

		FCbObjectView UpstreamGetsObjectView = RootObjectView["upstream_gets"].AsObjectView();
		FZenRequestStats& UpstreamGetsStats = stats.UpstreamGetsStats;

		UpstreamGetsStats.Count = UpstreamGetsObjectView["count"].AsInt64();
		UpstreamGetsStats.RateMean = UpstreamGetsObjectView["rate_mean"].AsDouble();
		UpstreamGetsStats.TAverage = UpstreamGetsObjectView["t_avg"].AsDouble();
		UpstreamGetsStats.TMin = UpstreamGetsObjectView["t_min"].AsDouble();
		UpstreamGetsStats.TMax = UpstreamGetsObjectView["t_max"].AsDouble();

		FCbArrayView EndpPointArrayView = UpstreamObjectView["endpoints"].AsArrayView();

		for (FCbFieldView FieldView : EndpPointArrayView)
		{
			FCbObjectView EndPointView = FieldView.AsObjectView();
			FZenEndPointStats EndPointStats;

			EndPointStats.Name = FString(EndPointView["name"].AsString());
			EndPointStats.Health = FString(EndPointView["health"].AsString());
			EndPointStats.HitRatio = EndPointView["hit_ratio"].AsDouble();
			EndPointStats.UploadedMB = EndPointView["uploaded_mb"].AsDouble();
			EndPointStats.DownloadedMB = EndPointView["downloaded_mb"].AsDouble();
			EndPointStats.ErrorCount = EndPointView["error_count"].AsInt64();

			UpstreamStats.TotalUploadedMB += EndPointStats.UploadedMB;
			UpstreamStats.TotalDownloadedMB += EndPointStats.DownloadedMB;

			UpstreamStats.EndPointStats.Push(EndPointStats);
		}

		return true;
	}

	return false;
}


}

#endif // UE_WITH_ZEN
