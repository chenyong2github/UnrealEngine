// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardEditorModule.h"
#include "SwitchboardEditorSettings.h"
#include "SwitchboardMenuEntry.h"
#include "SwitchboardProjectSettings.h"
#include "SwitchboardSettingsCustomization.h"
#include "Async/Async.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeExit.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winreg.h>
#include "Windows/HideWindowsPlatformTypes.h"

static const FString WindowsRunRegKeyPath = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const FString ListenerAutolaunchRegValueName = "SwitchboardListener";

FString GetListenerAutolaunchEntry();
FString GetListenerAutolaunchEntryExecutable();
bool SetListenerAutolaunchEntry(const FString& NewCommandLine);
bool RemoveListenerAutolaunchEntry();
#endif


#define LOCTEXT_NAMESPACE "SwitchboardEditorModule"

DEFINE_LOG_CATEGORY(LogSwitchboardPlugin);


// static
const FString& FSwitchboardEditorModule::GetSbScriptsPath()
{
	using namespace UE::Switchboard::Private;
	static const FString SbScriptsPath = ConcatPaths(FPaths::EnginePluginsDir(),
		"VirtualProduction", "Switchboard", "Source", "Switchboard");
	return SbScriptsPath;
}


// static
const FString& FSwitchboardEditorModule::GetSbThirdPartyPath()
{
	using namespace UE::Switchboard::Private;
	static const FString SbThirdPartyPath = ConcatPaths(FPaths::EngineDir(),
		"Extras", "ThirdPartyNotUE", "SwitchboardThirdParty");
	return SbThirdPartyPath;
}


// static
const FString& FSwitchboardEditorModule::GetSbExePath()
{
#if PLATFORM_WINDOWS
	static const FString ExePath = GetSbScriptsPath() / TEXT("switchboard.bat");
#elif PLATFORM_LINUX
	static const FString ExePath = GetSbScriptsPath() / TEXT("switchboard.sh");
#endif

	return ExePath;
}


void FSwitchboardEditorModule::StartupModule()
{
	FSwitchboardMenuEntry::Register();

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		ISettingsSectionPtr EditorSettingsSection = SettingsModule->RegisterSettings("Editor", "Plugins", "Switchboard",
			LOCTEXT("EditorSettingsName", "Switchboard"),
			LOCTEXT("EditorSettingsDescription", "Configure the Switchboard launcher."),
			GetMutableDefault<USwitchboardEditorSettings>()
		);

		ISettingsSectionPtr ProjectSettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "Switchboard",
			LOCTEXT("ProjectSettingsName", "Switchboard"),
			LOCTEXT("ProjectSettingsDescription", "Configure the Switchboard launcher."),
			GetMutableDefault<USwitchboardProjectSettings>()
		);

		EditorSettingsSection->OnModified().BindRaw(this, &FSwitchboardEditorModule::OnEditorSettingsModified);
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(USwitchboardEditorSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FSwitchboardEditorSettingsCustomization::MakeInstance));
	PropertyModule.RegisterCustomClassLayout(USwitchboardProjectSettings::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FSwitchboardProjectSettingsCustomization::MakeInstance));

	DeferredStartDelegateHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FSwitchboardEditorModule::OnEngineInitComplete);

#if SB_LISTENER_AUTOLAUNCH
	bCachedAutolaunchEnabled = GetListenerAutolaunchEnabled_Internal();
#endif
}


void FSwitchboardEditorModule::ShutdownModule()
{
	if (!IsRunningCommandlet() && UObjectInitialized() && !IsEngineExitRequested())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(USwitchboardEditorSettings::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(USwitchboardProjectSettings::StaticClass()->GetFName());

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Editor", "Plugins", "Switchboard");
			SettingsModule->UnregisterSettings("Project", "Plugins", "Switchboard");
		}

		FSwitchboardMenuEntry::Unregister();
	}
}


bool FSwitchboardEditorModule::LaunchSwitchboard()
{
	const FString ScriptArgs = FString::Printf(TEXT("\"%s\""),
		*GetDefault<USwitchboardEditorSettings>()->VirtualEnvironmentPath.Path);

	return RunProcess(GetSbExePath(), ScriptArgs);
}


bool FSwitchboardEditorModule::LaunchListener()
{
	const FString ListenerPath = GetDefault<USwitchboardEditorSettings>()->GetListenerPlatformPath();
	const FString ListenerArgs = GetDefault<USwitchboardEditorSettings>()->ListenerCommandlineArguments;
	return RunProcess(ListenerPath, ListenerArgs);
}


bool FSwitchboardEditorModule::RunProcess(const FString& InExe, const FString& InArgs)
{
	const bool bLaunchDetached = false;
	const bool bLaunchHidden = false;
	const bool bLaunchReallyHidden = false;
	uint32* OutProcessId = nullptr;
	const int32 PriorityModifier = 0;
	const TCHAR* WorkingDirectory = nullptr;
	void* PipeWriteChild = nullptr;

	const FProcHandle Handle = FPlatformProcess::CreateProc(*InExe, *InArgs, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, OutProcessId, PriorityModifier, WorkingDirectory, PipeWriteChild);
	return Handle.IsValid();
}


FSwitchboardEditorModule::ESwitchboardInstallState FSwitchboardEditorModule::GetSwitchboardInstallState()
{
	TSharedFuture<FSwitchboardVerifyResult> Result = GetVerifyResult();

	if (!Result.IsReady())
	{
		return ESwitchboardInstallState::VerifyInProgress;
	}

	if (Result.Get().Summary != FSwitchboardVerifyResult::ESummary::Success)
	{
		return ESwitchboardInstallState::NeedInstallOrRepair;
	}

#if SWITCHBOARD_SHORTCUTS
	const bool bListenerDesktopShortcutExists = DoesShortcutExist(EShortcutApp::Listener, EShortcutLocation::Desktop) == EShortcutCompare::AlreadyExists;
	const bool bListenerProgramsShortcutExists = DoesShortcutExist(EShortcutApp::Listener, EShortcutLocation::Programs) == EShortcutCompare::AlreadyExists;
	const bool bAppDesktopShortcutExists = DoesShortcutExist(EShortcutApp::Switchboard, EShortcutLocation::Desktop) == EShortcutCompare::AlreadyExists;
	const bool bAppProgramsShortcutExists = DoesShortcutExist(EShortcutApp::Switchboard, EShortcutLocation::Programs) == EShortcutCompare::AlreadyExists;

	if (!bListenerDesktopShortcutExists || !bListenerProgramsShortcutExists
		|| !bAppDesktopShortcutExists || !bAppProgramsShortcutExists)
	{
		return ESwitchboardInstallState::ShortcutsMissing;
	}
#endif // #if SWITCHBOARD_SHORTCUTS

	return ESwitchboardInstallState::Nominal;
}


TSharedFuture<FSwitchboardVerifyResult> FSwitchboardEditorModule::GetVerifyResult(bool bForceRefresh /* = false */)
{
	const FString CurrentVenv = GetDefault<USwitchboardEditorSettings>()->VirtualEnvironmentPath.Path;

	if (bForceRefresh || !VerifyResult.IsValid() || VerifyPath != CurrentVenv)
	{
#if SWITCHBOARD_SHORTCUTS
		CachedShortcutCompares.Empty();
#endif

		UE_LOG(LogSwitchboardPlugin, Log, TEXT("Issuing verify for venv: %s"), *CurrentVenv);

		TFuture<FSwitchboardVerifyResult> Future = FSwitchboardVerifyResult::RunVerify(CurrentVenv);
		Future = Future.Next([Venv = CurrentVenv](const FSwitchboardVerifyResult& Result)
		{
			UE_LOG(LogSwitchboardPlugin, Log, TEXT("Verify complete for venv: %s"), *Venv);
			UE_LOG(LogSwitchboardPlugin, Log, TEXT("Verify summary: %d"), Result.Summary);
			UE_LOG(LogSwitchboardPlugin, Log, TEXT("Verify log: %s"), *Result.Log);

			// On platforms where we support creating shortcuts, once shortcuts have been created,
			// we hide our toolbar button to yield space.
#if SWITCHBOARD_SHORTCUTS
			// This task gets us from the future back onto the main thread prior to manipulating the UI.
			Async(EAsyncExecution::TaskGraphMainThread, []() {
				if (FSwitchboardEditorModule::Get().GetSwitchboardInstallState() == ESwitchboardInstallState::Nominal)
				{
					FSwitchboardMenuEntry::RemoveMenu();
				}
				else
				{
					FSwitchboardMenuEntry::AddMenu();
				}
			});
#endif

			return Result;
		});

		VerifyPath = CurrentVenv;
		VerifyResult = Future.Share();
	}

	return VerifyResult;
}


#if SB_LISTENER_AUTOLAUNCH
bool FSwitchboardEditorModule::IsListenerAutolaunchEnabled(bool bForceRefreshCache /* = false */)
{
	if (bForceRefreshCache)
	{
		bCachedAutolaunchEnabled = GetListenerAutolaunchEnabled_Internal();
	}

	return bCachedAutolaunchEnabled;
}


bool FSwitchboardEditorModule::GetListenerAutolaunchEnabled_Internal() const
{
	const FString& ExistingCmd = GetListenerAutolaunchEntry().TrimEnd();
	const FString& ConfigCmd = GetDefault<USwitchboardEditorSettings>()->GetListenerInvocation().TrimEnd();
	return ExistingCmd == ConfigCmd;
}


bool FSwitchboardEditorModule::SetListenerAutolaunchEnabled(bool bEnabled)
{
	bool bSucceeded = false;

	if (bEnabled)
	{
		const FString CommandLine = GetDefault<USwitchboardEditorSettings>()->GetListenerInvocation();
		bSucceeded = SetListenerAutolaunchEntry(CommandLine);
	}
	else
	{
		bSucceeded = RemoveListenerAutolaunchEntry();
	}

	bCachedAutolaunchEnabled = GetListenerAutolaunchEnabled_Internal();
	return bSucceeded;
}
#endif // #if SB_LISTENER_AUTOLAUNCH


#if SWITCHBOARD_SHORTCUTS
FSwitchboardEditorModule::EShortcutCompare FSwitchboardEditorModule::DoesShortcutExist(
	EShortcutApp App,
	EShortcutLocation Location,
	bool bForceRefreshCache /* = false */
)
{
	using namespace UE::Switchboard::Private::Shorcuts;

	const TPair<EShortcutApp, EShortcutLocation> CacheKey{ App, Location };

	if (!bForceRefreshCache)
	{
		if (const EShortcutCompare* CachedResult = CachedShortcutCompares.Find(CacheKey))
		{
			return *CachedResult;
		}
	}

	const FShortcutParams ExpectedParams = BuildShortcutParams(App, Location);
	const EShortcutCompare Result = CompareShortcut(ExpectedParams);
	CachedShortcutCompares.FindOrAdd(CacheKey) = Result;
	return Result;
}


bool FSwitchboardEditorModule::CreateOrUpdateShortcut(EShortcutApp App, EShortcutLocation Location)
{
	using namespace UE::Switchboard::Private::Shorcuts;

	const FShortcutParams Params = BuildShortcutParams(App, Location);
	const bool bResult = UE::Switchboard::Private::Shorcuts::CreateOrUpdateShortcut(Params);

	const bool bForceUpdateCache = true;
	(void)DoesShortcutExist(App, Location, bForceUpdateCache);

	return bResult;
}
#endif // #if SWITCHBOARD_SHORTCUTS


void FSwitchboardEditorModule::OnEngineInitComplete()
{
	FCoreDelegates::OnFEngineLoopInitComplete.Remove(DeferredStartDelegateHandle);
	DeferredStartDelegateHandle.Reset();

	RunDefaultOSCListener();

	// Populate initial verification results.
	GetVerifyResult(true);
}


bool FSwitchboardEditorModule::OnEditorSettingsModified()
{
#if SB_LISTENER_AUTOLAUNCH
	// If the existing entry's listener executable path matches our current engine / path,
	// then we ensure the command line arguments also stay in sync with our settings.
	const FString AutolaunchEntryExecutable = GetListenerAutolaunchEntryExecutable();
	if (!AutolaunchEntryExecutable.IsEmpty())
	{
		const FString ConfigExecutable = GetDefault<USwitchboardEditorSettings>()->GetListenerPlatformPath();
		if (AutolaunchEntryExecutable == ConfigExecutable)
		{
			UE_LOG(LogSwitchboardPlugin, Log, TEXT("Updating listener auto-launch entry"));
			SetListenerAutolaunchEnabled(true);
		}
		else
		{
			UE_LOG(LogSwitchboardPlugin, Log, TEXT("NOT updating listener auto-launch; paths differ:\n\t%s\n\t%s"),
				*AutolaunchEntryExecutable, *ConfigExecutable);
		}
	}
#endif // #if SB_LISTENER_AUTOLAUNCH

	return true;
}


void FSwitchboardEditorModule::RunDefaultOSCListener()
{
	USwitchboardProjectSettings* SwitchboardProjectSettings = USwitchboardProjectSettings::GetSwitchboardProjectSettings();
	FSoftObjectPath SwitchboardOSCListener = SwitchboardProjectSettings->SwitchboardOSCListener;
	if (SwitchboardOSCListener.IsValid())
	{
		UObject* SwitchboardOSCListenerObject = SwitchboardOSCListener.TryLoad();
		if (SwitchboardOSCListenerObject && IsValidChecked(SwitchboardOSCListenerObject) && !SwitchboardOSCListenerObject->IsUnreachable() && GEditor)
		{
			GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>()->TryRun(SwitchboardOSCListenerObject);
		}
	}
}


#if PLATFORM_WINDOWS
HKEY OpenHkcuRunKey()
{
	HKEY HkcuRunKey = nullptr;
	const LSTATUS OpenResult = RegOpenKeyEx(HKEY_CURRENT_USER, *WindowsRunRegKeyPath, 0, KEY_ALL_ACCESS, &HkcuRunKey);
	if (OpenResult != ERROR_SUCCESS)
	{
		UE_LOG(LogSwitchboardPlugin, Log, TEXT("Error opening registry key %s (%08X)"), *WindowsRunRegKeyPath, OpenResult);
		return nullptr;
	}
	return HkcuRunKey;
}


FString GetListenerAutolaunchEntry()
{
	HKEY HkcuRunKey = OpenHkcuRunKey();
	if (!HkcuRunKey)
	{
		return FString();
	}

	ON_SCOPE_EXIT
	{
		RegCloseKey(HkcuRunKey);
	};

	DWORD ValueType;
	DWORD ValueSizeBytes = 0;
	const LSTATUS SizeResult = RegQueryValueEx(HkcuRunKey, *ListenerAutolaunchRegValueName, nullptr, &ValueType, nullptr, &ValueSizeBytes);
	if (SizeResult == ERROR_FILE_NOT_FOUND)
	{
		return FString();
	}
	else if (SizeResult != ERROR_SUCCESS)
	{
		UE_LOG(LogSwitchboardPlugin, Log, TEXT("Error reading registry value %s:\"%s\" (%08X)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, SizeResult);
		return FString();
	}

	FString Value;
	TArray<TCHAR, FString::AllocatorType>& CharArray = Value.GetCharArray();
	const uint32 ValueLenChars = ValueSizeBytes / sizeof(TCHAR);
	CharArray.SetNumUninitialized(ValueLenChars);

	const LSTATUS QueryResult = RegQueryValueEx(HkcuRunKey, *ListenerAutolaunchRegValueName, nullptr, &ValueType, reinterpret_cast<LPBYTE>(CharArray.GetData()), &ValueSizeBytes);
	if (QueryResult != ERROR_SUCCESS)
	{
		UE_LOG(LogSwitchboardPlugin, Log, TEXT("Error reading registry value %s:\"%s\" (%08X)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, QueryResult);
		return FString();
	}
	else if (ValueType != REG_SZ)
	{
		UE_LOG(LogSwitchboardPlugin, Log, TEXT("Registry value %s:\"%s\" has wrong type (%u)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, ValueType);
		return FString();
	}

	if (CharArray[CharArray.Num() - 1] != TEXT('\0'))
	{
		CharArray.Add(TEXT('\0'));
	}

	return Value;
}


FString GetListenerAutolaunchEntryExecutable()
{
	FString AutolaunchCommand = GetListenerAutolaunchEntry();
	if (AutolaunchCommand.IsEmpty())
	{
		return FString();
	}

	TArray<FString> QuoteParts;
	AutolaunchCommand.ParseIntoArray(QuoteParts, TEXT("\""));
	return QuoteParts[0];
}


bool SetListenerAutolaunchEntry(const FString& NewCommandLine)
{
	HKEY HkcuRunKey = OpenHkcuRunKey();
	if (!HkcuRunKey)
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		RegCloseKey(HkcuRunKey);
	};

	const TArray<TCHAR, FString::AllocatorType>& CharArray = NewCommandLine.GetCharArray();
	const LSTATUS SetResult = RegSetValueEx(HkcuRunKey, *ListenerAutolaunchRegValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(CharArray.GetData()), CharArray.Num() * sizeof(TCHAR));
	if (SetResult != ERROR_SUCCESS)
	{
		UE_LOG(LogSwitchboardPlugin, Error, TEXT("Error setting registry value %s:\"%s\" (%08X)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, SetResult);
		return false;
	}

	return true;
}


bool RemoveListenerAutolaunchEntry()
{
	HKEY HkcuRunKey = OpenHkcuRunKey();
	if (!HkcuRunKey)
	{
		return true;
	}

	ON_SCOPE_EXIT
	{
		RegCloseKey(HkcuRunKey);
	};

	const LSTATUS DeleteResult = RegDeleteValue(HkcuRunKey, *ListenerAutolaunchRegValueName);
	if (DeleteResult != ERROR_SUCCESS)
	{
		UE_LOG(LogSwitchboardPlugin, Error, TEXT("Error deleting registry value %s:\"%s\" (%08X)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, DeleteResult);
		return false;
	}

	return true;
}
#endif // #if PLATFORM_WINDOWS


IMPLEMENT_MODULE(FSwitchboardEditorModule, SwitchboardEditor);

#undef LOCTEXT_NAMESPACE
