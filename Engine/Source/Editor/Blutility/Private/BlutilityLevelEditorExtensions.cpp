// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlutilityLevelEditorExtensions.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "ActorActionUtility.h"
#include "EditorUtilityBlueprint.h"
#include "LevelEditor.h"
#include "BlueprintEditorModule.h"
#include "BlutilityMenuExtensions.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "BlutilityLevelEditorExtensions"

FDelegateHandle LevelViewportExtenderHandle;

class FBlutilityLevelEditorExtensions_Impl
{
public:
	static TSharedRef<FExtender> OnExtendLevelEditorActorContextMenu(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors)
	{
		TSharedRef<FExtender> Extender(new FExtender());
			   
		TMap<IEditorUtilityExtension*, TSet<int32>> UtilityAndSelectionIndices;

		// Run thru the actors to determine if any meet our criteria
		TArray<IEditorUtilityExtension*> SupportedUtils;
		TArray<AActor*> SupportedActors;
		if (SelectedActors.Num() > 0)
		{
			// Check blueprint utils (we need to load them to query their validity against these assets)
			TArray<FAssetData> UtilAssets;
			FBlutilityMenuExtensions::GetBlutilityClasses(UtilAssets, UActorActionUtility::StaticClass()->GetFName());

			// Collect all UActorActionUtility derived (non-blueprint) classes 
			TSet<UActorActionUtility*> AssetClasses;
			for (TObjectIterator<UClass> AssetActionClassIt; AssetActionClassIt; ++AssetActionClassIt)
			{
				if (AssetActionClassIt->IsChildOf(UActorActionUtility::StaticClass()) && UActorActionUtility::StaticClass()->GetFName() != AssetActionClassIt->GetFName() && AssetActionClassIt->ClassGeneratedBy == nullptr)
				{
					AssetClasses.Add(Cast<UActorActionUtility>(AssetActionClassIt->GetDefaultObject()));
				}
			}

			if (UtilAssets.Num() + AssetClasses.Num() > 0)
			{
				for (AActor* Actor : SelectedActors)
				{
					if (Actor)
					{
						auto ProcessAssetAction = [&SupportedUtils, &SupportedActors, &UtilityAndSelectionIndices, Actor](UActorActionUtility* InAction)
						{
							bool bPassesClassFilter = false;

							UClass* SupportedClass = InAction->GetSupportedClass();
							if (SupportedClass == nullptr || (SupportedClass && Actor->GetClass()->IsChildOf(SupportedClass)))
							{
								bPassesClassFilter = true;
							}
							
							if (bPassesClassFilter)
							{
								SupportedUtils.AddUnique(InAction);
								const int32 Index = SupportedActors.AddUnique(Actor);
								UtilityAndSelectionIndices.FindOrAdd(InAction).Add(Index);
							}
						};
						
						// Process asset based utilities
						for (const FAssetData& UtilAsset : UtilAssets)
						{
							if (UEditorUtilityBlueprint* Blueprint = Cast<UEditorUtilityBlueprint>(UtilAsset.GetAsset()))
							{
								if (UClass* BPClass = Blueprint->GeneratedClass.Get())
								{
									if (UActorActionUtility* DefaultObject = Cast<UActorActionUtility>(BPClass->GetDefaultObject()))
									{
										ProcessAssetAction(DefaultObject);
									}
								}
							}
						}

						// Process non-asset based utilities
						for (UActorActionUtility* Action : AssetClasses)
						{
							ProcessAssetAction(Action);
						}
					}
				}
			}
		}

		if (SupportedUtils.Num() > 0)
		{
			// Add asset actions extender
			Extender->AddMenuExtension(
				"ActorOptions",
				EExtensionHook::After,
				CommandList,
				FMenuExtensionDelegate::CreateStatic(&FBlutilityMenuExtensions::CreateActorBlutilityActionsMenu, UtilityAndSelectionIndices, SupportedActors));
		}

		return Extender;
	}
};

void FBlutilityLevelEditorExtensions::InstallHooks()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	auto& MenuExtenders = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();

	MenuExtenders.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateStatic(&FBlutilityLevelEditorExtensions_Impl::OnExtendLevelEditorActorContextMenu));
	LevelViewportExtenderHandle = MenuExtenders.Last().GetHandle();
}

void FBlutilityLevelEditorExtensions::RemoveHooks()
{
	if (LevelViewportExtenderHandle.IsValid())
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::Get().GetModulePtr<FLevelEditorModule>("LevelEditor");
		if (LevelEditorModule)
		{
			typedef FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors DelegateType;
			LevelEditorModule->GetAllLevelViewportContextMenuExtenders().RemoveAll([=](const DelegateType& In) { return In.GetHandle() == LevelViewportExtenderHandle; });
		}
	}
}

#undef LOCTEXT_NAMESPACE
