// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardEditorModule.h"

#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeExit.h"
#include "SwitchboardEditorSettings.h"
#include "SwitchboardMenuEntry.h"

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


void FSwitchboardEditorModule::StartupModule()
{
	FSwitchboardMenuEntry::Register();

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "Switchboard",
			LOCTEXT("SettingsName", "Switchboard"),
			LOCTEXT("Description", "Configure the Switchboard launcher."),
			GetMutableDefault<USwitchboardEditorSettings>()
		);

		SettingsSection->OnModified().BindRaw(this, &FSwitchboardEditorModule::OnSettingsModified);
	}

	DeferredStartDelegateHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FSwitchboardEditorModule::OnEngineInitComplete);
}


void FSwitchboardEditorModule::ShutdownModule()
{
	if (!IsRunningCommandlet() && UObjectInitialized() && !IsEngineExitRequested())
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Switchboard");
		}
		FSwitchboardMenuEntry::Unregister();
	}
}


#if PLATFORM_WINDOWS
bool FSwitchboardEditorModule::IsListenerAutolaunchEnabled() const
{
	const FString& ExistingCmd = GetListenerAutolaunchEntry().TrimEnd();
	const FString& ConfigCmd = GetDefault<USwitchboardEditorSettings>()->GetListenerInvocation().TrimEnd();
	return ExistingCmd == ConfigCmd;
}


bool FSwitchboardEditorModule::SetListenerAutolaunchEnabled(bool bEnabled)
{
	if (bEnabled)
	{
		const FString CommandLine = GetDefault<USwitchboardEditorSettings>()->GetListenerInvocation();
		return SetListenerAutolaunchEntry(CommandLine);
	}
	else
	{
		return RemoveListenerAutolaunchEntry();
	}
}
#endif // #if PLATFORM_WINDOWS


void FSwitchboardEditorModule::OnEngineInitComplete()
{
	FCoreDelegates::OnFEngineLoopInitComplete.Remove(DeferredStartDelegateHandle);
	DeferredStartDelegateHandle.Reset();

	RunDefaultOSCListener();
}


bool FSwitchboardEditorModule::OnSettingsModified()
{
#if PLATFORM_WINDOWS
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
#endif // #if PLATFORM_WINDOWS

	return true;
}


void FSwitchboardEditorModule::RunDefaultOSCListener()
{
	USwitchboardEditorSettings* SwitchboardEditorSettings = USwitchboardEditorSettings::GetSwitchboardEditorSettings();
	FSoftObjectPath SwitchboardOSCListener = SwitchboardEditorSettings->SwitchboardOSCListener;
	if (SwitchboardOSCListener.IsValid())
	{
		UObject* SwitchboardOSCListenerObject = SwitchboardOSCListener.TryLoad();
		if (SwitchboardOSCListenerObject && !SwitchboardOSCListenerObject->IsPendingKillOrUnreachable() && GEditor)
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
		UE_LOG(LogSwitchboardPlugin, Error, TEXT("Error opening registry key %s (%08X)"), *WindowsRunRegKeyPath, OpenResult);
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
		UE_LOG(LogSwitchboardPlugin, Error, TEXT("Error reading registry value %s:\"%s\" (%08X)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, SizeResult);
		return FString();
	}

	FString Value;
	TArray<TCHAR>& CharArray = Value.GetCharArray();
	const uint32 ValueLenChars = ValueSizeBytes / sizeof(TCHAR);
	CharArray.SetNumUninitialized(ValueLenChars);

	const LSTATUS QueryResult = RegQueryValueEx(HkcuRunKey, *ListenerAutolaunchRegValueName, nullptr, &ValueType, reinterpret_cast<LPBYTE>(CharArray.GetData()), &ValueSizeBytes);
	if (QueryResult != ERROR_SUCCESS)
	{
		UE_LOG(LogSwitchboardPlugin, Error, TEXT("Error reading registry value %s:\"%s\" (%08X)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, QueryResult);
		return FString();
	}
	else if (ValueType != REG_SZ)
	{
		UE_LOG(LogSwitchboardPlugin, Error, TEXT("Registry value %s:\"%s\" has wrong type (%u)"), *WindowsRunRegKeyPath, *ListenerAutolaunchRegValueName, ValueType);
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

	const TArray<TCHAR>& CharArray = NewCommandLine.GetCharArray();
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
