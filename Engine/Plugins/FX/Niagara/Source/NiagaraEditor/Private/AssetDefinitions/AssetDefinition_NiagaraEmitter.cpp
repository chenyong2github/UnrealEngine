// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_NiagaraEmitter.h"

#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ToolMenus.h"
#include "ToolMenuDelegates.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraSystemToolkit.h"
#include "ViewModels/NiagaraSystemViewModel.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraEmitter"

FLinearColor UAssetDefinition_NiagaraEmitter::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.Emitter");
}

TSoftClassPtr<> UAssetDefinition_NiagaraEmitter::GetAssetClass() const
{
	return UNiagaraEmitter::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraEmitter::GetAssetCategories() const
{
	static FAssetCategoryPath AssetPaths[] = { LOCTEXT("NiagaraAssetsCategory", "FX") };
	return AssetPaths;
}

EAssetCommandResult UAssetDefinition_NiagaraEmitter::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (OpenArgs.OpenMethod == EAssetOpenMethod::Edit)
	{
		EToolkitMode::Type Mode = OpenArgs.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
		TArray<UObject*> Objects = OpenArgs.LoadObjects<UObject>();
		for (UObject* OpenObj : Objects)
		{
			if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(OpenObj))
			{
				if (!Emitter->VersionToOpenInEditor.IsValid())
				{
					Emitter->VersionToOpenInEditor = Emitter->GetExposedVersion().VersionGuid;
				}
				TSharedRef<FNiagaraSystemToolkit> SystemToolkit(new FNiagaraSystemToolkit());
				SystemToolkit->InitializeWithEmitter(Mode, OpenArgs.ToolkitHost, *Emitter);
			}
		}
		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

namespace MenuExtension_Emitter
{
	void CreateUniqueAssetName(const FString& InBasePackageName, const FString& InSuffix, FString& OutPackageName, FString& OutAssetName)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(InBasePackageName, InSuffix, OutPackageName, OutAssetName);
	}
	
	static void ExecuteNewNiagaraSystem(const FToolMenuContext& MenuContext)
	{
		if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext))
		{
			TArray<UNiagaraEmitter*> Emitters = Context->LoadSelectedObjects<UNiagaraEmitter>();
			const FString DefaultSuffix = TEXT("_System");

			FNiagaraSystemViewModelOptions SystemOptions;
			SystemOptions.bCanModifyEmittersFromTimeline = true;
			SystemOptions.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;

			TArray<UObject*> ObjectsToSync;
			for (UNiagaraEmitter* Emitter : Emitters)
			{
				if (Emitter)
				{
					// Determine an appropriate names
					FString Name;
					FString PackageName;
					CreateUniqueAssetName(Emitter->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

					// Create the factory used to generate the asset
					UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
					Factory->EmittersToAddToNewSystem.Add(FVersionedNiagaraEmitter(Emitter, Emitter->GetExposedVersion().VersionGuid));
					FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");
					UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, FPackageName::GetLongPackagePath(PackageName), UNiagaraSystem::StaticClass(), Factory);
			
					UNiagaraSystem* System = Cast<UNiagaraSystem>(NewAsset);
					if (System != nullptr)
					{
						ObjectsToSync.Add(NewAsset);
					}
				}
			}

			if (ObjectsToSync.Num() > 0)
			{
				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
			}
		}
	}

	static void ExecuteCreateDuplicateParent(const FToolMenuContext& MenuContext)
	{
		if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext))
		{
			TArray<UNiagaraEmitter*> Emitters = Context->LoadSelectedObjects<UNiagaraEmitter>();
			const FString DefaultSuffix = TEXT("_Parent");

			TArray<UObject*> ObjectsToSync;
			FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

			for (UNiagaraEmitter* Emitter : Emitters)
			{
				if (Emitter == nullptr)
				{
					continue;
				}
				FString Name;
				FString PackageName;
				CreateUniqueAssetName(Emitter->GetOutermost()->GetName(), DefaultSuffix, PackageName, Name);

				// Create the factory used to generate the asset

				if (UNiagaraEmitter* NewEmitter = Cast<UNiagaraEmitter>(AssetToolsModule.Get().DuplicateAssetWithDialog(Name, FPackageName::GetLongPackagePath(PackageName), Emitter)))
				{
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Emitter);

					NewEmitter->Modify();
					NewEmitter->GetLatestEmitterData()->GraphSource->MarkNotSynchronized(TEXT("Emitter created"));

					Emitter->Modify();
					Emitter->SetParent(FVersionedNiagaraEmitter(NewEmitter, NewEmitter->GetExposedVersion().VersionGuid));
					Emitter->GetLatestEmitterData()->GraphSource->MarkNotSynchronized(TEXT("Emitter parent changed"));

					ObjectsToSync.Add(NewEmitter);
				}
			}

			if (ObjectsToSync.Num() > 0)
			{
				AssetToolsModule.Get().OpenEditorForAssets(ObjectsToSync);

				FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
			}
		}
	}

	static bool CanCreateDuplicateParent(const FToolMenuContext& MenuContext)
	{
		if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext))
		{
			TArray<UNiagaraEmitter*> Emitters = Context->LoadSelectedObjects<UNiagaraEmitter>();
			for (UNiagaraEmitter* EmitterPtr : Emitters)
			{
				if (EmitterPtr && EmitterPtr->IsVersioningEnabled())
				{
					return false;
				}
			}
		}
		return true;
	}

	static void MarkDependentCompilableAssetsDirty(const FToolMenuContext& MenuContext)
	{
		if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext))
		{
			FNiagaraEditorUtilities::MarkDependentCompilableAssetsDirty(Context->SelectedAssets);
		}
	}
	
	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UNiagaraEmitter::StaticClass());
	        
			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddDynamicEntry("GetAssetActions_Emitter", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				{
					const TAttribute Label = LOCTEXT("Emitter_NewNiagaraSystem", "Create Niagara System");
					const TAttribute ToolTip = LOCTEXT("Emitter_NewNiagaraSystemTooltip", "Creates a Niagara system using this emitter as a base.");
					const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.ParticleSystem");
					const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&ExecuteNewNiagaraSystem);

					InSection.AddMenuEntry("Emitter_NewNiagaraSystem", Label, ToolTip, Icon, UIAction);
				}
				{
					const TAttribute Label = LOCTEXT("Emitter_CreateDuplicateParentLabel", "Create Duplicate Parent");
					const TAttribute ToolTip = LOCTEXT("Emitter_CreateDuplicateParentTooltip", "Duplicate this emitter and set this emitter's parent to the new emitter.");
					const FSlateIcon Icon = FSlateIcon();
					FToolUIAction UIAction = FToolMenuExecuteAction::CreateStatic(&ExecuteCreateDuplicateParent);
					UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&CanCreateDuplicateParent);

					InSection.AddMenuEntry("Emitter_CreateDuplicateParent", Label, ToolTip, Icon, UIAction);
				}
				{
					const TAttribute Label = LOCTEXT("MarkDependentCompilableAssetsDirtyLabel", "Mark Dependent Compilable Assets Dirty");
					const TAttribute ToolTip = LOCTEXT("MarkDependentCompilableAssetsDirtyToolTip", "Finds all niagara assets which depend on this asset either directly or indirectly,\n and marks them dirty so they can be saved with the latest version.");
					const FSlateIcon Icon = FSlateIcon();
					const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&MarkDependentCompilableAssetsDirty);

					InSection.AddMenuEntry("MarkDependentCompilableAssetsDirty", Label, ToolTip, Icon, UIAction);
				}
			}));
		}));
	});
}

#undef LOCTEXT_NAMESPACE
