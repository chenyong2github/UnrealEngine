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

UEditorUtilityWidget* UEditorUtilitySubsystem::SpawnAndRegisterTab(UEditorUtilityWidgetBlueprint* InBlueprint)
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
		TSharedRef<SDockTab> NewDockTab = LevelEditorTabManager->InvokeTab(RegistrationName);
		return InBlueprint->GetCreatedWidget();
	}

	return nullptr;
}


#undef LOCTEXT_NAMESPACE
