// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveCodingModule.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "LiveCodingLog.h"
#include "External/LC_EntryPoint.h"
#include "External/LC_API.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "LiveCodingSettings.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Windows/WindowsHWrapper.h"

IMPLEMENT_MODULE(FLiveCodingModule, LiveCoding)

#define LOCTEXT_NAMESPACE "LiveCodingModule"

bool GIsCompileActive = false;
bool GHasLoadedPatch = false;
FString GLiveCodingConsolePath;
FString GLiveCodingConsoleArguments;

#if IS_MONOLITHIC
extern const TCHAR* GLiveCodingEngineDir;
extern const TCHAR* GLiveCodingProject;
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextern-initializer"
#endif

LPP_PRECOMPILE_HOOK(FLiveCodingModule::PreCompileHook);
LPP_POSTCOMPILE_HOOK(FLiveCodingModule::PostCompileHook);

#ifdef __clang__
#pragma clang diagnostic pop
#endif

FLiveCodingModule::FLiveCodingModule()
	: bEnabledLastTick(false)
	, bEnabledForSession(false)
	, bStarted(false)
	, bUpdateModulesInTick(false)
	, FullEnginePluginsDir(FPaths::ConvertRelativePathToFull(FPaths::EnginePluginsDir()))
	, FullProjectDir(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()))
	, FullProjectPluginsDir(FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir()))
{
}

void FLiveCodingModule::StartupModule()
{
	Settings = GetMutableDefault<ULiveCodingSettings>();

	IConsoleManager& ConsoleManager = IConsoleManager::Get();

	EnableCommand = ConsoleManager.RegisterConsoleCommand(
		TEXT("LiveCoding"),
		TEXT("Enables live coding support"),
		FConsoleCommandDelegate::CreateRaw(this, &FLiveCodingModule::EnableForSession, true),
		ECVF_Cheat
	);

	CompileCommand = ConsoleManager.RegisterConsoleCommand(
		TEXT("LiveCoding.Compile"),
		TEXT("Initiates a live coding compile"),
		FConsoleCommandDelegate::CreateRaw(this, &FLiveCodingModule::Compile),
		ECVF_Cheat
	);

#if IS_MONOLITHIC
	FString DefaultEngineDir = GLiveCodingEngineDir;
#else
	FString DefaultEngineDir = FPaths::EngineDir();
#endif
#if USE_DEBUG_LIVE_CODING_CONSOLE
	static const TCHAR* DefaultConsolePath = TEXT("Binaries/Win64/LiveCodingConsole-Win64-Debug.exe");
#else
	static const TCHAR* DefaultConsolePath = TEXT("Binaries/Win64/LiveCodingConsole.exe");
#endif 
	ConsolePathVariable = ConsoleManager.RegisterConsoleVariable(
		TEXT("LiveCoding.ConsolePath"),
		FPaths::ConvertRelativePathToFull(DefaultEngineDir / DefaultConsolePath),
		TEXT("Path to the live coding console application"),
		ECVF_Cheat
	);

#if IS_MONOLITHIC
	FString SourceProject = (GLiveCodingProject != nullptr)? GLiveCodingProject : TEXT("");
#else
	FString SourceProject = FPaths::IsProjectFilePathSet() ? FPaths::GetProjectFilePath() : TEXT("");
#endif
	SourceProjectVariable = ConsoleManager.RegisterConsoleVariable(
		TEXT("LiveCoding.SourceProject"),
		FPaths::ConvertRelativePathToFull(SourceProject),
		TEXT("Path to the project that this target was built from"),
		ECVF_Cheat
	);

	EndFrameDelegateHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FLiveCodingModule::Tick);

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		SettingsSection = SettingsModule->RegisterSettings("Editor", "General", "Live Coding",
			LOCTEXT("LiveCodingSettingsName", "Live Coding"),
			LOCTEXT("LiveCodintSettingsDescription", "Settings for recompiling C++ code while the engine is running."),
			GetMutableDefault<ULiveCodingSettings>()
		);
	}

	LppStartup(hInstance);

	if (Settings->bEnabled && !FApp::IsUnattended())
	{
		if(Settings->Startup == ELiveCodingStartupMode::Automatic)
		{
			StartLiveCoding();
			ShowConsole();
		}
		else if(Settings->Startup == ELiveCodingStartupMode::AutomaticButHidden)
		{
			GLiveCodingConsoleArguments = L"-Hidden";
			StartLiveCoding();
		}
	}

	if(FParse::Param(FCommandLine::Get(), TEXT("LiveCoding")))
	{
		StartLiveCoding();
	}

	bEnabledLastTick = Settings->bEnabled;
}

void FLiveCodingModule::ShutdownModule()
{
	LppShutdown();

	FCoreDelegates::OnEndFrame.Remove(EndFrameDelegateHandle);

	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	ConsoleManager.UnregisterConsoleObject(SourceProjectVariable);
	ConsoleManager.UnregisterConsoleObject(ConsolePathVariable);
	ConsoleManager.UnregisterConsoleObject(CompileCommand);
	ConsoleManager.UnregisterConsoleObject(EnableCommand);
}

void FLiveCodingModule::EnableByDefault(bool bEnable)
{
	if(Settings->bEnabled != bEnable)
	{
		Settings->bEnabled = bEnable;
		if(SettingsSection.IsValid())
		{
			SettingsSection->Save();
		}
	}
	EnableForSession(bEnable);
}

bool FLiveCodingModule::IsEnabledByDefault() const
{
	return Settings->bEnabled;
}

void FLiveCodingModule::EnableForSession(bool bEnable)
{
	if (bEnable)
	{
		if(!bStarted)
		{
			StartLiveCoding();
			ShowConsole();
		}
		else
		{
			bEnabledForSession = true;
			ShowConsole();
		}
	}
	else 
	{
		if(bStarted)
		{
			UE_LOG(LogLiveCoding, Display, TEXT("Console will be hidden but remain running in the background. Restart to disable completely."));
			LppSetActive(false);
			LppSetVisible(false);
			bEnabledForSession = false;
		}
	}
}

bool FLiveCodingModule::IsEnabledForSession() const
{
	return bEnabledForSession;
}

bool FLiveCodingModule::CanEnableForSession() const
{
#if !IS_MONOLITHIC
	FModuleManager& ModuleManager = FModuleManager::Get();
	if(ModuleManager.HasAnyOverridenModuleFilename())
	{
		return false;
	}
#endif
	return true;
}

bool FLiveCodingModule::HasStarted() const
{
	return bStarted;
}

void FLiveCodingModule::ShowConsole()
{
	if (bStarted)
	{
		LppSetVisible(true);
		LppSetActive(true);
		LppShowConsole();
	}
}

void FLiveCodingModule::Compile()
{
	if(!GIsCompileActive)
	{
		EnableForSession(true);
		if(bStarted)
		{
			UpdateModules(); // Need to do this immediately rather than waiting until next tick
			LppTriggerRecompile();
			GIsCompileActive = true;
		}
	}
}

bool FLiveCodingModule::IsCompiling() const
{
	return GIsCompileActive;
}

void FLiveCodingModule::Tick()
{
	if (LppWantsRestart())
	{
		LppRestart(lpp::LPP_RESTART_BEHAVIOR_REQUEST_EXIT, 0);
	}

	if (Settings->bEnabled != bEnabledLastTick && Settings->Startup != ELiveCodingStartupMode::Manual)
	{
		EnableForSession(Settings->bEnabled);
		bEnabledLastTick = Settings->bEnabled;
		if (IsEnabledByDefault() && !IsEnabledForSession())
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoEnableLiveCodingAfterHotReload", "Live Coding cannot be enabled while hot-reloaded modules are active. Please close the editor and build from your IDE before restarting."));
		}
	}

	if (bUpdateModulesInTick)
	{
		UpdateModules();
		bUpdateModulesInTick = false;
	}

	AttemptSyncLivePatching();
}

void FLiveCodingModule::AttemptSyncLivePatching()
{
	while (LppPendingTokens.Num() > 0)
	{
		if (LppTryWaitForToken(LppPendingTokens[0]))
		{
			LppPendingTokens.RemoveAt(0);
		}
		else
		{
			return;
		}
	}

	// Needs to happen after updating modules, since "Quick Restart" functionality may try to install patch immediately
	extern void LppSyncPoint();
	LppSyncPoint();

	if (GHasLoadedPatch)
	{
		OnPatchCompleteDelegate.Broadcast();
		GHasLoadedPatch = false;
	}
}

ILiveCodingModule::FOnPatchCompleteDelegate& FLiveCodingModule::GetOnPatchCompleteDelegate()
{
	return OnPatchCompleteDelegate;
}

bool FLiveCodingModule::StartLiveCoding()
{
	if(!bStarted)
	{
		// Make sure there aren't any hot reload modules already active
		if (!CanEnableForSession())
		{
			UE_LOG(LogLiveCoding, Error, TEXT("Unable to start live coding session. Some modules have already been hot reloaded."));
			return false;
		}

		// Setup the console path
		GLiveCodingConsolePath = ConsolePathVariable->GetString();
		if (!FPaths::FileExists(GLiveCodingConsolePath))
		{
			UE_LOG(LogLiveCoding, Error, TEXT("Unable to start live coding session. Missing executable '%s'. Use the LiveCoding.ConsolePath console variable to modify."), *GLiveCodingConsolePath);
			return false;
		}

		// Get the source project filename
		FString SourceProject = SourceProjectVariable->GetString();
		if (SourceProject.Len() > 0 && !FPaths::FileExists(SourceProject))
		{
			UE_LOG(LogLiveCoding, Error, TEXT("Unable to start live coding session. Unable to find source project file '%s'."), *SourceProject);
			return false;
		}

		UE_LOG(LogLiveCoding, Display, TEXT("Starting LiveCoding"));

		// Enable external build system
		LppUseExternalBuildSystem();

		// Enable the server
		FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()).ToLower();
		FString ProcessGroup = FString::Printf(TEXT("UE4_%s_0x%08x"), FApp::GetProjectName(), GetTypeHash(ProjectPath));
		LppRegisterProcessGroup(TCHAR_TO_ANSI(*ProcessGroup));

		// Build the command line
		FString KnownTargetName = FPlatformMisc::GetUBTTargetName();
		FString Arguments = FString::Printf(TEXT("%s %s %s"),
			*KnownTargetName,
			FPlatformMisc::GetUBTPlatform(),
			LexToString(FApp::GetBuildConfiguration()));

		UE_LOG(LogLiveCoding, Display, TEXT("LiveCodingConsole Arguments: %s"), *Arguments);

		if(SourceProject.Len() > 0)
		{
			Arguments += FString::Printf(TEXT(" -Project=\"%s\""), *FPaths::ConvertRelativePathToFull(SourceProject));
		}
		LppSetBuildArguments(*Arguments);

		// Create a mutex that allows UBT to detect that we shouldn't hot-reload into this executable. The handle to it will be released automatically when the process exits.
		FString ExecutablePath = FPaths::ConvertRelativePathToFull(FPlatformProcess::ExecutablePath());

		FString MutexName = TEXT("Global\\LiveCoding_");
		for (int Idx = 0; Idx < ExecutablePath.Len(); Idx++)
		{
			TCHAR Character = ExecutablePath[Idx];
			if (Character == '/' || Character == '\\' || Character == ':')
			{
				MutexName += '+';
			}
			else
			{
				MutexName += Character;
			}
		}

		ensure(CreateMutex(NULL, Windows::FALSE, *MutexName));

		// Configure all the current modules. For non-commandlets, schedule it to be done in the first Tick() so we can batch everything together.
		if (IsRunningCommandlet())
		{
			UpdateModules();
		}
		else
		{
			bUpdateModulesInTick = true;
		}

		// Register a delegate to listen for new modules loaded from this point onwards
		ModulesChangedDelegateHandle = FModuleManager::Get().OnModulesChanged().AddRaw(this, &FLiveCodingModule::OnModulesChanged);

		// Mark it as started
		bStarted = true;
		bEnabledForSession = true;
	}
	return true;
}

void FLiveCodingModule::UpdateModules()
{
	if (bEnabledForSession)
	{
#if IS_MONOLITHIC
		wchar_t FullFilePath[WINDOWS_MAX_PATH];
		verify(GetModuleFileName(hInstance, FullFilePath, UE_ARRAY_COUNT(FullFilePath)));
		LppEnableModule(FullFilePath);
#else
		TArray<FModuleStatus> ModuleStatuses;
		FModuleManager::Get().QueryModules(ModuleStatuses);

		TArray<FString> EnableModules;
		for (const FModuleStatus& ModuleStatus : ModuleStatuses)
		{
			if (ModuleStatus.bIsLoaded)
			{
				FName ModuleName(*ModuleStatus.Name);
				if (!ConfiguredModules.Contains(ModuleName))
				{
					FString FullFilePath = FPaths::ConvertRelativePathToFull(ModuleStatus.FilePath);
					if (ShouldPreloadModule(ModuleName, FullFilePath))
					{
						EnableModules.Add(FullFilePath);
					}
					else
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LppEnableLazyLoadedModule);
						void* LppEnableLazyLoadedModuleToken = LppEnableLazyLoadedModule(*FullFilePath);
						LppPendingTokens.Add(LppEnableLazyLoadedModuleToken);
					}
					ConfiguredModules.Add(ModuleName);
				}
			}
		}

		if (EnableModules.Num() > 0)
		{
			TArray<const TCHAR*> EnableModuleFileNames;
			for (const FString& EnableModule : EnableModules)
			{
				EnableModuleFileNames.Add(*EnableModule);
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LppEnableModules);
				void* LppEnableModulesToken = LppEnableModules(EnableModuleFileNames.GetData(), EnableModuleFileNames.Num());
				LppPendingTokens.Add(LppEnableModulesToken);
			}
		}
#endif
	}
}

void FLiveCodingModule::OnModulesChanged(FName ModuleName, EModuleChangeReason Reason)
{
#if !IS_MONOLITHIC
	if (Reason == EModuleChangeReason::ModuleLoaded)
	{
		// Assume that Tick() won't be called if we're running a commandlet
		if (IsRunningCommandlet())
		{
			UpdateModules();
		}
		else
		{
			bUpdateModulesInTick = true;
		}
	}
#endif
}

bool FLiveCodingModule::ShouldPreloadModule(const FName& Name, const FString& FullFilePath) const
{
	// For the hooks to work properly, we always have to load the live coding module
	if (Name == TEXT(LIVE_CODING_MODULE_NAME))
	{
		return true;
	}

	if (Settings->PreloadNamedModules.Contains(Name))
	{
		return true;
	}

	if (FullFilePath.StartsWith(FullProjectDir))
	{
		if (Settings->bPreloadProjectModules == Settings->bPreloadProjectPluginModules)
		{
			return Settings->bPreloadProjectModules;
		}

		if(FullFilePath.StartsWith(FullProjectPluginsDir))
		{
			return Settings->bPreloadProjectPluginModules;
		}
		else
		{
			return Settings->bPreloadProjectModules;
		}
	}
	else
	{
		if (FApp::IsEngineInstalled())
		{
			return false;
		}

		if (Settings->bPreloadEngineModules == Settings->bPreloadEnginePluginModules)
		{
			return Settings->bPreloadEngineModules;
		}

		if(FullFilePath.StartsWith(FullEnginePluginsDir))
		{
			return Settings->bPreloadEnginePluginModules;
		}
		else
		{
			return Settings->bPreloadEngineModules;
		}
	}
}

void FLiveCodingModule::PreCompileHook()
{
	UE_LOG(LogLiveCoding, Display, TEXT("Starting Live Coding compile."));
	GIsCompileActive = true;
}

void FLiveCodingModule::PostCompileHook()
{
	UE_LOG(LogLiveCoding, Display, TEXT("Live Coding compile done.  See Live Coding console for more information."));
	GIsCompileActive = false;
}

#undef LOCTEXT_NAMESPACE
