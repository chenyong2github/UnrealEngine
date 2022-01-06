// Copyright Epic Games, Inc. All Rights Reserved.
#include "ZenServerInterface.h"


#include "ZenBackendUtils.h"
#include "ZenServerHttp.h"
#include "ZenSerialization.h"

#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/Platform.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "String/LexFromString.h"

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include <shellapi.h>
#	include <synchapi.h>
#	include <Windows.h>
#	include "Windows/HideWindowsPlatformTypes.h"
#endif

#define ALLOW_SETTINGS_OVERRIDE_FROM_COMMANDLINE			(UE_SERVER || !(UE_BUILD_SHIPPING))

namespace UE::Zen
{

DEFINE_LOG_CATEGORY_STATIC(LogZenServiceInstance, Log, All);

static bool
AttemptFileCopyWithRetries(const TCHAR* Dst, const TCHAR* Src, double RetryDurationSeconds)
{
	uint32 CopyResult = IFileManager::Get().Copy(Dst, Src, true, true, false);
	uint64 CopyWaitStartTime = FPlatformTime::Cycles64();
	while (CopyResult != COPY_OK)
	{
		double CopyWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - CopyWaitStartTime);
		if (CopyWaitDuration < RetryDurationSeconds)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		else
		{
			break;
		}
		CopyResult = IFileManager::Get().Copy(Dst, Src, true, true, false);
	}

	return CopyResult == COPY_OK;
}

static void
DetermineLocalDataCachePath(const TCHAR* ConfigSection, FString& DataPath)
{
	FString DataPathEnvOverride;
	if (GConfig->GetString(ConfigSection, TEXT("LocalDataCachePathEnvOverride"), DataPathEnvOverride, GEngineIni))
	{
		FString DataPathEnvOverrideValue = FPlatformMisc::GetEnvironmentVariable(*DataPathEnvOverride);
		if (!DataPathEnvOverrideValue.IsEmpty())
		{
			DataPath = DataPathEnvOverrideValue;
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found environment variable %s=%s"), *DataPathEnvOverride, *DataPathEnvOverrideValue);
		}

		if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("GlobalDataCachePath"), *DataPathEnvOverride, DataPathEnvOverrideValue))
		{
			if (!DataPathEnvOverrideValue.IsEmpty())
			{
				DataPath = DataPathEnvOverrideValue;
				UE_LOG(LogZenServiceInstance, Log, TEXT("Found registry key GlobalDataCachePath %s=%s"), *DataPathEnvOverride, *DataPath);
			}
		}
	}

	FString DataPathEditorOverrideSetting;
	if (GConfig->GetString(ConfigSection, TEXT("LocalDataCachePathEditorOverrideSetting"), DataPathEditorOverrideSetting, GEngineIni))
	{
		FString Setting = GConfig->GetStr(TEXT("/Script/UnrealEd.EditorSettings"), *DataPathEditorOverrideSetting, GEditorSettingsIni);
		if (!Setting.IsEmpty())
		{
			FString SettingPath;
			if (FParse::Value(*Setting, TEXT("Path="), SettingPath))
			{
				SettingPath = SettingPath.TrimQuotes();
				if (!SettingPath.IsEmpty())
				{
					DataPath = SettingPath;
					UE_LOG(LogZenServiceInstance, Log, TEXT("Found editor setting /Script/UnrealEd.EditorSettings.Path=%s"), *DataPath);
				}
			}
		}
	}
}

static void
DetermineDataPath(const TCHAR* ConfigSection, FString& DataPath)
{
	auto NormalizeDataPath = [](const FString& InDataPath)
	{
		FString FinalPath = FPaths::ConvertRelativePathToFull(InDataPath);
		FPaths::NormalizeDirectoryName(FinalPath);
		return FinalPath;
	};

	// Zen commandline
	FString CommandLineOverrideValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("ZenDataPath="), CommandLineOverrideValue))
	{
		DataPath = NormalizeDataPath(CommandLineOverrideValue);
		UE_LOG(LogZenServiceInstance, Log, TEXT("Found command line override ZenDataPath=%s"), *CommandLineOverrideValue);
		return;
	}

	// Zen registry/stored
	FString DataPathEnvOverrideValue;
	if (FPlatformMisc::GetStoredValue(TEXT("Epic Games"), TEXT("Zen"), TEXT("DataPath"), DataPathEnvOverrideValue))
	{
		if (!DataPathEnvOverrideValue.IsEmpty())
		{
			DataPath = NormalizeDataPath(DataPathEnvOverrideValue);
			UE_LOG(LogZenServiceInstance, Log, TEXT("Found registry key Zen DataPath=%s"), *DataPath);
			return;
		}
	}

	// Zen environment
	DataPathEnvOverrideValue = FPlatformMisc::GetEnvironmentVariable(TEXT("UE-ZenDataPath"));
	if (!DataPathEnvOverrideValue.IsEmpty())
	{
		DataPath = DataPathEnvOverrideValue;
		UE_LOG(LogZenServiceInstance, Log, TEXT("Found environment variable UE-ZenDataPath=%s"), *DataPathEnvOverrideValue);
		return;
	}

	// Follow local DDC (if outside workspace)
	FString LocalDataCachePath;
	DetermineLocalDataCachePath(ConfigSection, LocalDataCachePath);
	if (!LocalDataCachePath.IsEmpty() && (LocalDataCachePath != TEXT("None")) && !FPaths::IsUnderDirectory(LocalDataCachePath, FPaths::RootDir()))
	{
		DataPath = FPaths::Combine(LocalDataCachePath, TEXT("Zen"));
		return;
	}

	// Zen config default
	GConfig->GetString(ConfigSection, TEXT("DataPath"), DataPath, GEngineIni);

	check(!DataPath.IsEmpty())
}

static void
ReadUInt16FromConfig(const TCHAR* Section, const TCHAR* Key, uint16& Value, const FString& ConfigFile)
{
	int32 ValueInt32 = Value;
	GConfig->GetInt(Section, Key, ValueInt32, ConfigFile);
	Value = (uint16)ValueInt32;
}

void
FServiceSettings::ReadFromConfig()
{
	check(GConfig && GConfig->IsReadyForUse());
	const TCHAR* ConfigSection = TEXT("Zen");
	bool bAutoLaunch = true;
	GConfig->GetBool(ConfigSection, TEXT("AutoLaunch"), bAutoLaunch, GEngineIni);

	if (bAutoLaunch)
	{
		if (!TryApplyAutoLaunchOverride())
		{
			// AutoLaunch settings
			const TCHAR* AutoLaunchConfigSection = TEXT("Zen.AutoLaunch");
			SettingsVariant.Emplace<FServiceAutoLaunchSettings>();
			FServiceAutoLaunchSettings& AutoLaunchSettings = SettingsVariant.Get<FServiceAutoLaunchSettings>();

			DetermineDataPath(AutoLaunchConfigSection, AutoLaunchSettings.DataPath);
			GConfig->GetString(AutoLaunchConfigSection, TEXT("ExtraArgs"), AutoLaunchSettings.ExtraArgs, GEngineIni);

			ReadUInt16FromConfig(AutoLaunchConfigSection, TEXT("DesiredPort"), AutoLaunchSettings.DesiredPort, GEngineIni);
			GConfig->GetBool(AutoLaunchConfigSection, TEXT("ShowConsole"), AutoLaunchSettings.bShowConsole, GEngineIni);
			GConfig->GetBool(AutoLaunchConfigSection, TEXT("LimitProcessLifetime"), AutoLaunchSettings.bLimitProcessLifetime, GEngineIni);
		}
	}
	else
	{
		// ConnectExisting settings
		const TCHAR* ConnectExistingConfigSection = TEXT("Zen.ConnectExisting");
		SettingsVariant.Emplace<FServiceConnectSettings>();
		FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

		GConfig->GetString(ConnectExistingConfigSection, TEXT("HostName"), ConnectExistingSettings.HostName, GEngineIni);
		ReadUInt16FromConfig(ConnectExistingConfigSection, TEXT("Port"), ConnectExistingSettings.Port, GEngineIni);
	}
}

void
FServiceSettings::ReadFromJson(FJsonObject& JsonObject)
{
	if (TSharedPtr<FJsonValue> bAutoLaunchValue = JsonObject.Values.FindRef(TEXT("bAutoLaunch")))
	{
		if (bAutoLaunchValue->AsBool())
		{
			if (!TryApplyAutoLaunchOverride())
			{
				SettingsVariant.Emplace<FServiceAutoLaunchSettings>();
				FServiceAutoLaunchSettings& AutoLaunchSettings = SettingsVariant.Get<FServiceAutoLaunchSettings>();

				TSharedPtr<FJsonValue> AutoLaunchSettingsValue = JsonObject.Values.FindRef(TEXT("AutoLaunchSettings"));
				if (AutoLaunchSettingsValue)
				{
					TSharedPtr<FJsonObject> AutoLaunchSettingsObject = AutoLaunchSettingsValue->AsObject();
					AutoLaunchSettings.DataPath = AutoLaunchSettingsObject->Values.FindRef(TEXT("DataPath"))->AsString();
					AutoLaunchSettings.ExtraArgs = AutoLaunchSettingsObject->Values.FindRef(TEXT("ExtraArgs"))->AsString();
					AutoLaunchSettingsObject->Values.FindRef(TEXT("DesiredPort"))->TryGetNumber(AutoLaunchSettings.DesiredPort);
					AutoLaunchSettingsObject->Values.FindRef(TEXT("bShowConsole"))->TryGetBool(AutoLaunchSettings.bShowConsole);
					AutoLaunchSettingsObject->Values.FindRef(TEXT("bLimitProcessLifetime"))->TryGetBool(AutoLaunchSettings.bLimitProcessLifetime);
					if (TSharedPtr<FJsonValue> TreatAsBuildMachineValue = AutoLaunchSettingsObject->Values.FindRef(TEXT("TreatAsBuildMachine")))
					{
						if (TreatAsBuildMachineValue->AsString() == FPlatformProcess::ComputerName())
						{
							AutoLaunchSettings.bLimitProcessLifetime = true;
						}
					}
				}
			}
		}
		else
		{
			SettingsVariant.Emplace<FServiceConnectSettings>();
			FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

			TSharedPtr<FJsonValue> ConnectExistingSettingsValue = JsonObject.Values.FindRef(TEXT("ConnectExistingSettings"));
			if (ConnectExistingSettingsValue)
			{
				TSharedPtr<FJsonObject> ConnectExistingSettingsObject = ConnectExistingSettingsValue->AsObject();
				ConnectExistingSettings.HostName = ConnectExistingSettingsObject->Values.FindRef(TEXT("HostName"))->AsString();
				ConnectExistingSettingsObject->Values.FindRef(TEXT("Port"))->TryGetNumber(ConnectExistingSettings.Port);
			}
		}

	}
}

void
FServiceSettings::ReadFromURL(FStringView InstanceURL)
{
	SettingsVariant.Emplace<FServiceConnectSettings>();
	FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();

	if (InstanceURL.StartsWith(TEXT("http://")))
	{
		InstanceURL.RightChopInline(7);
	}

	int32 PortDelimIndex = INDEX_NONE;
	InstanceURL.FindChar(TEXT(':'), PortDelimIndex);
	if (PortDelimIndex != INDEX_NONE)
	{
		ConnectExistingSettings.HostName = InstanceURL.Left(PortDelimIndex);
		LexFromString(ConnectExistingSettings.Port, InstanceURL.RightChop(PortDelimIndex + 1));
	}
	else
	{
		ConnectExistingSettings.HostName = InstanceURL;
		ConnectExistingSettings.Port = 1337;
	}
}

void
FServiceSettings::WriteToJson(TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>& Writer) const
{
	bool bAutoLaunch = IsAutoLaunch();
	Writer.WriteValue(TEXT("bAutoLaunch"), bAutoLaunch);
	if (bAutoLaunch)
	{
		const FServiceAutoLaunchSettings& AutoLaunchSettings = SettingsVariant.Get<FServiceAutoLaunchSettings>();
		Writer.WriteObjectStart(TEXT("AutoLaunchSettings"));
		Writer.WriteValue(TEXT("DataPath"), AutoLaunchSettings.DataPath);
		Writer.WriteValue(TEXT("ExtraArgs"), AutoLaunchSettings.ExtraArgs);
		Writer.WriteValue(TEXT("DesiredPort"), AutoLaunchSettings.DesiredPort);
		Writer.WriteValue(TEXT("bShowConsole"), AutoLaunchSettings.bShowConsole);
		Writer.WriteValue(TEXT("bLimitProcessLifetime"), AutoLaunchSettings.bLimitProcessLifetime);
		if (GIsBuildMachine)
		{
			Writer.WriteValue(TEXT("TreatAsBuildMachine"), FPlatformProcess::ComputerName());
		}
		Writer.WriteObjectEnd();
	}
	else
	{
		const FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();
		Writer.WriteObjectStart(TEXT("ConnectExistingSettings"));
		Writer.WriteValue(TEXT("HostName"), ConnectExistingSettings.HostName);
		Writer.WriteValue(TEXT("Port"), ConnectExistingSettings.Port);
		Writer.WriteObjectEnd();
	}
}

bool
FServiceSettings::TryApplyAutoLaunchOverride()
{
#if ALLOW_SETTINGS_OVERRIDE_FROM_COMMANDLINE
	if (FParse::Param(FCommandLine::Get(), TEXT("NoZenAutoLaunch")))
	{
		SettingsVariant.Emplace<FServiceConnectSettings>();
		FServiceConnectSettings& ConnectExistingSettings = SettingsVariant.Get<FServiceConnectSettings>();
		ConnectExistingSettings.HostName = TEXT("localhost");
		ConnectExistingSettings.Port = 1337;
		return true;
	}
#endif
	return false;
}

#if UE_WITH_ZEN

static bool
ReadCbLockFile(FStringView FileName, FCbObject& OutLockObject)
{
#if PLATFORM_WINDOWS
	// Windows specific lock reading path
	// Uses share flags that are unique to windows to allow us to read file contents while the file may be open for write AND delete by another process (zenserver).

	uint32 Access = GENERIC_READ;
	uint32 WinFlags = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
	uint32 Create = OPEN_EXISTING;

	TStringBuilder<MAX_PATH> FullFileNameBuilder;
	FPathViews::ToAbsolutePath(FileName, FullFileNameBuilder);
	for (TCHAR& Char : MakeArrayView(FullFileNameBuilder))
	{
		if (Char == '/')
		{
			Char = '\\';
		}
	}
	if (FullFileNameBuilder.Len() >= MAX_PATH)
	{
		FullFileNameBuilder.Prepend(TEXT("\\\\?\\"_SV));
	}
	HANDLE Handle = CreateFileW(FullFileNameBuilder.ToString(), Access, WinFlags, NULL, Create, FILE_ATTRIBUTE_NORMAL, NULL);
	if (Handle != INVALID_HANDLE_VALUE)
	{
		ON_SCOPE_EXIT { CloseHandle(Handle); };
		LARGE_INTEGER LI;
		if (GetFileSizeEx(Handle, &LI))
		{
			checkf(LI.QuadPart == LI.u.LowPart, TEXT("Lock file exceeds supported 2GB limit."));
			int32 FileSize32 = LI.u.LowPart;
			FUniqueBuffer FileBytes = FUniqueBuffer::Alloc(FileSize32);
			DWORD ReadBytes = 0;
			if (ReadFile(Handle, FileBytes.GetData(), FileSize32, &ReadBytes, NULL) && (ReadBytes == FileSize32))
			{
				if (ValidateCompactBinary(FileBytes, ECbValidateMode::Default) == ECbValidateError::None)
				{
					OutLockObject = FCbObject(FileBytes.MoveToShared());
					return true;
				}
			}
		}
	}
	return false;
#else
	// Generic lock reading path
	if (TUniquePtr<FArchive> Ar{FileManager.CreateFileReader(*FileName, FILEREAD_AllowWrite | FILEREAD_Silent)})
	{
		*Ar << OutLockObject;
		FMemoryView View;
		if (Ar.Close() &&
			OutLockObject.TryGetView(View) &&
			ValidateCompactBinary(View, ECbValidateMode::Default) == ECbValidateError::None)
		{
			return true;
		}
	}
	return false;
#endif
}

static bool GIsDefaultServicePresent = false;

FZenServiceInstance& GetDefaultServiceInstance()
{
	static FZenServiceInstance DefaultServiceInstance;
	GIsDefaultServicePresent = true;
	return DefaultServiceInstance;
}

bool IsDefaultServicePresent()
{
	return GIsDefaultServicePresent;
}

FScopeZenService::FScopeZenService()
	: FScopeZenService(FStringView())
{
}

FScopeZenService::FScopeZenService(FStringView InstanceURL)
{
	if (!InstanceURL.IsEmpty() && !InstanceURL.Equals(TEXT("<DefaultInstance>")))
	{
		UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(InstanceURL);
		ServiceInstance = UniqueNonDefaultInstance.Get();
	}
	else
	{
		ServiceInstance = &GetDefaultServiceInstance();
	}
}

FScopeZenService::FScopeZenService(FServiceSettings&& InSettings)
{
	UniqueNonDefaultInstance = MakeUnique<FZenServiceInstance>(MoveTemp(InSettings));
	ServiceInstance = UniqueNonDefaultInstance.Get();
}

FScopeZenService::~FScopeZenService()
{}

FZenServiceInstance::FZenServiceInstance()
: FZenServiceInstance(FStringView())
{
}

FZenServiceInstance::FZenServiceInstance(FStringView InstanceURL)
{
	if (InstanceURL.IsEmpty())
	{
		Settings.ReadFromConfig();
	}
	else
	{
		Settings.ReadFromURL(InstanceURL);
	}

	Initialize();
}

FZenServiceInstance::FZenServiceInstance(FServiceSettings&& InSettings)
: Settings(MoveTemp(InSettings))
{
	Initialize();
}

FZenServiceInstance::~FZenServiceInstance()
{
}

bool 
FZenServiceInstance::IsServiceRunning()
{
	return !Settings.IsAutoLaunch() || bHasLaunchedLocal;
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

void
FZenServiceInstance::Initialize()
{
	if (Settings.IsAutoLaunch())
	{
		bHasLaunchedLocal = AutoLaunch(Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>(), ConditionalUpdateLocalInstall(), HostName, Port);
		if (bHasLaunchedLocal)
		{
			AutoLaunchedPort = Port;
		}
	}
	else
	{
		const FServiceConnectSettings& ConnectExistingSettings = Settings.SettingsVariant.Get<FServiceConnectSettings>();
		HostName = ConnectExistingSettings.HostName;
		Port = ConnectExistingSettings.Port;
	}
	URL = WriteToString<64>(TEXT("http://"), HostName, TEXT(":"), Port, TEXT("/"));
}

void
FZenServiceInstance::PromptUserToStopRunningServerInstance(const FString& ServerFilePath)
{
	if (FApp::IsUnattended())
	{
		// Do not ask if there is no one to show a message
		return;
	}

	FText ZenUpdatePromptTitle = NSLOCTEXT("Zen", "Zen_UpdatePromptTitle", "Update required");
	FText ZenUpdatePromptText = FText::Format(NSLOCTEXT("Zen", "Zen_UpdatePromptText", "ZenServer needs to be updated to a new version. Please shut down Unreal Editor and any tools that are using the ZenServer at '{0}'"), FText::FromString(ServerFilePath));
	FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ZenUpdatePromptText.ToString(), *ZenUpdatePromptTitle.ToString());
}

FString
FZenServiceInstance::ConditionalUpdateLocalInstall()
{
	FString InTreeFilePath = FPaths::ConvertRelativePathToFull(FPlatformProcess::GenerateApplicationPath(TEXT("zenserver"), EBuildConfiguration::Development));
	FString InstallFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), TEXT("ZenServer"), FString(FPathViews::GetCleanFilename(InTreeFilePath))));

	IFileManager& FileManager = IFileManager::Get();
	FDateTime InTreeFileTime;
	FDateTime InstallFileTime;
	FileManager.GetTimeStampPair(*InTreeFilePath, *InstallFilePath, InTreeFileTime, InstallFileTime);
	if (InTreeFileTime > InstallFileTime)
	{
		if (FPlatformProcess::IsApplicationRunning(*FPaths::GetCleanFilename(InstallFilePath)))
		{
			// TODO: Instead of using the lock file, this could use the shared memory system state named "Global\ZenMap" (see zenserverprocess.{h,cpp} in zen codebase)
			FString LockFilePath = FPaths::Combine(Settings.SettingsVariant.Get<FServiceAutoLaunchSettings>().DataPath, TEXT(".lock"));
			FCbObject LockObject;
			if (ReadCbLockFile(LockFilePath, LockObject))
			{
				uint16 RunningPort = LockObject["port"].AsUInt16(0);
				if (RunningPort != 0)
				{
#if PLATFORM_WINDOWS
					FString ShutdownEventName = *WriteToString<64>(TEXT("Zen_"), RunningPort, TEXT("_Shutdown"));
					HANDLE Handle = OpenEventW(EVENT_MODIFY_STATE, false, *ShutdownEventName);
					if (Handle != INVALID_HANDLE_VALUE)
					{
						ON_SCOPE_EXIT{ CloseHandle(Handle); };
						SetEvent(Handle);
					}
#else
					static_assert(false, "Missing implementation for Zen named shutdown events");
#endif

					uint64 ZenShutdownWaitStartTime = FPlatformTime::Cycles64();
					while (FileManager.FileExists(*LockFilePath))
					{
						double ZenShutdownWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenShutdownWaitStartTime);
						if (ZenShutdownWaitDuration < 5.0)
						{
							FPlatformProcess::Sleep(0.01f);
						}
						else
						{
							break;
						}
					}
				}
			}

			if (FileManager.FileExists(*LockFilePath))
			{
				PromptUserToStopRunningServerInstance(InstallFilePath);
			}
		}

		// Even after waiting for the lock file to be removed, the executable may have a period where it can't be overwritten as the process shuts down
		// so any attempt to overwrite it should have some tolerance for retrying.
		bool bExecutableCopySucceeded = AttemptFileCopyWithRetries(*InstallFilePath, *InTreeFilePath, 5.0);
		checkf(bExecutableCopySucceeded, TEXT("Failed to copy zenserver to install location '%s'."), *InstallFilePath);

#if PLATFORM_WINDOWS
		FString InTreeSymbolFilePath = FPaths::ChangeExtension(InTreeFilePath, TEXT("pdb"));
		FString InstallSymbolFilePath = FPaths::ChangeExtension(InstallFilePath, TEXT("pdb"));
		bool bSymbolCopySucceeded = AttemptFileCopyWithRetries(*InstallFilePath, *InTreeFilePath, 1.0);
		checkf(bSymbolCopySucceeded, TEXT("Failed to copy zenserver symbols to install location '%s'."), *InstallSymbolFilePath);
#endif
	}

	return InstallFilePath;
}

bool
FZenServiceInstance::AutoLaunch(const FServiceAutoLaunchSettings& InSettings, FString&& ExecutablePath, FString& OutHostName, uint16& OutPort)
{
	IFileManager& FileManager = IFileManager::Get();
	FString LockFilePath = FPaths::Combine(InSettings.DataPath, TEXT(".lock"));
	FileManager.Delete(*LockFilePath, false, false, true);

	bool bProcessIsLive = FileManager.FileExists(*LockFilePath) && FPlatformProcess::IsApplicationRunning(*FPaths::GetCleanFilename(ExecutablePath));

	// When limiting process lifetime, always re-launch to add sponsor process IDs.
	// When not limiting process lifetime, only launch if the process is not already live.
	if (InSettings.bLimitProcessLifetime || !bProcessIsLive)
	{
		FString Parms;
		Parms.Appendf(TEXT("--port %d --data-dir \"%s\""),
			InSettings.DesiredPort,
			*InSettings.DataPath);

		if (InSettings.bLimitProcessLifetime || GIsBuildMachine)
		{
			Parms.Appendf(TEXT(" --owner-pid %d"),
				FPlatformProcess::GetCurrentProcessId());
		}

		FString LogCommandLineOverrideValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("ZenLogPath="), LogCommandLineOverrideValue))
		{
			if (!LogCommandLineOverrideValue.IsEmpty())
			{
				Parms.Appendf(TEXT(" --abslog \"%s\""),
					*FPaths::ConvertRelativePathToFull(LogCommandLineOverrideValue));
			}
		}

		if (!InSettings.ExtraArgs.IsEmpty())
		{
			Parms.AppendChar(TEXT(' '));
			Parms.Append(InSettings.ExtraArgs);
		}

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
				(::DWORD)STARTF_USESHOWWINDOW,
				(::WORD)(InSettings.bShowConsole ? SW_SHOWMINNOACTIVE : SW_HIDE),
				0, NULL,
				HANDLE(nullptr),
				HANDLE(nullptr),
				HANDLE(nullptr)
			};

			FString CommandLine = FString::Printf(TEXT("\"%s\" %s"), *ExecutablePath, *Parms);
			PROCESS_INFORMATION ProcInfo;
			if (CreateProcess(NULL, CommandLine.GetCharArray().GetData(), nullptr, nullptr, false, (::DWORD)(NORMAL_PRIORITY_CLASS | DETACHED_PROCESS), nullptr, nullptr, &StartupInfo, &ProcInfo))
			{
				::CloseHandle(ProcInfo.hThread);
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
			ShellExecuteInfo.lpFile = *ExecutablePath;
			ShellExecuteInfo.lpVerb = TEXT("runas");
			ShellExecuteInfo.nShow = InSettings.bShowConsole ? SW_SHOWMINNOACTIVE : SW_HIDE;
			ShellExecuteInfo.lpParameters = *Parms;

			if (ShellExecuteEx(&ShellExecuteInfo))
			{
				Proc = FProcHandle(ShellExecuteInfo.hProcess);
			}
		}
#else
		{
			bool bLaunchDetached = true;
			bool bLaunchHidden = true;
			bool bLaunchReallyHidden = !InSettings.bShowConsole;
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
		bProcessIsLive = Proc.IsValid();
	}


	OutHostName = TEXT("localhost");
	// Default to assuming that we get to run on the port we want
	OutPort = InSettings.DesiredPort;

	if (bProcessIsLive)
	{

		FScopedSlowTask WaitForZenReadySlowTask(0, NSLOCTEXT("Zen", "Zen_WaitingForReady", "Waiting for ZenServer to be ready"));
		uint64 ZenWaitStartTime = FPlatformTime::Cycles64();
		enum class EWaitDurationPhase
		{
			Short,
			Medium,
			Long
		} DurationPhase = EWaitDurationPhase::Short;
		bool bIsReady = false;
		while (!bIsReady)
		{
			FCbObject LockObject;
			if (ReadCbLockFile(LockFilePath, LockObject))
			{
				bIsReady = LockObject["ready"].AsBool();
				if (bIsReady)
				{
					OutPort = LockObject["port"].AsUInt16(InSettings.DesiredPort);
					break;
				}
			}

			double ZenWaitDuration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - ZenWaitStartTime);
			if (ZenWaitDuration < 1.0)
			{
				// Initial 1 second window of higher frequency checks
				FPlatformProcess::Sleep(0.01f);
			}
			else
			{
				if (DurationPhase == EWaitDurationPhase::Short)
				{
					// Insist after 1 second that the lock file must exist by now otherwise there has been a failure to launch.
					// The file existing does not mean that it is ready to use, just that it is running.  It may be performing scrubbing.
					checkf(FileManager.FileExists(*LockFilePath), TEXT("ZenServer did not launch in the expected duration."));// Note that the dialog may not show up when zenserver is needed early in the launch cycle, but this will at least ensure
					// the splash screen is refreshed with the appropriate text status message.
					WaitForZenReadySlowTask.MakeDialog(true, false);
					UE_LOG(LogZenServiceInstance, Display, TEXT("Waiting for ZenServer to be ready..."));
					DurationPhase = EWaitDurationPhase::Medium;
				}
				else if (!FApp::IsUnattended() && ZenWaitDuration > 10.0 && (DurationPhase == EWaitDurationPhase::Medium))
				{
					FText ZenLongWaitPromptTitle = NSLOCTEXT("Zen", "Zen_LongWaitPromptTitle", "Wait for ZenServer?");
					FText ZenLongWaitPromptText = NSLOCTEXT("Zen", "Zen_LongWaitPromptText", "ZenServer is taking a long time to launch. It may be performing maintenance. Keep waiting?");
					if (FPlatformMisc::MessageBoxExt(EAppMsgType::YesNo, *ZenLongWaitPromptText.ToString(), *ZenLongWaitPromptTitle.ToString()) == EAppReturnType::No)
					{
						FPlatformMisc::RequestExit(true);
						return false;
					}
					DurationPhase = EWaitDurationPhase::Long;
				}

				if (WaitForZenReadySlowTask.ShouldCancel())
				{
					FPlatformMisc::RequestExit(true);
					return false;
				}
				FPlatformProcess::Sleep(0.1f);
			}
		}

		return bIsReady;
	}
	else
	{
		return false;
	}
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

		FCbObjectView CacheSizeObjectView = CacheObjectView["size"].AsObjectView();
		FZenCacheSizeStats& CacheSizeStats = CacheStats.Size;
		CacheSizeStats.Disk = CacheSizeObjectView["disk"].AsDouble();
		CacheSizeStats.Memory = CacheSizeObjectView["memory"].AsDouble();

		FCbObjectView UpstreamObjectView = RootObjectView["upstream"].AsObjectView();
		FZenUpstreamStats& UpstreamStats = stats.UpstreamStats;

		UpstreamStats.Reading = UpstreamObjectView["reading"].AsBool();
		UpstreamStats.Writing = UpstreamObjectView["writing"].AsBool();
		UpstreamStats.WorkerThreads = UpstreamObjectView["worker_threads"].AsInt64();
		UpstreamStats.QueueCount = UpstreamObjectView["queue_count"].AsInt64();
		UpstreamStats.TotalUploadedMB = 0.0;
		UpstreamStats.TotalDownloadedMB = 0.0;

		FCbObjectView UpstreamRequestObjectView = RootObjectView["upstream_gets"].AsObjectView();
		FZenRequestStats& UpstreamRequestStats = stats.UpstreamRequestStats;

		UpstreamRequestStats.Count = UpstreamRequestObjectView["count"].AsInt64();
		UpstreamRequestStats.RateMean = UpstreamRequestObjectView["rate_mean"].AsDouble();
		UpstreamRequestStats.TAverage = UpstreamRequestObjectView["t_avg"].AsDouble();
		UpstreamRequestStats.TMin = UpstreamRequestObjectView["t_min"].AsDouble();
		UpstreamRequestStats.TMax = UpstreamRequestObjectView["t_max"].AsDouble();

		FCbArrayView EndpPointArrayView = UpstreamObjectView["endpoints"].AsArrayView();

		for (FCbFieldView FieldView : EndpPointArrayView)
		{
			FCbObjectView EndPointView = FieldView.AsObjectView();
			FZenEndPointStats EndPointStats;

			EndPointStats.Name = FString(EndPointView["name"].AsString());
			EndPointStats.Url = FString(EndPointView["url"].AsString());
			EndPointStats.Health = FString(EndPointView["health"].AsString());
			EndPointStats.HitRatio = EndPointView["hit_ratio"].AsDouble();
			EndPointStats.UploadedMB = EndPointView["uploaded_mb"].AsDouble();
			EndPointStats.DownloadedMB = EndPointView["downloaded_mb"].AsDouble();
			EndPointStats.ErrorCount = EndPointView["error_count"].AsInt64();

			UpstreamStats.TotalUploadedMB += EndPointStats.UploadedMB;
			UpstreamStats.TotalDownloadedMB += EndPointStats.DownloadedMB;

			UpstreamStats.EndPointStats.Push(EndPointStats);
		}

		FCbObjectView CASObjectView = RootObjectView["cas"].AsObjectView();	
		FCbObjectView CASSizeObjectView = CASObjectView["size"].AsObjectView();

		FZenCASSizeStats& CASSizeStats = stats.CASStats.Size;

		CASSizeStats.Tiny = CASSizeObjectView["tiny"].AsInt64();
		CASSizeStats.Small = CASSizeObjectView["small"].AsInt64();
		CASSizeStats.Large = CASSizeObjectView["large"].AsInt64();
		CASSizeStats.Total = CASSizeObjectView["total"].AsInt64();

		return true;
	}

	return false;
}

#endif // UE_WITH_ZEN

}

