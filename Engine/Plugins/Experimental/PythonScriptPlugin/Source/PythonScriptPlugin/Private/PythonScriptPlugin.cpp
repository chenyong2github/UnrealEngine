// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PythonScriptPlugin.h"
#include "PythonScriptPluginSettings.h"
#include "PythonScriptRemoteExecution.h"
#include "PyGIL.h"
#include "PyCore.h"
#include "PySlate.h"
#include "PyEngine.h"
#include "PyEditor.h"
#include "PyConstant.h"
#include "PyConversion.h"
#include "PyMethodWithClosure.h"
#include "PyReferenceCollector.h"
#include "PyWrapperTypeRegistry.h"
#include "EngineAnalytics.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/PackageReload.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"
#include "Features/IModularFeatures.h"
#include "ProfilingDebugging/ScopedTimers.h"

#if WITH_EDITOR
#include "EditorSupportDelegates.h"
#include "DesktopPlatformModule.h"
#include "EditorStyleSet.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#endif	// WITH_EDITOR

#if PLATFORM_WINDOWS
	#include <stdio.h>
	#include <fcntl.h>
	#include <io.h>
#endif	// PLATFORM_WINDOWS

#define LOCTEXT_NAMESPACE "PythonScriptPlugin"

#if WITH_PYTHON

static PyUtil::FPyApiBuffer NullPyArg = PyUtil::TCHARToPyApiBuffer(TEXT(""));
static PyUtil::FPyApiChar* NullPyArgPtrs[] = { NullPyArg.GetData() };

/** Util struct to set the sys.argv data for Python when executing a file with arguments */
struct FPythonScopedArgv
{
	FPythonScopedArgv(const TCHAR* InArgs)
	{
		if (InArgs && *InArgs)
		{
			FString NextToken;
			while (FParse::Token(InArgs, NextToken, false))
			{
				PyCommandLineArgs.Add(PyUtil::TCHARToPyApiBuffer(*NextToken));
			}

			PyCommandLineArgPtrs.Reserve(PyCommandLineArgs.Num());
			for (PyUtil::FPyApiBuffer& PyCommandLineArg : PyCommandLineArgs)
			{
				PyCommandLineArgPtrs.Add(PyCommandLineArg.GetData());
			}
		}
		PySys_SetArgvEx(PyCommandLineArgPtrs.Num(), PyCommandLineArgPtrs.GetData(), 0);
	}

	~FPythonScopedArgv()
	{
		PySys_SetArgvEx(1, NullPyArgPtrs, 0);
	}

	TArray<PyUtil::FPyApiBuffer> PyCommandLineArgs;
	TArray<PyUtil::FPyApiChar*> PyCommandLineArgPtrs;
};

FPythonCommandExecutor::FPythonCommandExecutor(IPythonScriptPlugin* InPythonScriptPlugin)
	: PythonScriptPlugin(InPythonScriptPlugin)
{
}

FName FPythonCommandExecutor::StaticName()
{
	static const FName CmdExecName = TEXT("Python");
	return CmdExecName;
}

FName FPythonCommandExecutor::GetName() const
{
	return StaticName();
}

FText FPythonCommandExecutor::GetDisplayName() const
{
	return LOCTEXT("PythonCommandExecutorDisplayName", "Python");
}

FText FPythonCommandExecutor::GetDescription() const
{
	return LOCTEXT("PythonCommandExecutorDescription", "Execute Python scripts (including files)");
}

FText FPythonCommandExecutor::GetHintText() const
{
	return LOCTEXT("PythonCommandExecutorHintText", "Enter Python script or a filename to execute");
}

void FPythonCommandExecutor::GetAutoCompleteSuggestions(const TCHAR* Input, TArray<FString>& Out)
{
}

void FPythonCommandExecutor::GetExecHistory(TArray<FString>& Out)
{
	IConsoleManager::Get().GetConsoleHistory(TEXT("Python"), Out);
}

bool FPythonCommandExecutor::Exec(const TCHAR* Input)
{
	IConsoleManager::Get().AddConsoleHistoryEntry(TEXT("Python"), Input);

	UE_LOG(LogPython, Log, TEXT("%s"), Input);

	PythonScriptPlugin->ExecPythonCommand(Input);

	return true;
}

bool FPythonCommandExecutor::AllowHotKeyClose() const
{
	return false;
}

bool FPythonCommandExecutor::AllowMultiLine() const
{
	return true;
}

FPythonREPLCommandExecutor::FPythonREPLCommandExecutor(IPythonScriptPlugin* InPythonScriptPlugin)
	: PythonScriptPlugin(InPythonScriptPlugin)
{
}

FName FPythonREPLCommandExecutor::StaticName()
{
	static const FName CmdExecName = TEXT("PythonREPL");
	return CmdExecName;
}

FName FPythonREPLCommandExecutor::GetName() const
{
	return StaticName();
}

FText FPythonREPLCommandExecutor::GetDisplayName() const
{
	return LOCTEXT("PythonREPLCommandExecutorDisplayName", "Python (REPL)");
}

FText FPythonREPLCommandExecutor::GetDescription() const
{
	return LOCTEXT("PythonREPLCommandExecutorDescription", "Execute a single Python statement and show its result");
}

FText FPythonREPLCommandExecutor::GetHintText() const
{
	return LOCTEXT("PythonREPLCommandExecutorHintText", "Enter a single Python statement");
}

void FPythonREPLCommandExecutor::GetAutoCompleteSuggestions(const TCHAR* Input, TArray<FString>& Out)
{
}

void FPythonREPLCommandExecutor::GetExecHistory(TArray<FString>& Out)
{
	IConsoleManager::Get().GetConsoleHistory(TEXT("PythonREPL"), Out);
}

bool FPythonREPLCommandExecutor::Exec(const TCHAR* Input)
{
	IConsoleManager::Get().AddConsoleHistoryEntry(TEXT("PythonREPL"), Input);

	UE_LOG(LogPython, Log, TEXT("%s"), Input);

	FPythonCommandEx PythonCommand;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
	PythonCommand.Command = Input;
	PythonScriptPlugin->ExecPythonCommandEx(PythonCommand);

	return true;
}

bool FPythonREPLCommandExecutor::AllowHotKeyClose() const
{
	return false;
}

bool FPythonREPLCommandExecutor::AllowMultiLine() const
{
	return true;
}

#if WITH_EDITOR
class FPythonCommandMenuImpl : public IPythonCommandMenu
{
public:
	FPythonCommandMenuImpl()
		: bRecentsFilesDirty(false)
	{
	}

	virtual void OnStartupMenu() override
	{
		LoadConfig();

		RegisterMenus();
	}

	virtual void OnShutdownMenu() override
	{
		UToolMenus::UnregisterOwner(this);

		// Write to file
		if (bRecentsFilesDirty)
		{
			SaveConfig();
			bRecentsFilesDirty = false;
		}
	}

	virtual void OnRunFile(const FString& InFile, bool bAdd) override
	{
		if (bAdd)
		{
			int32 Index = RecentsFiles.Find(InFile);
			if (Index != INDEX_NONE)
			{
				// If already in the list but not at the last position
				if (Index != RecentsFiles.Num() - 1)
				{
					RecentsFiles.RemoveAt(Index);
					RecentsFiles.Add(InFile);
					bRecentsFilesDirty = true;
				}
			}
			else
			{
				if (RecentsFiles.Num() >= MaxNumberOfFiles)
				{
					RecentsFiles.RemoveAt(0);
				}
				RecentsFiles.Add(InFile);
				bRecentsFilesDirty = true;
			}
		}
		else
		{
			if (RecentsFiles.RemoveSingle(InFile) > 0)
			{
				bRecentsFilesDirty = true;
			}
		}
	}

private:
	const TCHAR* STR_ConfigSection = TEXT("Python");
	const TCHAR* STR_ConfigDirectoryKey = TEXT("LastDirectory");
	const FName NAME_ConfigRecentsFilesyKey = TEXT("RecentsFiles");
	static const int32 MaxNumberOfFiles = 10;

	void LoadConfig()
	{
		RecentsFiles.Reset();

		GConfig->GetString(STR_ConfigSection, STR_ConfigDirectoryKey, LastDirectory, GEditorPerProjectIni);

		FConfigSection* Sec = GConfig->GetSectionPrivate(STR_ConfigSection, false, true, GEditorPerProjectIni);
		if (Sec)
		{
			TArray<FConfigValue> List;
			Sec->MultiFind(NAME_ConfigRecentsFilesyKey, List);

			int32 ListNum = FMath::Min(List.Num(), MaxNumberOfFiles);

			RecentsFiles.Reserve(ListNum);
			for (int32 Index = 0; Index < ListNum; ++Index)
			{
				RecentsFiles.Add(List[Index].GetValue());
			}
		}
	}

	void SaveConfig() const
	{
		GConfig->SetString(STR_ConfigSection, STR_ConfigDirectoryKey, *LastDirectory, GEditorPerProjectIni);

		FConfigSection* Sec = GConfig->GetSectionPrivate(STR_ConfigSection, true, false, GEditorPerProjectIni);
		if (Sec)
		{
			Sec->Remove(NAME_ConfigRecentsFilesyKey);
			for (int32 Index = RecentsFiles.Num() - 1; Index >= 0; --Index)
			{
				Sec->Add(NAME_ConfigRecentsFilesyKey, *RecentsFiles[Index]);
			}
		}

		GConfig->Flush(false);
	}

	void MakeRecentPythonScriptMenu(UToolMenu* InMenu)
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		FToolMenuSection& Section = InMenu->AddSection("Files");
		for (int32 Index = RecentsFiles.Num() - 1; Index >= 0; --Index)
		{
			Section.AddMenuEntry(
				NAME_None,
				FText::FromString(RecentsFiles[Index]),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FPythonCommandMenuImpl::Menu_ExecutePythonRecent, Index))
			);
		}

		FToolMenuSection& ClearSection = InMenu->AddSection("Clear");
		Section.AddMenuEntry(
			"ClearRecentPython",
			LOCTEXT("ClearRecentPython", "Clear Recent Python Scripts"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FPythonCommandMenuImpl::Menu_ClearRecentPython))
		);
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.File");
		FToolMenuSection& Section = Menu->AddSection("Python", LOCTEXT("Python", "Python"), FToolMenuInsert("FileLoadAndSave", EToolMenuInsertType::After));
		Section.AddMenuEntry(
			"OpenPython",
			LOCTEXT("OpenPython", "Execute Python Script..."),
			LOCTEXT("OpenPythonTooltip", "Open a Python Script file and Execute it."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FPythonCommandMenuImpl::Menu_ExecutePython))
		);
		Section.AddSubMenu(
			"RecentPythonsSubMenu",
			LOCTEXT("RecentPythonsSubMenu", "Recent Python Scripts"),
			LOCTEXT("RecentPythonsSubMenu_ToolTip", "Select a recent Python Script file and Execute it."),
			FNewToolMenuDelegate::CreateRaw(this, &FPythonCommandMenuImpl::MakeRecentPythonScriptMenu),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.RecentLevels")
		);
	}

	void Menu_ExecutePythonRecent(int32 Index)
	{
		if (RecentsFiles.IsValidIndex(Index))
		{
			FString PyCopied = RecentsFiles[Index];
			GEngine->Exec(NULL, *FString::Printf(TEXT("py \"%s\""), *PyCopied));
		}
	}

	void Menu_ClearRecentPython()
	{
		if (RecentsFiles.Num() > 0)
		{
			RecentsFiles.Reset();
			bRecentsFilesDirty = true;
		}
	}

	void Menu_ExecutePython()
	{
		TArray<FString> OpenedFiles;
		FString DefaultDirectory = LastDirectory;

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			bool bOpened = DesktopPlatform->OpenFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				LOCTEXT("ExecutePython", "Execute Python Script").ToString(),
				DefaultDirectory,
				TEXT(""),
				TEXT("Python files|*.py|"),
				EFileDialogFlags::None,
				OpenedFiles
			);

			if (bOpened && OpenedFiles.Num() > 0)
			{
				if (DefaultDirectory != LastDirectory)
				{
					LastDirectory = DefaultDirectory;
					bRecentsFilesDirty = true;
				}

				GEngine->Exec(NULL, *FString::Printf(TEXT("py \"%s\""), *OpenedFiles.Last()));
			}
		}
	}

private:

	TArray<FString> RecentsFiles;
	FString LastDirectory;

	bool bRecentsFilesDirty;
};
#endif // WITH_EDITOR

#endif	// WITH_PYTHON

FPythonScriptPlugin::FPythonScriptPlugin()
#if WITH_PYTHON
	: CmdExec(this)
	, CmdREPLExec(this)
	, CmdMenu(nullptr)
	, bInitialized(false)
	, bHasTicked(false)
#endif	// WITH_PYTHON
{
}

bool FPythonScriptPlugin::IsPythonAvailable() const
{
	return WITH_PYTHON;
}

bool FPythonScriptPlugin::ExecPythonCommand(const TCHAR* InPythonCommand)
{
	FPythonCommandEx PythonCommand;
	PythonCommand.Command = InPythonCommand;
	return ExecPythonCommandEx(PythonCommand);
}

bool FPythonScriptPlugin::ExecPythonCommandEx(FPythonCommandEx& InOutPythonCommand)
{
#if WITH_PYTHON
	if (InOutPythonCommand.ExecutionMode == EPythonCommandExecutionMode::ExecuteFile)
	{
		// We may have been passed literal code or a file
		// To work out which, extract the first token and see if it's a .py file
		// If it is, treat the remaining text as arguments to the file
		// Otherwise, treat it as literal code
		FString ExtractedFilename;
		{
			const TCHAR* Tmp = *InOutPythonCommand.Command;
			ExtractedFilename = FParse::Token(Tmp, false);
		}
		if (FPaths::GetExtension(ExtractedFilename) == TEXT("py"))
		{
			return RunFile(*ExtractedFilename, *InOutPythonCommand.Command, InOutPythonCommand);
		}
		else
		{
			return RunString(InOutPythonCommand);
		}
	}
	else
	{
		return RunString(InOutPythonCommand);
	}
#else	// WITH_PYTHON
	InOutPythonCommand.CommandResult = TEXT("Python is not available!");
	ensureAlwaysMsgf(false, TEXT("%s"), *InOutPythonCommand.CommandResult);
	return false;
#endif	// WITH_PYTHON
}

FSimpleMulticastDelegate& FPythonScriptPlugin::OnPythonInitialized()
{
	return OnPythonInitializedDelegate;
}

FSimpleMulticastDelegate& FPythonScriptPlugin::OnPythonShutdown()
{
	return OnPythonShutdownDelegate;
}

void FPythonScriptPlugin::StartupModule()
{
#if WITH_PYTHON
	InitializePython();
	IModularFeatures::Get().RegisterModularFeature(IConsoleCommandExecutor::ModularFeatureName(), &CmdExec);
	IModularFeatures::Get().RegisterModularFeature(IConsoleCommandExecutor::ModularFeatureName(), &CmdREPLExec);

	check(!RemoteExecution);
	RemoteExecution = MakeUnique<FPythonScriptRemoteExecution>(this);

#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPythonScriptPlugin::OnPostEngineInit);
#endif // WITH_EDITOR

	FCoreDelegates::OnPreExit.AddRaw(this, &FPythonScriptPlugin::ShutdownPython);
#endif	// WITH_PYTHON
}

#if WITH_PYTHON
#if WITH_EDITOR
void FPythonScriptPlugin::OnPostEngineInit()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		check(CmdMenu == nullptr);
		CmdMenu = new FPythonCommandMenuImpl();
		CmdMenu->OnStartupMenu();

		UToolMenus::Get()->RegisterStringCommandHandler("Python", FToolMenuExecuteString::CreateLambda([this](const FString& InString, const FToolMenuContext& InContext) {
			ExecPythonCommand(*InString);
		}));
	}
}

#endif // WITH_EDITOR
#endif // WITH_PYTHON

void FPythonScriptPlugin::ShutdownModule()
{
#if WITH_PYTHON
	FCoreDelegates::OnPreExit.RemoveAll(this);

	RemoteExecution.Reset();

#if WITH_EDITOR
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	if (CmdMenu)
	{
		CmdMenu->OnShutdownMenu();
		delete CmdMenu;
		CmdMenu = nullptr;
	}

	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterStringCommandHandler("Python");
	}
#endif // WITH_EDITOR

	IModularFeatures::Get().UnregisterModularFeature(IConsoleCommandExecutor::ModularFeatureName(), &CmdExec);
	IModularFeatures::Get().UnregisterModularFeature(IConsoleCommandExecutor::ModularFeatureName(), &CmdREPLExec);
	ShutdownPython();
#endif	// WITH_PYTHON
}

bool FPythonScriptPlugin::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
#if WITH_PYTHON
	if (FParse::Command(&Cmd, TEXT("PY")))
	{
		ExecPythonCommand(Cmd);
		return true;
	}
#endif	// WITH_PYTHON
	return false;
}

void FPythonScriptPlugin::PreChange(const UUserDefinedEnum* Enum, FEnumEditorUtils::EEnumEditorChangeInfo Info)
{
}

void FPythonScriptPlugin::PostChange(const UUserDefinedEnum* Enum, FEnumEditorUtils::EEnumEditorChangeInfo Info)
{
	OnAssetUpdated(Enum);
}

#if WITH_PYTHON

void FPythonScriptPlugin::InitializePython()
{
	bInitialized = true;

	// Set-up the correct program name
	{
		FString ProgramName = FPlatformProcess::GetCurrentWorkingDirectory() / FPlatformProcess::ExecutableName(false);
		FPaths::NormalizeFilename(ProgramName);
		PyProgramName = PyUtil::TCHARToPyApiBuffer(*ProgramName);
	}

	// Set-up the correct home path
	{
		// Build the full Python directory (UE_PYTHON_DIR may be relative to UE4 engine directory for portability)
		FString PythonDir = UTF8_TO_TCHAR(UE_PYTHON_DIR);
		PythonDir.ReplaceInline(TEXT("{ENGINE_DIR}"), *FPaths::EngineDir(), ESearchCase::CaseSensitive);
		FPaths::NormalizeDirectoryName(PythonDir);
		FPaths::RemoveDuplicateSlashes(PythonDir);
		PyHomePath = PyUtil::TCHARToPyApiBuffer(*PythonDir);
	}

	// Initialize the Python interpreter
	{
		UE_LOG(LogPython, Log, TEXT("Using Python %d.%d.%d"), PY_MAJOR_VERSION, PY_MINOR_VERSION, PY_MICRO_VERSION);

		// Python 3 changes the console mode from O_TEXT to O_BINARY which affects other UE4 uses of the console
		// So change the console mode back to its current setting after Py_Initialize has been called
#if PLATFORM_WINDOWS && PY_MAJOR_VERSION >= 3
		// We call _setmode here to cache the current state
		CA_SUPPRESS(6031)
		fflush(stdin);
		const int StdInMode  = _setmode(_fileno(stdin), _O_TEXT);
		CA_SUPPRESS(6031)
		fflush(stdout);
		const int StdOutMode = _setmode(_fileno(stdout), _O_TEXT);
		CA_SUPPRESS(6031)
		fflush(stderr);
		const int StdErrMode = _setmode(_fileno(stderr), _O_TEXT);
#endif	// PLATFORM_WINDOWS && PY_MAJOR_VERSION >= 3

#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 4
		Py_SetStandardStreamEncoding("utf-8", nullptr);
#endif	// PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 4
		Py_SetProgramName(PyProgramName.GetData());
		Py_SetPythonHome(PyHomePath.GetData());
		Py_InitializeEx(0); // 0 so Python doesn't override any UE4 signal handling

#if PLATFORM_WINDOWS && PY_MAJOR_VERSION >= 3
		// We call _setmode here to restore the previous state
		if (StdInMode != -1)
		{
			CA_SUPPRESS(6031)
			fflush(stdin);
			CA_SUPPRESS(6031)
			_setmode(_fileno(stdin), StdInMode);
		}
		if (StdOutMode != -1)
		{
			CA_SUPPRESS(6031)
			fflush(stdout);
			CA_SUPPRESS(6031)
			_setmode(_fileno(stdout), StdOutMode);
		}
		if (StdErrMode != -1)
		{
			CA_SUPPRESS(6031)
			fflush(stderr);
			CA_SUPPRESS(6031)
			_setmode(_fileno(stderr), StdErrMode);
		}
#endif	// PLATFORM_WINDOWS && PY_MAJOR_VERSION >= 3

		PySys_SetArgvEx(1, NullPyArgPtrs, 0);

		// Enable developer warnings if requested
		if (GetDefault<UPythonScriptPluginSettings>()->bDeveloperMode)
		{
			PyUtil::EnableDeveloperWarnings();
		}

		// Initialize our custom method type as we'll need it when generating bindings
		InitializePyMethodWithClosure();

		// Initialize our custom constant type as we'll need it when generating bindings
		InitializePyConstant();

		PyObject* PyMainModule = PyImport_AddModule("__main__");
		PyDefaultGlobalDict = FPyObjectPtr::NewReference(PyModule_GetDict(PyMainModule));
		PyDefaultLocalDict = PyDefaultGlobalDict;

		PyConsoleGlobalDict = FPyObjectPtr::StealReference(PyDict_Copy(PyDefaultGlobalDict));
		PyConsoleLocalDict = PyConsoleGlobalDict;

#if WITH_EDITOR
		FEditorSupportDelegates::PrepareToCleanseEditorObject.AddRaw(this, &FPythonScriptPlugin::OnPrepareToCleanseEditorObject);
#endif	// WITH_EDITOR
	}

	// Set-up the known Python script paths
	{
		PyUtil::AddSystemPath(FPaths::ConvertRelativePathToFull(FPlatformProcess::UserDir() / FApp::GetEpicProductIdentifier() / TEXT("Python")));

		TArray<FString> RootPaths;
		FPackageName::QueryRootContentPaths(RootPaths);
		for (const FString& RootPath : RootPaths)
		{
			const FString RootFilesystemPath = FPackageName::LongPackageNameToFilename(RootPath);
			PyUtil::AddSystemPath(FPaths::ConvertRelativePathToFull(RootFilesystemPath / TEXT("Python")));
		}

		for (const FDirectoryPath& AdditionalPath : GetDefault<UPythonScriptPluginSettings>()->AdditionalPaths)
		{
			PyUtil::AddSystemPath(FPaths::ConvertRelativePathToFull(AdditionalPath.Path));
		}

		FPackageName::OnContentPathMounted().AddRaw(this, &FPythonScriptPlugin::OnContentPathMounted);
		FPackageName::OnContentPathDismounted().AddRaw(this, &FPythonScriptPlugin::OnContentPathDismounted);
		FCoreUObjectDelegates::OnPackageReloaded.AddRaw(this, &FPythonScriptPlugin::OnAssetReload);

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().OnAssetRenamed().AddRaw(this, &FPythonScriptPlugin::OnAssetRenamed);
		AssetRegistryModule.Get().OnAssetRemoved().AddRaw(this, &FPythonScriptPlugin::OnAssetRemoved);
	}

	// Initialize the Unreal Python module
	{
		// Create the top-level "unreal" module
		PyUnrealModule = FPyObjectPtr::NewReference(PyImport_AddModule("unreal"));
		
		// Import "unreal" into the console by default
		PyDict_SetItemString(PyConsoleGlobalDict, "unreal", PyUnrealModule);

		// Initialize the and import the "core" module
		PyCore::InitializeModule();
		ImportUnrealModule(TEXT("core"));

		// Initialize the and import the "slate" module
		PySlate::InitializeModule();
		ImportUnrealModule(TEXT("slate"));

		// Initialize the and import the "engine" module
		PyEngine::InitializeModule();
		ImportUnrealModule(TEXT("engine"));

#if WITH_EDITOR
		// Initialize the and import the "editor" module
		PyEditor::InitializeModule();
		ImportUnrealModule(TEXT("editor"));
#endif	// WITH_EDITOR

		FPyWrapperTypeRegistry::Get().OnModuleDirtied().AddRaw(this, &FPythonScriptPlugin::OnModuleDirtied);
		FModuleManager::Get().OnModulesChanged().AddRaw(this, &FPythonScriptPlugin::OnModulesChanged);

		// Initialize the wrapped types
		FPyWrapperTypeRegistry::Get().GenerateWrappedTypes();

		// Initialize the tick handler
		TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
		{
			Tick(DeltaTime);
			return true;
		}));
	}

	// Notify any external listeners
	OnPythonInitializedDelegate.Broadcast();
}

void FPythonScriptPlugin::ShutdownPython()
{
	if (!bInitialized)
	{
		return;
	}

	// Notify any external listeners
	OnPythonShutdownDelegate.Broadcast();

	FTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (ModuleDelayedHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(ModuleDelayedHandle);
	}

	FPyWrapperTypeRegistry::Get().OnModuleDirtied().RemoveAll(this);
	FModuleManager::Get().OnModulesChanged().RemoveAll(this);

	FPackageName::OnContentPathMounted().RemoveAll(this);
	FPackageName::OnContentPathDismounted().RemoveAll(this);
	FCoreUObjectDelegates::OnPackageReloaded.RemoveAll(this);

	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		AssetRegistryModule->Get().OnAssetRenamed().RemoveAll(this);
		AssetRegistryModule->Get().OnAssetRemoved().RemoveAll(this);
	}

#if WITH_EDITOR
	FEditorSupportDelegates::PrepareToCleanseEditorObject.RemoveAll(this);
#endif	// WITH_EDITOR

	FPyReferenceCollector::Get().PurgeUnrealGeneratedTypes();

#if WITH_EDITOR
	PyEditor::ShutdownModule();
#endif	// WITH_EDITOR
	PyEngine::ShutdownModule();
	PySlate::ShutdownModule();
	PyCore::ShutdownModule();

	PyUnrealModule.Reset();
	PyDefaultGlobalDict.Reset();
	PyDefaultLocalDict.Reset();
	PyConsoleGlobalDict.Reset();
	PyConsoleLocalDict.Reset();

	ShutdownPyMethodWithClosure();

	Py_Finalize();

	bInitialized = false;
	bHasTicked = false;
}

void FPythonScriptPlugin::RequestStubCodeGeneration()
{
	// Ignore requests made before the fist Tick
	if (!bHasTicked)
	{
		return;
	}

	// Delay 2 seconds before generating as this may be triggered by loading several modules at once
	static const float Delay = 2.0f;

	// If there is an existing pending notification, remove it so that it can be reset
	if (ModuleDelayedHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(ModuleDelayedHandle);
		ModuleDelayedHandle.Reset();
	}

	// Set new tick
	ModuleDelayedHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this](float DeltaTime)
		{
			// Once ticked, the delegate will be removed so reset the handle to indicate that it isn't set.
			ModuleDelayedHandle.Reset();

			// Call the event now that the delay has passed.
			GenerateStubCode();

			// Don't reschedule to run again.
			return false;
		}),
		Delay);
}

void FPythonScriptPlugin::GenerateStubCode()
{
	if (GetDefault<UPythonScriptPluginSettings>()->bDeveloperMode)
	{
		// Generate stub code if developer mode enabled
		FPyWrapperTypeRegistry::Get().GenerateStubCodeForWrappedTypes();
	}
}

void FPythonScriptPlugin::Tick(const float InDeltaTime)
{
	// If this is our first Tick, handle any post-init logic that should happen once the engine is fully initialized
	if (!bHasTicked)
	{
		bHasTicked = true;

		// Run start-up scripts now
		TArray<FString> PySysPaths;
		{
			FPyScopedGIL GIL;
			PySysPaths = PyUtil::GetSystemPaths();
		}
		for (const FString& PySysPath : PySysPaths)
		{
			const FString PotentialFilePath = PySysPath / TEXT("init_unreal.py");
			if (FPaths::FileExists(PotentialFilePath))
			{
				// Execute these files in the "public" scope, as if their contents had been run directly in the console
				// This allows them to be used to set-up an editor environment for the console
				FPythonCommandEx InitUnrealPythonCommand;
				InitUnrealPythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;
				RunFile(*PotentialFilePath, *InitUnrealPythonCommand.Command, InitUnrealPythonCommand);
			}
		}
		for (const FString& StartupScript : GetDefault<UPythonScriptPluginSettings>()->StartupScripts)
		{
			ExecPythonCommand(*StartupScript);
		}

#if WITH_EDITOR
		// Register to generate stub code after a short delay
		RequestStubCodeGeneration();
#endif	// WITH_EDITOR
	}

	RemoteExecution->Tick(InDeltaTime);

	FPyWrapperTypeReinstancer::Get().ProcessPending();
}

void FPythonScriptPlugin::SyncRemoteExecutionToSettings()
{
	RemoteExecution->SyncToSettings();
}

void FPythonScriptPlugin::ImportUnrealModule(const TCHAR* InModuleName)
{
	const FString PythonModuleName = FString::Printf(TEXT("unreal_%s"), InModuleName);
	const FString NativeModuleName = FString::Printf(TEXT("_unreal_%s"), InModuleName);

	FPyScopedGIL GIL;

	const TCHAR* ModuleNameToImport = nullptr;
	PyObject* ModuleToReload = nullptr;
	if (PyUtil::IsModuleAvailableForImport(*PythonModuleName))
	{
		// Python modules that are already loaded should be reloaded if we're requested to import them again
		if (!PyUtil::IsModuleImported(*PythonModuleName, &ModuleToReload))
		{
			ModuleNameToImport = *PythonModuleName;
		}
	}
	else if (PyUtil::IsModuleAvailableForImport(*NativeModuleName))
	{
		ModuleNameToImport = *NativeModuleName;
	}

	FPyObjectPtr PyModule;
	if (ModuleToReload)
	{
		PyModule = FPyObjectPtr::StealReference(PyImport_ReloadModule(ModuleToReload));
	}
	else if (ModuleNameToImport)
	{
		PyModule = FPyObjectPtr::StealReference(PyImport_ImportModule(TCHAR_TO_UTF8(ModuleNameToImport)));
	}

	if (PyModule)
	{
		check(PyUnrealModule);
		PyObject* PyUnrealModuleDict = PyModule_GetDict(PyUnrealModule);

		// Hoist every public symbol from this module into the top-level "unreal" module
		{
			PyObject* PyModuleDict = PyModule_GetDict(PyModule);

			PyObject* PyObjKey = nullptr;
			PyObject* PyObjValue = nullptr;
			Py_ssize_t ModuleDictIndex = 0;
			while (PyDict_Next(PyModuleDict, &ModuleDictIndex, &PyObjKey, &PyObjValue))
			{
				if (PyObjKey)
				{
					const FString Key = PyUtil::PyObjectToUEString(PyObjKey);
					if (Key.Len() > 0 && Key[0] != TEXT('_'))
					{
						PyDict_SetItem(PyUnrealModuleDict, PyObjKey, PyObjValue);
					}
				}
			}
		}
	}
	else
	{
		PyUtil::LogPythonError(/*bInteractive*/true);
	}
}

PyObject* FPythonScriptPlugin::EvalString(const TCHAR* InStr, const TCHAR* InContext, const int InMode)
{
	return EvalString(InStr, InContext, InMode, PyConsoleGlobalDict, PyConsoleLocalDict);
}

PyObject* FPythonScriptPlugin::EvalString(const TCHAR* InStr, const TCHAR* InContext, const int InMode, PyObject* InGlobalDict, PyObject* InLocalDict)
{
	PyCompilerFlags *PyCompFlags = nullptr;

	PyArena* PyArena = PyArena_New();
	if (!PyArena)
	{
		return nullptr;
	}

	_mod* PyModule = PyParser_ASTFromString(TCHAR_TO_UTF8(InStr), TCHAR_TO_UTF8(InContext), InMode, PyCompFlags, PyArena);
	if (!PyModule)
	{
		PyArena_Free(PyArena);
		return nullptr;
	}

	typedef TPyPtr<PyCodeObject> PyCodeObjectPtr;
	PyCodeObjectPtr PyCodeObj = PyCodeObjectPtr::StealReference(PyAST_Compile(PyModule, TCHAR_TO_UTF8(InContext), PyCompFlags, PyArena));
	if (!PyCodeObj)
	{
		return nullptr;
	}

	return PyEval_EvalCode((PyUtil::FPyCodeObjectType*)PyCodeObj.Get(), InGlobalDict, InLocalDict);
}

bool FPythonScriptPlugin::RunString(FPythonCommandEx& InOutPythonCommand)
{
	// Execute Python code within this block
	{
		FPyScopedGIL GIL;
		TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, GIsRunningUnattendedScript || EnumHasAnyFlags(InOutPythonCommand.Flags, EPythonCommandFlags::Unattended));

		int PyExecMode = 0;
		switch (InOutPythonCommand.ExecutionMode)
		{
		case EPythonCommandExecutionMode::ExecuteFile:
			PyExecMode = Py_file_input;
			break;
		case EPythonCommandExecutionMode::ExecuteStatement:
			PyExecMode = Py_single_input;
			break;
		case EPythonCommandExecutionMode::EvaluateStatement:
			PyExecMode = Py_eval_input;
			break;
		default:
			checkf(false, TEXT("Invalid EPythonCommandExecutionMode!"));
			break;
		}

		FDelegateHandle LogCaptureHandle = PyCore::GetPythonLogCapture().AddLambda([&InOutPythonCommand](EPythonLogOutputType InLogType, const TCHAR* InLogString) { InOutPythonCommand.LogOutput.Add(FPythonLogOutputEntry{ InLogType, InLogString }); });
		FPyObjectPtr PyResult = FPyObjectPtr::StealReference(EvalString(*InOutPythonCommand.Command, TEXT("<string>"), PyExecMode));
		PyCore::GetPythonLogCapture().Remove(LogCaptureHandle);
		
		if (PyResult)
		{
			InOutPythonCommand.CommandResult = PyUtil::PyObjectToUEStringRepr(PyResult);
		}
		else
		{
			InOutPythonCommand.CommandResult = PyUtil::LogPythonError();
			return false;
		}
	}

	FPyWrapperTypeReinstancer::Get().ProcessPending();
	return true;
}

bool FPythonScriptPlugin::RunFile(const TCHAR* InFile, const TCHAR* InArgs, FPythonCommandEx& InOutPythonCommand)
{
	auto ResolveFilePath = [InFile]() -> FString
	{
		// Favor the CWD
		if (FPaths::FileExists(InFile))
		{
			return FPaths::ConvertRelativePathToFull(InFile);
		}

		// Execute Python code within this block
		{
			FPyScopedGIL GIL;

			// Then test against each system path in order (as Python would)
			const TArray<FString> PySysPaths = PyUtil::GetSystemPaths();
			for (const FString& PySysPath : PySysPaths)
			{
				const FString PotentialFilePath = PySysPath / InFile;
				if (FPaths::FileExists(PotentialFilePath))
				{
					return PotentialFilePath;
				}
			}
		}

		// Didn't find a match... we know this file doesn't exist, but we'll use this path in the error reporting
		return FPaths::ConvertRelativePathToFull(InFile);
	};

	const FString ResolvedFilePath = ResolveFilePath();

	FString FileStr;
	bool bLoaded = FFileHelper::LoadFileToString(FileStr, *ResolvedFilePath);
#if WITH_EDITOR
	if (CmdMenu)
	{
		CmdMenu->OnRunFile(ResolvedFilePath, bLoaded);
	}
#endif // WITH_EDITOR

	if (!bLoaded)
	{
		InOutPythonCommand.CommandResult = FString::Printf(TEXT("Could not load Python file '%s' (resolved from '%s')"), *ResolvedFilePath, InFile);
		UE_LOG(LogPython, Error, TEXT("%s"), *InOutPythonCommand.CommandResult);
		return false;
	}

	// Execute Python code within this block
	double ElapsedSeconds = 0.0;
	{
		FPyScopedGIL GIL;
		TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, GIsRunningUnattendedScript || EnumHasAnyFlags(InOutPythonCommand.Flags, EPythonCommandFlags::Unattended));

		FPyObjectPtr PyFileGlobalDict = PyConsoleGlobalDict;
		FPyObjectPtr PyFileLocalDict = PyConsoleLocalDict;
		if (InOutPythonCommand.FileExecutionScope == EPythonFileExecutionScope::Private)
		{
			PyFileGlobalDict = FPyObjectPtr::StealReference(PyDict_Copy(PyDefaultGlobalDict));
			PyFileLocalDict = PyFileGlobalDict;
		}
		{
			FPyObjectPtr PyResolvedFilePath;
			if (PyConversion::Pythonize(ResolvedFilePath, PyResolvedFilePath.Get(), PyConversion::ESetErrorState::No))
			{
				PyDict_SetItemString(PyFileGlobalDict, "__file__", PyResolvedFilePath);
			}
		}

		FPyObjectPtr PyResult;
		{
			FScopedDurationTimer Timer(ElapsedSeconds);
			FPythonScopedArgv ScopedArgv(InArgs);

			FDelegateHandle LogCaptureHandle = PyCore::GetPythonLogCapture().AddLambda([&InOutPythonCommand](EPythonLogOutputType InLogType, const TCHAR* InLogString) { InOutPythonCommand.LogOutput.Add(FPythonLogOutputEntry{ InLogType, InLogString }); });
			PyResult = FPyObjectPtr::StealReference(EvalString(*FileStr, *ResolvedFilePath, Py_file_input, PyFileGlobalDict, PyFileLocalDict)); // We can't just use PyRun_File here as Python isn't always built against the same version of the CRT as UE4, so we get a crash at the CRT layer
			PyCore::GetPythonLogCapture().Remove(LogCaptureHandle);
		}

		PyDict_DelItemString(PyFileGlobalDict, "__file__");

		if (PyResult)
		{
			InOutPythonCommand.CommandResult = PyUtil::PyObjectToUEStringRepr(PyResult);
		}
		else
		{
			InOutPythonCommand.CommandResult = PyUtil::LogPythonError();
			return false;
		}
	}

	FPyWrapperTypeReinstancer::Get().ProcessPending();

	if (FEngineAnalytics::IsAvailable())
	{
		TArray<FAnalyticsEventAttribute> EventAttributes;
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Duration"), ElapsedSeconds));
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("PythonScriptPlugin"), EventAttributes);
	}

	return true;
}

void FPythonScriptPlugin::OnModuleDirtied(FName InModuleName)
{
	ImportUnrealModule(*InModuleName.ToString());
}

void FPythonScriptPlugin::OnModulesChanged(FName InModuleName, EModuleChangeReason InModuleChangeReason)
{
	switch (InModuleChangeReason)
	{
	case EModuleChangeReason::ModuleLoaded:
		FPyWrapperTypeRegistry::Get().GenerateWrappedTypesForModule(InModuleName);
#if WITH_EDITOR
		// Register to generate stub code after a short delay
		RequestStubCodeGeneration();
#endif	// WITH_EDITOR
		break;

	case EModuleChangeReason::ModuleUnloaded:
		FPyWrapperTypeRegistry::Get().OrphanWrappedTypesForModule(InModuleName);
#if WITH_EDITOR
		// Register to generate stub code after a short delay
		RequestStubCodeGeneration();
#endif	// WITH_EDITOR
		break;

	default:
		break;
	}
}

void FPythonScriptPlugin::OnContentPathMounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	FPyScopedGIL GIL;
	PyUtil::AddSystemPath(FPaths::ConvertRelativePathToFull(InFilesystemPath / TEXT("Python")));
}

void FPythonScriptPlugin::OnContentPathDismounted(const FString& InAssetPath, const FString& InFilesystemPath)
{
	FPyScopedGIL GIL;
	PyUtil::RemoveSystemPath(FPaths::ConvertRelativePathToFull(InFilesystemPath / TEXT("Python")));
}

void FPythonScriptPlugin::OnAssetRenamed(const FAssetData& Data, const FString& OldName)
{
	const FName OldPackageName = *FPackageName::ObjectPathToPackageName(OldName);
	const UObject* AssetPtr = PyGenUtil::GetTypeRegistryType(Data.GetAsset());
	if (AssetPtr)
	{
		// If this asset has an associated Python type, then we need to rename it
		FPyWrapperTypeRegistry& PyWrapperTypeRegistry = FPyWrapperTypeRegistry::Get();
		if (PyWrapperTypeRegistry.HasWrappedTypeForObjectName(OldPackageName))
		{
			PyWrapperTypeRegistry.UpdateGenerateWrappedTypeForRename(OldPackageName, AssetPtr);
			OnAssetUpdated(AssetPtr);
		}
	}
}

void FPythonScriptPlugin::OnAssetRemoved(const FAssetData& Data)
{
	const UObject* AssetPtr = PyGenUtil::GetTypeRegistryType(Data.GetAsset());
	if (AssetPtr)
	{
		// If this asset has an associated Python type, then we need to remove it
		FPyWrapperTypeRegistry& PyWrapperTypeRegistry = FPyWrapperTypeRegistry::Get();
		if (PyWrapperTypeRegistry.HasWrappedTypeForObject(AssetPtr))
		{
			PyWrapperTypeRegistry.RemoveGenerateWrappedTypeForDelete(AssetPtr);
		}
	}
}

void FPythonScriptPlugin::OnAssetReload(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent)
{
	if (InPackageReloadPhase == EPackageReloadPhase::PostPackageFixup)
	{
		// Get the primary asset in this package
		// Use the new package as it has the correct name
		const UPackage* NewPackage = InPackageReloadedEvent->GetNewPackage();
		const UObject* NewAsset = StaticFindObject(UObject::StaticClass(), (UPackage*)NewPackage, *FPackageName::GetLongPackageAssetName(NewPackage->GetName()));
		OnAssetUpdated(NewAsset);
	}
}

void FPythonScriptPlugin::OnAssetUpdated(const UObject* InObj)
{
	const UObject* AssetPtr = PyGenUtil::GetTypeRegistryType(InObj);
	if (AssetPtr)
	{
		// If this asset has an associated Python type, then we need to re-generate it
		FPyWrapperTypeRegistry& PyWrapperTypeRegistry = FPyWrapperTypeRegistry::Get();
		if (PyWrapperTypeRegistry.HasWrappedTypeForObject(AssetPtr))
		{
			FPyWrapperTypeRegistry::FGeneratedWrappedTypeReferences GeneratedWrappedTypeReferences;
			TSet<FName> DirtyModules;

			PyWrapperTypeRegistry.GenerateWrappedTypeForObject(AssetPtr, GeneratedWrappedTypeReferences, DirtyModules, EPyTypeGenerationFlags::IncludeBlueprintGeneratedTypes | EPyTypeGenerationFlags::OverwriteExisting);

			PyWrapperTypeRegistry.GenerateWrappedTypesForReferences(GeneratedWrappedTypeReferences, DirtyModules);
			PyWrapperTypeRegistry.NotifyModulesDirtied(DirtyModules);
		}
	}
}

#if WITH_EDITOR

void FPythonScriptPlugin::OnPrepareToCleanseEditorObject(UObject* InObject)
{
	FPyReferenceCollector::Get().PurgeUnrealObjectReferences(InObject, true);
}

#endif	// WITH_EDITOR

#endif	// WITH_PYTHON

IMPLEMENT_MODULE(FPythonScriptPlugin, PythonScriptPlugin)

#undef LOCTEXT_NAMESPACE
