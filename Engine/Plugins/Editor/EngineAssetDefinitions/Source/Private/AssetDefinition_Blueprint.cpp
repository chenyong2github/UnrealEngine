// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_Blueprint.h"

#include "AssetDefinitionRegistry.h"
#include "AssetToolsModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "BlueprintEditorModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "IAssetTools.h"
#include "ToolMenus.h"
#include "Factories/BlueprintFactory.h"
#include "Internationalization/LocKeyFuncs.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "IContentBrowserSingleton.h"
#include "Algo/AllOf.h"
#include "Logging/MessageLog.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_Blueprint"

// Menu Extensions
//--------------------------------------------------------------------

namespace MenuExtension_Blueprint
{
	static bool CanExecuteNewDerivedBlueprint(const FToolMenuContext& MenuContext, const FAssetData* SelectedBlueprintPtr)
	{
		const uint32 BPFlags = SelectedBlueprintPtr->GetTagValueRef<uint32>(FBlueprintTags::ClassFlags);
		if ((BPFlags & (CLASS_Deprecated)) == 0)
		{
			return true;
		}

		return false;
	}

	static void ExecuteNewDerivedBlueprint(const FToolMenuContext& MenuContext, const FAssetData* SelectedBlueprintPtr)
	{
		if (UBlueprint* ParentBlueprint = Cast<UBlueprint>(SelectedBlueprintPtr->GetAsset()))
		{
			UClass* TargetParentClass = ParentBlueprint->GeneratedClass;

			if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(TargetParentClass))
			{
				FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("InvalidClassToMakeBlueprintFrom", "Invalid class with which to make a Blueprint."));
				return;
			}

			FString Name;
			FString PackageName;
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().CreateUniqueAssetName(ParentBlueprint->GetOutermost()->GetName(), TEXT("_Child"), PackageName, Name);
			const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

			const UAssetDefinition_Blueprint* BlueprintAssetDefinition = Cast<UAssetDefinition_Blueprint>(UAssetDefinitionRegistry::Get()->GetAssetDefinitionForClass(ParentBlueprint->GetClass()));
			if (BlueprintAssetDefinition)
			{
				UFactory* Factory = BlueprintAssetDefinition->GetFactoryForBlueprintType(ParentBlueprint);

				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().CreateNewAsset(Name, PackagePath, ParentBlueprint->GetClass(), Factory);
			}
		}
	}

	static void ExecuteEditDefaults(const FToolMenuContext& MenuContext, TArray<FAssetData> BlueprintAssets)
	{
		TArray<UBlueprint*> Blueprints;

		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.NewPage(LOCTEXT("ExecuteEditDefaultsNewLogPage", "Loading Blueprints"));

		for (const FAssetData& BlueprintAsset : BlueprintAssets)
		{
			if (UBlueprint* Object = Cast<UBlueprint>(BlueprintAsset.GetAsset()))
			{
				// If the blueprint is valid, allow it to be added to the list, otherwise log the error.
				if ( Object->SkeletonGeneratedClass && Object->GeneratedClass )
				{
					Blueprints.Add(Object);
				}
				else
				{
					FFormatNamedArguments Arguments;
					Arguments.Add(TEXT("ObjectName"), FText::FromString(Object->GetName()));
					EditorErrors.Error(FText::Format(LOCTEXT("LoadBlueprint_FailedLog", "{ObjectName} could not be loaded because it derives from an invalid class.  Check to make sure the parent class for this blueprint hasn't been removed!"), Arguments ) );
				}
			}
		}

		if ( Blueprints.Num() > 0 )
		{
			FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>( "Kismet" );
			TSharedRef< IBlueprintEditor > NewBlueprintEditor = BlueprintEditorModule.CreateBlueprintEditor(  EToolkitMode::Standalone, TSharedPtr<IToolkitHost>(), Blueprints );
		}

		// Report errors
		EditorErrors.Notify(LOCTEXT("OpenDefaults_Failed", "Opening Class Defaults Failed!"));
	}
	
	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []{ 
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UBlueprint::StaticClass());
	        
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
				Section.AddDynamicEntry("GetAssetActions_Blueprint", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
				{
					if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection))
					{
						// TODO NDarnell re: EIncludeSubclasses::No, Temporary - Need to ensure we don't have duplicates for now, because no all subclasses of blueprint are of this class yet.
						if (const FAssetData* SelectedBlueprintPtr = Context->GetSingleSelectedAssetOfType(UBlueprint::StaticClass(), EIncludeSubclasses::No))
						{
							const TAttribute<FText> Label = LOCTEXT("Blueprint_NewDerivedBlueprint", "Create Child Blueprint Class");
							const TAttribute<FText> ToolTip = TAttribute<FText>::CreateLambda([SelectedBlueprintPtr]()
							{
								const uint32 BPFlags = SelectedBlueprintPtr->GetTagValueRef<uint32>(FBlueprintTags::ClassFlags);
								if ((BPFlags & (CLASS_Deprecated)) == 0)
								{
									return LOCTEXT("Blueprint_NewDerivedBlueprintTooltip", "Creates a Child Blueprint Class based on the current Blueprint, allowing you to create variants easily.");
								}
								else
								{
									return LOCTEXT("Blueprint_NewDerivedBlueprintIsDeprecatedTooltip", "Blueprint class is deprecated, cannot derive a child Blueprint!");
								}
							});
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Blueprint");

							FToolUIAction DeriveNewBlueprint;
							DeriveNewBlueprint.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteNewDerivedBlueprint, SelectedBlueprintPtr);
							DeriveNewBlueprint.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&CanExecuteNewDerivedBlueprint, SelectedBlueprintPtr);
							InSection.AddMenuEntry("CreateChildBlueprintClass", Label, ToolTip, Icon, DeriveNewBlueprint);
						}

						TArray<FAssetData> SelectedBlueprints = Context->GetSelectedAssetsOfType(UBlueprint::StaticClass(), EIncludeSubclasses::No);
						if (SelectedBlueprints.Num() > 1)
						{
							TArray<UClass*> SelectedBlueprintParentClasses;
							Algo::Transform(SelectedBlueprints, SelectedBlueprintParentClasses, [](const FAssetData& BlueprintAsset){ return UBlueprint::GetBlueprintParentClassFromAssetTags(BlueprintAsset); });
							
							// Ensure that all the selected blueprints are actors
							const bool bAreAllSelectedBlueprintsActors =
								Algo::AllOf(SelectedBlueprintParentClasses, [](UClass* ParentClass){ return ParentClass->IsChildOf(AActor::StaticClass()); });
							
							if (bAreAllSelectedBlueprintsActors)
							{
								const TAttribute<FText> Label = LOCTEXT("Blueprint_EditDefaults", "Edit Shared Defaults");
								const TAttribute<FText> ToolTip = LOCTEXT("Blueprint_EditDefaultsTooltip", "Edit the shared default properties of the selected actor blueprints.");
								const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.Tabs.BlueprintDefaults");
								const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&ExecuteEditDefaults, MoveTemp(SelectedBlueprints));
								InSection.AddMenuEntry("Blueprint_EditDefaults", Label, ToolTip, Icon, UIAction);
							}
						}
					}
				}));
		}));
	});
}


UFactory* UAssetDefinition_Blueprint::GetFactoryForBlueprintType(UBlueprint* InBlueprint) const
{
	UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
	BlueprintFactory->ParentClass = InBlueprint->GeneratedClass;
	return BlueprintFactory;
}

#undef LOCTEXT_NAMESPACE
