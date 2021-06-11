// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUtilitySubsystem.h"
#include "EditorUtilityCommon.h"
#include "Interfaces/IMainFrameModule.h"
#include "Engine/Blueprint.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "LevelEditor.h"
#include "IBlutilityModule.h"
#include "EditorUtilityWidget.h"
#include "ScopedTransaction.h"
#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "IAssetRegistry.h"
#include "EditorUtilityTask.h"

#define LOCTEXT_NAMESPACE "EditorUtilitySubsystem"


UEditorUtilitySubsystem::UEditorUtilitySubsystem()
	: UEditorSubsystem()
{

}

void UEditorUtilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	RunTaskCommandObject = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("RunTask"),
		TEXT(""),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateUObject(this, &UEditorUtilitySubsystem::RunTaskCommand),
		ECVF_Default
	);

	CancelAllTasksCommandObject = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("CancelAllTasks"),
		TEXT(""),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateUObject(this, &UEditorUtilitySubsystem::CancelAllTasksCommand),
		ECVF_Default
	);

	IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	if (MainFrameModule.IsWindowInitialized())
	{
		HandleStartup();
	}
	else
	{
		MainFrameModule.OnMainFrameCreationFinished().AddUObject(this, &UEditorUtilitySubsystem::MainFrameCreationFinished);
	}

	TickerHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UEditorUtilitySubsystem::Tick), 0);
}

void UEditorUtilitySubsystem::Deinitialize()
{
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule::Get().OnMainFrameCreationFinished().RemoveAll(this);
	}

	FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	IConsoleManager::Get().UnregisterConsoleObject(RunTaskCommandObject);
}

void UEditorUtilitySubsystem::MainFrameCreationFinished(TSharedPtr<SWindow> InRootWindow, bool bIsNewProjectWindow)
{
	HandleStartup();
}

void UEditorUtilitySubsystem::HandleStartup()
{
	for (const FSoftObjectPath& ObjectPath : StartupObjects)
	{
		UObject* Object = ObjectPath.TryLoad();
		if (!Object || Object->IsPendingKillOrUnreachable())
		{
			UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Could not load: %s"), *ObjectPath.ToString());
			continue;
		}
		
		TryRun(ObjectPath.TryLoad());
	}
}

bool UEditorUtilitySubsystem::TryRun(UObject* Asset)
{
	if (!Asset || Asset->IsPendingKillOrUnreachable())
	{
		UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Could not run: %s"), Asset ? *Asset->GetPathName() : TEXT("None"));
		return false;
	}

	UClass* ObjectClass = Asset->GetClass();
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		ObjectClass = Blueprint->GeneratedClass;
	}

	if (!ObjectClass)
	{
		UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Missing class: %s"), *Asset->GetPathName());
		return false;
	}

	if (ObjectClass->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Could not run because functions on actors can only be called when spawned in a world: %s"), *Asset->GetPathName());
		return false;
	}

	static const FName RunFunctionName("Run");
	UFunction* RunFunction = ObjectClass->FindFunctionByName(RunFunctionName);
	if (RunFunction)
	{
		UObject* Instance = NewObject<UObject>(this, ObjectClass);
		ObjectInstances.Add(Asset, Instance);

		FEditorScriptExecutionGuard ScriptGuard;
		Instance->ProcessEvent(RunFunction, nullptr);
		return true;
	}
	else
	{
		UE_LOG(LogEditorUtilityBlueprint, Warning, TEXT("Missing function named 'Run': %s"), *Asset->GetPathName());
	}

	return false;
}

bool UEditorUtilitySubsystem::CanRun(UObject* Asset) const
{
	UClass* ObjectClass = Asset->GetClass();
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		ObjectClass = Blueprint->GeneratedClass;
	}

	if (ObjectClass)
	{
		if (ObjectClass->IsChildOf(AActor::StaticClass()))
		{
			return false;
		}

		return true;
	}

	return false;
}

void UEditorUtilitySubsystem::ReleaseInstanceOfAsset(UObject* Asset)
{
	ObjectInstances.Remove(Asset);
}

UEditorUtilityWidget* UEditorUtilitySubsystem::SpawnAndRegisterTabAndGetID(UEditorUtilityWidgetBlueprint* InBlueprint, FName& NewTabID)
{
	RegisterTabAndGetID(InBlueprint, NewTabID);
	SpawnRegisteredTabByID(NewTabID);
	return FindUtilityWidgetFromBlueprint(InBlueprint);
}

UEditorUtilityWidget* UEditorUtilitySubsystem::SpawnAndRegisterTab(class UEditorUtilityWidgetBlueprint* InBlueprint)
{
	FName InTabID;
	return SpawnAndRegisterTabAndGetID(InBlueprint, InTabID);
}

void UEditorUtilitySubsystem::RegisterTabAndGetID(class UEditorUtilityWidgetBlueprint* InBlueprint, FName& NewTabID)
{
	if (InBlueprint && !IsRunningCommandlet())
	{
		FName RegistrationName = FName(*(InBlueprint->GetPathName() + LOCTEXT("ActiveTabSuffix", "_ActiveTab").ToString()));
		FText DisplayName = FText::FromString(InBlueprint->GetName());
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if (!LevelEditorTabManager->HasTabSpawner(RegistrationName))
		{
			IBlutilityModule* BlutilityModule = FModuleManager::GetModulePtr<IBlutilityModule>("Blutility");
			LevelEditorTabManager->RegisterTabSpawner(RegistrationName, FOnSpawnTab::CreateUObject(InBlueprint, &UEditorUtilityWidgetBlueprint::SpawnEditorUITab))
				.SetDisplayName(DisplayName)
				.SetGroup(BlutilityModule->GetMenuGroup().ToSharedRef());
			InBlueprint->SetRegistrationName(RegistrationName);
		}
		RegisteredTabs.Add(RegistrationName, InBlueprint);
		NewTabID = RegistrationName;
	}
}

bool UEditorUtilitySubsystem::SpawnRegisteredTabByID(FName NewTabID)
{
	if (!IsRunningCommandlet())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if (LevelEditorTabManager->HasTabSpawner(NewTabID))
		{
			TSharedPtr<SDockTab> NewDockTab = LevelEditorTabManager->TryInvokeTab(NewTabID);
			IBlutilityModule* BlutilityModule = FModuleManager::GetModulePtr<IBlutilityModule>("Blutility");
			UEditorUtilityWidgetBlueprint* WidgetToSpawn = *RegisteredTabs.Find(NewTabID);
			check(WidgetToSpawn);
			BlutilityModule->AddLoadedScriptUI(WidgetToSpawn);
			return true;
		}
	}
	return false;
}

bool UEditorUtilitySubsystem::DoesTabExist(FName NewTabID)
{
	if (!IsRunningCommandlet())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		TSharedPtr<SDockTab> FoundTab = LevelEditorTabManager->FindExistingLiveTab(NewTabID);
		if (FoundTab)
		{
			return true;
		}
	}

	return false;
}

bool UEditorUtilitySubsystem::CloseTabByID(FName NewTabID)
{
	if (!IsRunningCommandlet())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		TSharedPtr<SDockTab> FoundTab = LevelEditorTabManager->FindExistingLiveTab(NewTabID);
		if (FoundTab)
		{
			FoundTab->RequestCloseTab();
			return true;
		}
	}

	return false;
}

UEditorUtilityWidget* UEditorUtilitySubsystem::FindUtilityWidgetFromBlueprint(class UEditorUtilityWidgetBlueprint* InBlueprint)
{
	return InBlueprint->GetCreatedWidget();
}

bool UEditorUtilitySubsystem::Tick(float DeltaTime)
{
	// Will run until we have a task that doesn't immediately complete upon calling StartExecutingTask().
	while (ActiveTask == nullptr && PendingTasks.Num() > 0)
	{
		ActiveTask = PendingTasks[0];
		PendingTasks.RemoveAt(0);

		UE_LOG(LogEditorUtilityBlueprint, Log, TEXT("Running task %s"), *GetPathNameSafe(ActiveTask));

		// And start executing it
		ActiveTask->StartExecutingTask();
	}

	if (ActiveTask && ActiveTask->WasCancelRequested())
	{
		ActiveTask->FinishExecutingTask();
	}

	return true;
}

void UEditorUtilitySubsystem::RunTaskCommand(const TArray<FString>& Params, UWorld* InWorld, FOutputDevice& Ar)
{
	if (Params.Num() >= 1)
	{
		FString TaskName = Params[0];
		if (UClass* FoundClass = FindClassByName(TaskName))
		{
			TSubclassOf<UEditorUtilityTask> TaskToSpawn(FoundClass);
			if (FoundClass == nullptr)
			{
				UE_LOG(LogEditorUtilityBlueprint, Error, TEXT("Found Task: %s, but it's not a subclass of 'EditorUtilityTask'."), *FoundClass->GetName());
				return;
			}

			UE_LOG(LogEditorUtilityBlueprint, Log, TEXT("Running task %s"), *TaskToSpawn->GetPathName());

			UEditorUtilityTask* NewTask = NewObject<UEditorUtilityTask>(this, *TaskToSpawn);
			
			//TODO Attempt to map XXX=YYY to properties on the task to make the tasks parameterizable

			RegisterAndExecuteTask(NewTask);
		}
		else
		{
			UE_LOG(LogEditorUtilityBlueprint, Error, TEXT("Unable to find task named %s."), *TaskName);
		}
	}
	else
	{
		UE_LOG(LogEditorUtilityBlueprint, Error, TEXT("No task specified.  RunTask <Name of Task>"));
	}
}

void UEditorUtilitySubsystem::CancelAllTasksCommand(const TArray<FString>& Params, UWorld* InWorld, FOutputDevice& Ar)
{
	PendingTasks.Reset();

	if (ActiveTask)
	{
		ActiveTask->RequestCancel();
		ActiveTask->FinishExecutingTask();
		ActiveTask = nullptr;
	}
}

void UEditorUtilitySubsystem::RegisterAndExecuteTask(UEditorUtilityTask* NewTask)
{
	if (NewTask != nullptr)
	{
		// Make sure this task wasn't already registered somehow
		ensureAlwaysMsgf(NewTask->MyTaskManager == nullptr, TEXT("RegisterAndExecuteTask(this=%s, task=%s) - Passed in task is already registered to %s"), *GetPathName(), *NewTask->GetPathName(), *GetPathNameSafe(NewTask->MyTaskManager));
		if (NewTask->MyTaskManager != nullptr)
		{
			NewTask->MyTaskManager->RemoveTaskFromActiveList(NewTask);
		}

		// Register it
		check(!(PendingTasks.Contains(NewTask) || ActiveTask == NewTask));
		PendingTasks.Add(NewTask);
		NewTask->MyTaskManager = this;
	}
}

void UEditorUtilitySubsystem::RemoveTaskFromActiveList(UEditorUtilityTask* Task)
{
	if (Task != nullptr)
	{
		if (ensure(Task->MyTaskManager == this))
		{
			check(PendingTasks.Contains(Task) || ActiveTask == Task);
			PendingTasks.Remove(Task);

			if (ActiveTask == Task)
			{
				ActiveTask = nullptr;
			}

			Task->MyTaskManager = nullptr;

			UE_LOG(LogEditorUtilityBlueprint, Log, TEXT("Task %s completed"), *GetPathNameSafe(Task));
		}
	}
}

void UEditorUtilitySubsystem::RegisterReferencedObject(UObject* ObjectToReference)
{
	ReferencedObjects.Add(ObjectToReference);
}

void UEditorUtilitySubsystem::UnregisterReferencedObject(UObject* ObjectToReference)
{
	ReferencedObjects.Remove(ObjectToReference);
}

UClass* UEditorUtilitySubsystem::FindClassByName(const FString& RawTargetName)
{
	FString TargetName = RawTargetName;

	// Check native classes and loaded assets first before resorting to the asset registry
	bool bIsValidClassName = true;
	if (TargetName.IsEmpty() || TargetName.Contains(TEXT(" ")))
	{
		bIsValidClassName = false;
	}
	else if (!FPackageName::IsShortPackageName(TargetName))
	{
		if (TargetName.Contains(TEXT(".")))
		{
			// Convert type'path' to just path (will return the full string if it doesn't have ' in it)
			TargetName = FPackageName::ExportTextPathToObjectPath(TargetName);

			FString PackageName;
			FString ObjectName;
			TargetName.Split(TEXT("."), &PackageName, &ObjectName);

			const bool bIncludeReadOnlyRoots = true;
			FText Reason;
			if (!FPackageName::IsValidLongPackageName(PackageName, bIncludeReadOnlyRoots, &Reason))
			{
				bIsValidClassName = false;
			}
		}
		else
		{
			bIsValidClassName = false;
		}
	}

	UClass* ResultClass = nullptr;
	if (bIsValidClassName)
	{
		if (FPackageName::IsShortPackageName(TargetName))
		{
			ResultClass = FindObject<UClass>(ANY_PACKAGE, *TargetName);
		}
		else
		{
			ResultClass = FindObject<UClass>(nullptr, *TargetName);
		}
	}

	// If we still haven't found anything yet, try the asset registry for blueprints that match the requirements
	if (ResultClass == nullptr)
	{
		ResultClass = FindBlueprintClass(TargetName);
	}

	return ResultClass;
}

UClass* UEditorUtilitySubsystem::FindBlueprintClass(const FString& TargetNameRaw)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.SearchAllAssets(true);
	}

	FString TargetName = TargetNameRaw;
	TargetName.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassNames.Add(UBlueprintCore::StaticClass()->GetFName());

	// We enumerate all assets to find any blueprints who inherit from native classes directly - or
	// from other blueprints.
	UClass* FoundClass = nullptr;
	AssetRegistry.EnumerateAssets(Filter, [&FoundClass, TargetName](const FAssetData& AssetData)
	{
		if ((AssetData.AssetName.ToString() == TargetName) || (AssetData.ObjectPath.ToString() == TargetName))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset()))
			{
				FoundClass = BP->GeneratedClass;
				return false;
			}
		}

		return true;
	});

	return FoundClass;
}

#undef LOCTEXT_NAMESPACE
