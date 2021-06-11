// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightWeightInstancesEditor.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "GameFramework/LightWeightInstanceManager.h"
#include "GameFramework/LightWeightInstanceSubsystem.h"

#define LOCTEXT_NAMESPACE "FLightWeightInstancesEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogLWIEditor, Log, All);

typedef FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors DelegateType;

void FLightWeightInstancesEditorModule::StartupModule()
{
	// hook up level editor extension for conversion between light weight instances and actors
	AddLevelViewportMenuExtender();
}

void FLightWeightInstancesEditorModule::ShutdownModule()
{
	// Cleanup menu extenstions
	RemoveLevelViewportMenuExtender();
}

void FLightWeightInstancesEditorModule::AddLevelViewportMenuExtender()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();

	MenuExtenders.Add(DelegateType::CreateRaw(this, &FLightWeightInstancesEditorModule::CreateLevelViewportContextMenuExtender));
	LevelViewportExtenderHandle = MenuExtenders.Last().GetHandle();
}

void FLightWeightInstancesEditorModule::RemoveLevelViewportMenuExtender()
{
	if (LevelViewportExtenderHandle.IsValid())
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule)
		{
			LevelEditorModule->GetAllLevelViewportContextMenuExtenders().RemoveAll([=](const DelegateType& In) { return In.GetHandle() == LevelViewportExtenderHandle; });
		}
	}
}


TSharedRef<FExtender> FLightWeightInstancesEditorModule::CreateLevelViewportContextMenuExtender(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> InActors)
{
	TSharedRef<FExtender> Extender = MakeShareable(new FExtender);

	// We only support conversion if all of the actors are the same type
	for (AActor* Actor : InActors)
	{
		if (Actor->GetClass() != InActors[0]->GetClass())
		{
			UE_LOG(LogLWIEditor, Warning, TEXT("Unable to convert actors of multiple types to light weight instances"));
			return Extender;
		}
	}

	if (InActors.Num() > 0)
	{
		FText ActorName = InActors.Num() == 1 ? FText::Format(LOCTEXT("ActorNameSingular", "\"{0}\""), FText::FromString(InActors[0]->GetActorLabel())) : LOCTEXT("ActorNamePlural", "Actors");

		FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedRef<FUICommandList> LevelEditorCommandBindings = LevelEditor.GetGlobalLevelEditorActions();

		// We can only convert to an LWI if we didn't select an LWI
		if (!InActors[0]->GetClass()->IsChildOf(ALightWeightInstanceManager::StaticClass()))
		{
			Extender->AddMenuExtension("ActorControl", EExtensionHook::After, LevelEditorCommandBindings, FMenuExtensionDelegate::CreateLambda(
				[this, ActorName, InActors](FMenuBuilder& MenuBuilder) {

					MenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("ConvertSelectedActorsToLWIsText", "Convert {0} To Light Weight Instances"), ActorName),
						LOCTEXT("ConvertSelectedActorsToLWIsTooltip", "Convert the selected actors to light weight instances."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateRaw(this, &FLightWeightInstancesEditorModule::ConvertActorsToLWIsUIAction, InActors))
					);
				})
			);
		}
	}

	return Extender;
}

void FLightWeightInstancesEditorModule::ConvertActorsToLWIsUIAction(const TArray<AActor*> InActors) const
{
	if (InActors.IsEmpty() || InActors[0] == nullptr)
	{
		UE_LOG(LogLWIEditor, Log, TEXT("Unable to convert unspecified actors to light weight instances"));
		return;
	}
	ALightWeightInstanceManager* Manager = FLightWeightInstanceSubsystem::Get().FindOrAddLightWeightInstanceManager(InActors[0]->GetClass(), InActors[0]->GetLevel());
	check(Manager);
	for (AActor* Actor : InActors)
	{
		Manager->ConvertActorToLightWeightInstance(Actor);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLightWeightInstancesEditorModule, LightWeightInstancesEditor)