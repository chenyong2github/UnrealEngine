// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorModule.h"

#include "ContentBrowserMenuContexts.h"
#include "LevelEditorMenuContext.h"
#include "UVEditor.h"
#include "UVEditorCommands.h"
#include "UVEditorMode.h"
#include "UVEditorSubsystem.h"
#include "EditorModeRegistry.h"

#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FUVEditorModule"

void FUVEditorModule::StartupModule()
{
	FUVEditorCommands::Register();

	// Menus need to be registered in a callback to make sure the system is ready for them.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUVEditorModule::RegisterMenus));
}

void FUVEditorModule::ShutdownModule()
{
	// Clean up menu things
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FUVEditorCommands::Unregister();

	FEditorModeRegistry::Get().UnregisterMode(UUVEditorMode::EM_UVEditorModeId);
}

void FUVEditorModule::RegisterMenus()
{
	// Allows cleanup when module unloads.
	FToolMenuOwnerScoped OwnerScoped(this);

	// Extent the content browser context menu for static meshes
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.StaticMesh");
		FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");

		Section.AddDynamicEntry("OpenUVEditor", FNewToolMenuSectionDelegate::CreateLambda(
			[this](FToolMenuSection& Section) 
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

						Section.AddMenuEntryWithCommandList(FUVEditorCommands::Get().OpenUVEditor, CommandListToBind);
					}
				}
			}));
	}

	// Extent the level editor context menu
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu");
		FToolMenuSection& Section = Menu->FindOrAddSection("ActorAsset");
		Section.AddDynamicEntry("OpenUVEditor", FNewToolMenuSectionDelegate::CreateLambda(
			[this](FToolMenuSection& Section) 
			{
				TArray<UObject*> ReferencedAssets;
				GEditor->GetReferencedAssetsForEditorSelection(ReferencedAssets);

				UUVEditorSubsystem* UVSubsystem = GEditor->GetEditorSubsystem<UUVEditorSubsystem>();
				check(UVSubsystem);

				if (UVSubsystem->AreObjectsValidTargets(ReferencedAssets))
				{
					TArray<TObjectPtr<UObject>> AssetsToEdit;
					AssetsToEdit.Append(ReferencedAssets);

					TSharedPtr<class FUICommandList> CommandListToBind = MakeShared<FUICommandList>();
					CommandListToBind->MapAction(
						FUVEditorCommands::Get().OpenUVEditor,
						FExecuteAction::CreateUObject(UVSubsystem, &UUVEditorSubsystem::StartUVEditor, AssetsToEdit));

					Section.AddMenuEntryWithCommandList(FUVEditorCommands::Get().OpenUVEditor, CommandListToBind);
				}
			}));
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUVEditorModule, UVEditor)