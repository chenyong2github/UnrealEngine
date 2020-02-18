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

#define LOCTEXT_NAMESPACE "EditorUtilitySubsystem"


UEditorUtilitySubsystem::UEditorUtilitySubsystem() :
	UEditorSubsystem()
{

}

void UEditorUtilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	IMainFrameModule& MainFrameModule = IMainFrameModule::Get();
	if (MainFrameModule.IsWindowInitialized())
	{
		HandleStartup();
	}
	else
	{
		MainFrameModule.OnMainFrameCreationFinished().AddUObject(this, &UEditorUtilitySubsystem::MainFrameCreationFinished);
	}
}

void UEditorUtilitySubsystem::Deinitialize()
{
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule::Get().OnMainFrameCreationFinished().RemoveAll(this);
	}
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
			BlutilityModule->AddLoadedScriptUI(InBlueprint);
		}
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
			TSharedRef<SDockTab> NewDockTab = LevelEditorTabManager->InvokeTab(NewTabID);
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

#undef LOCTEXT_NAMESPACE
