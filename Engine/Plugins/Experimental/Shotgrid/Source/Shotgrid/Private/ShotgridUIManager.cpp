// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShotgridUIManager.h"
#include "ShotgridEngine.h"
#include "ShotgridStyle.h"

#include "AssetData.h"
#include "ContentBrowserModule.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#include "IPythonScriptPlugin.h"

#define LOCTEXT_NAMESPACE "Shotgrid"
#define LEVELEDITOR_MODULE_NAME TEXT("LevelEditor")
#define CONTENTBROWSER_MODULE_NAME TEXT("ContentBrowser")

TUniquePtr<FShotgridUIManagerImpl> FShotgridUIManager::Instance;

class FShotgridUIManagerImpl
{
public:
	void Initialize();
	void Shutdown();

private:
	FDelegateHandle ContentBrowserAssetExtenderDelegateHandle;
	FDelegateHandle LevelEditorExtenderDelegateHandle;

	bool bIsShotgridEnabled;

	void SetupShotgridMenu();
	void SetupShotgridContextMenus();
	void RemoveShotgridContextMenus();

	void FillToolbar(FToolBarBuilder& ToolbarBuilder);
	TSharedRef<SWidget> GenerateShotgridToolbarMenu();
	void GenerateShotgridMenuContent(FMenuBuilder& MenuBuilder, const TArray<FAssetData>* SelectedAssets, const TArray< AActor*>* SelectedActors);
	void GenerateShotgridAssetContextMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets);
	void GenerateShotgridActorContextMenu(FMenuBuilder& MenuBuilder, TArray< AActor*> SelectedActors);

	// Menu extender callbacks
	TSharedRef<FExtender> OnExtendLevelEditor(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors);
	TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
};

void FShotgridUIManagerImpl::Initialize()
{
	bIsShotgridEnabled = false;

	// Check if the bootstrap environment variable is set and that the script exists
	FString ShotgridBootstrap = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_SHOTGUN_BOOTSTRAP"));
	if (!ShotgridBootstrap.IsEmpty() && FPaths::FileExists(ShotgridBootstrap))
	{
		// The following environment variables must be set for the Shotgrid apps to be fully functional
		// These variables are automatically set when the editor is launched through Shotgrid Desktop
		FString ShotgridEngine = FPlatformMisc::GetEnvironmentVariable(TEXT("SHOTGUN_ENGINE"));
		FString ShotgridEntityType = FPlatformMisc::GetEnvironmentVariable(TEXT("SHOTGUN_ENTITY_TYPE"));
		FString ShotgridEntityId = FPlatformMisc::GetEnvironmentVariable(TEXT("SHOTGUN_ENTITY_ID"));

		if (ShotgridEngine == TEXT("tk-unreal") && !ShotgridEntityType.IsEmpty() && !ShotgridEntityId.IsEmpty())
		{
			bIsShotgridEnabled = true;

			// Set environment variable in the Python interpreter to enable the Shotgrid Unreal init script
			IPythonScriptPlugin::Get()->ExecPythonCommand(TEXT("import os\nos.environ['UE_SHOTGUN_ENABLED']='True'"));
		}
	}

	if (bIsShotgridEnabled)
	{
		FShotgridStyle::Initialize();

		SetupShotgridMenu();
		SetupShotgridContextMenus();
	}
}

void FShotgridUIManagerImpl::Shutdown()
{
	if (bIsShotgridEnabled)
	{
		RemoveShotgridContextMenus();

		FShotgridStyle::Shutdown();
	}
}

void FShotgridUIManagerImpl::SetupShotgridMenu()
{
	// Set the Shotgrid icons
	FShotgridStyle::SetIcon("Logo", "sg_logo_80px");
	FShotgridStyle::SetIcon("ContextLogo", "sg_context_logo");

	// Add a Shotgrid toolbar section after the settings section of the level editor
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(LEVELEDITOR_MODULE_NAME);

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
	ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, nullptr, FToolBarExtensionDelegate::CreateRaw(this, &FShotgridUIManagerImpl::FillToolbar));

	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
}

void FShotgridUIManagerImpl::SetupShotgridContextMenus()
{
	// Register Content Browser menu extender
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(CONTENTBROWSER_MODULE_NAME);

	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	CBAssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FShotgridUIManagerImpl::OnExtendContentBrowserAssetSelectionMenu));
	ContentBrowserAssetExtenderDelegateHandle = CBAssetMenuExtenderDelegates.Last().GetHandle();

	// Register Level Editor menu extender
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LEVELEDITOR_MODULE_NAME);

	TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors>& LevelEditorMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
	LevelEditorMenuExtenderDelegates.Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateRaw(this, &FShotgridUIManagerImpl::OnExtendLevelEditor));
	LevelEditorExtenderDelegateHandle = LevelEditorMenuExtenderDelegates.Last().GetHandle();
}

void FShotgridUIManagerImpl::RemoveShotgridContextMenus()
{
	if (FModuleManager::Get().IsModuleLoaded(LEVELEDITOR_MODULE_NAME))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(LEVELEDITOR_MODULE_NAME);
		TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors>& LevelEditorMenuExtenderDelegates = LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
		LevelEditorMenuExtenderDelegates.RemoveAll([this](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Delegate) { return Delegate.GetHandle() == LevelEditorExtenderDelegateHandle; });
	}

	if (FModuleManager::Get().IsModuleLoaded(CONTENTBROWSER_MODULE_NAME))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>(CONTENTBROWSER_MODULE_NAME);
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBAssetMenuExtenderDelegates.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate) { return Delegate.GetHandle() == ContentBrowserAssetExtenderDelegateHandle; });
	}
}

void FShotgridUIManagerImpl::FillToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection(TEXT("Shotgrid"));
	{
		// Add a drop-down menu (with a label and an icon for the drop-down button) to list the Shotgrid actions available
		ToolbarBuilder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FShotgridUIManagerImpl::GenerateShotgridToolbarMenu),
			LOCTEXT("ShotgridCombo_Label", "Shotgrid"),
			LOCTEXT("ShotgridCombo_Tooltip", "Available Shotgrid commands"),
			FSlateIcon(FShotgridStyle::GetStyleSetName(), "Shotgrid.Logo")
		);
	}
	ToolbarBuilder.EndSection();
}

TSharedRef<SWidget> FShotgridUIManagerImpl::GenerateShotgridToolbarMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	GenerateShotgridMenuContent(MenuBuilder, nullptr, nullptr);

	return MenuBuilder.MakeWidget();
}

void FShotgridUIManagerImpl::GenerateShotgridMenuContent(FMenuBuilder& MenuBuilder, const TArray<FAssetData>* SelectedAssets, const TArray< AActor*>* SelectedActors)
{
	if (UShotgridEngine* Engine = UShotgridEngine::GetInstance())
	{
		Engine->SetSelection(SelectedAssets, SelectedActors);

		// Query the available Shotgrid commands from the Shotgrid engine
		TArray<FShotgridMenuItem> MenuItems = Engine->GetShotgridMenuItems();
		for (const FShotgridMenuItem& MenuItem : MenuItems)
		{
			if (MenuItem.Type == TEXT("context_begin"))
			{
				MenuBuilder.BeginSection(NAME_None, FText::FromString(MenuItem.Title));
			}
			else if (MenuItem.Type == TEXT("context_end"))
			{
				MenuBuilder.EndSection();
			}
			else if (MenuItem.Type == TEXT("separator"))
			{
				MenuBuilder.AddMenuSeparator();
			}
			else
			{
				// The other menu types correspond to actual Shotgrid commands with an associated action
				FString CommandName = MenuItem.Title;
				MenuBuilder.AddMenuEntry(
					FText::FromString(CommandName),
					FText::FromString(MenuItem.Description),
					FSlateIcon(),
					FExecuteAction::CreateLambda([CommandName]()
					{
						if (UShotgridEngine* Engine = UShotgridEngine::GetInstance())
						{
							Engine->ExecuteCommand(CommandName);
						}
					})
				);
			}
		}
	}
}

void FShotgridUIManagerImpl::GenerateShotgridAssetContextMenu(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
{
	GenerateShotgridMenuContent(MenuBuilder, &SelectedAssets, nullptr);
}

void FShotgridUIManagerImpl::GenerateShotgridActorContextMenu(FMenuBuilder& MenuBuilder, TArray<AActor*> SelectedActors)
{
	GenerateShotgridMenuContent(MenuBuilder, nullptr, &SelectedActors);
}

TSharedRef<FExtender> FShotgridUIManagerImpl::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	// Menu extender for Content Browser context menu when an asset is selected
	TSharedRef<FExtender> Extender(new FExtender());

	if (SelectedAssets.Num() > 0)
	{
		Extender->AddMenuExtension("AssetContextReferences", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateLambda(
			[this, SelectedAssets](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddMenuSeparator();
				MenuBuilder.AddSubMenu(
					LOCTEXT("Shotgrid_ContextMenu", "Shotgrid"),
					FText(),
					FNewMenuDelegate::CreateRaw(this, &FShotgridUIManagerImpl::GenerateShotgridAssetContextMenu, SelectedAssets),
					false,
					FSlateIcon(FShotgridStyle::GetStyleSetName(), "Shotgrid.ContextLogo")
				);
			}));
	}

	return Extender;
}

TSharedRef<FExtender> FShotgridUIManagerImpl::OnExtendLevelEditor(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors)
{
	// Menu extender for Level Editor and World Outliner context menus when an actor is selected
	TSharedRef<FExtender> Extender(new FExtender());

	if (SelectedActors.Num() > 0)
	{
		Extender->AddMenuExtension("ActorUETools", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateLambda(
			[this, SelectedActors](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("Shotgrid_ContextMenu", "Shotgrid"),
					FText(),
					FNewMenuDelegate::CreateRaw(this, &FShotgridUIManagerImpl::GenerateShotgridActorContextMenu, SelectedActors),
					false,
					FSlateIcon(FShotgridStyle::GetStyleSetName(), "Shotgrid.ContextLogo")
				);
			}));
	}

	return Extender;
}

void FShotgridUIManager::Initialize()
{
	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FShotgridUIManagerImpl>();
		Instance->Initialize();
	}
}

void FShotgridUIManager::Shutdown()
{
	if (Instance.IsValid())
	{
		Instance->Shutdown();
		Instance.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
