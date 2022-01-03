// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorModule.h"

#include "ContentBrowserMenuContexts.h"
#include "DetailsCustomizations/UVSelectToolCustomizations.h"
#include "EditorModeRegistry.h"
#include "LevelEditorMenuContext.h"
#include "PropertyEditorModule.h"
#include "Selection.h"
#include "UVEditor.h"
#include "UVEditorCommands.h"
#include "UVEditorMode.h"
#include "UVEditorStyle.h"
#include "UVEditorSubsystem.h"
#include "UVSelectTool.h"

#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FUVEditorModule"

void FUVEditorModule::StartupModule()
{
	FUVEditorStyle::Get(); // Causes the constructor to be called
	FUVEditorCommands::Register();

	// Menus need to be registered in a callback to make sure the system is ready for them.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUVEditorModule::RegisterMenus));

	// Register details view customizations
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	ClassesToUnregisterOnShutdown.Reset();
	PropertyModule.RegisterCustomClassLayout(USelectToolActionPropertySet::StaticClass()->GetFName(), 
		FOnGetDetailCustomizationInstance::CreateStatic(&FUVSelectToolActionPropertySetDetails::MakeInstance));
	ClassesToUnregisterOnShutdown.Add(USelectToolActionPropertySet::StaticClass()->GetFName());
}

void FUVEditorModule::ShutdownModule()
{
	// Clean up menu things
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FUVEditorCommands::Unregister();

	FEditorModeRegistry::Get().UnregisterMode(UUVEditorMode::EM_UVEditorModeId);

	// Unregister customizations
	FPropertyEditorModule* PropertyEditorModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
	if (PropertyEditorModule)
	{
		for (FName ClassName : ClassesToUnregisterOnShutdown)
		{
			PropertyEditorModule->UnregisterCustomClassLayout(ClassName);
		}
	}
}

void FUVEditorModule::RegisterMenus()
{
	// Allows cleanup when module unloads.
	FToolMenuOwnerScoped OwnerScoped(this);

	// Extend the content browser context menu for static meshes and skeletal meshes
	auto AddToContextMenuSection = [this](FToolMenuSection& Section)
	{
		Section.AddDynamicEntry("OpenUVEditor", FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& Section)
			{
				// We'll need to get the target objects out of the context
				if (UContentBrowserAssetContextMenuContext* Context = Section.FindContext<UContentBrowserAssetContextMenuContext>())
				{
					TArray<TObjectPtr<UObject>> AssetsToEdit;
					AssetsToEdit.Append(Context->GetSelectedObjects());

					UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
					check(UVSubsystem);

					if (UVSubsystem->AreObjectsValidTargets(AssetsToEdit))
					{
						TSharedPtr<class FUICommandList> CommandListToBind = MakeShared<FUICommandList>();
						CommandListToBind->MapAction(
							FUVEditorCommands::Get().OpenUVEditor,
							FExecuteAction::CreateUObject(UVSubsystem, &UUVEditorSubsystem::StartUVEditor, AssetsToEdit));

						Section.AddMenuEntryWithCommandList(FUVEditorCommands::Get().OpenUVEditor, CommandListToBind, TAttribute<FText>(), TAttribute<FText>(), FSlateIcon(FUVEditorStyle::Get().GetStyleSetName(), "UVEditor.OpenUVEditor"));
					}
				}
			}));
	};
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.StaticMesh");
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		AddToContextMenuSection(Section);
	}
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.SkeletalMesh");
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
		AddToContextMenuSection(Section);
	}

	// Extend the level editor context menu
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu.AssetToolsSubMenu");

		FToolMenuSection& Section = Menu->AddSection("UVEditorCommands", TAttribute<FText>(), FToolMenuInsert("AssetTools", EToolMenuInsertType::After));

		Section.AddDynamicEntry("OpenUVEditor", FNewToolMenuSectionDelegate::CreateLambda(
			[this](FToolMenuSection& Section) 
			{
				TArray<TObjectPtr<UObject>> TargetObjects;

				// TODO: There's some newer way to iterate across selected actors that we can switch over to someday
				for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
				{
					// We are interested in the (unique) assets backing the actor, or else the actor
					// itself if it is not asset backed (such as UDynamicMesh).
					AActor* Actor = static_cast<AActor*>(*It);
					TArray<UObject*> ActorAssets;
					Actor->GetReferencedContentObjects(ActorAssets);

					if (ActorAssets.Num() > 0)
					{
						for (UObject* Asset : ActorAssets)
						{
							TargetObjects.AddUnique(Asset);
						}
					}
					else
					{
						// Need to transform actors to components here because this is what our tool targets expect
						TInlineComponentArray<UActorComponent*> ActorComponents;
						Actor->GetComponents(ActorComponents);
						TargetObjects.Append(ActorComponents);
					}
				}

				UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
				check(UVSubsystem);

				bool bValidTargets = UVSubsystem->AreObjectsValidTargets(TargetObjects);

				TSharedPtr<class FUICommandList> CommandListToBind = MakeShared<FUICommandList>();
				CommandListToBind->MapAction(
					FUVEditorCommands::Get().OpenUVEditor,
					FExecuteAction::CreateUObject(UVSubsystem, &UUVEditorSubsystem::StartUVEditor, TargetObjects),
					FCanExecuteAction::CreateLambda([bValidTargets]() { return bValidTargets; }));

				Section.AddMenuEntryWithCommandList(FUVEditorCommands::Get().OpenUVEditor, CommandListToBind);
			}));
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUVEditorModule, UVEditor)