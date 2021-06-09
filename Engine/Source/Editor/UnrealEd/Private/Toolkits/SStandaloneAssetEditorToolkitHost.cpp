// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/SStandaloneAssetEditorToolkitHost.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBorder.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"
#include "Toolkits/ToolkitManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "UObject/Package.h"

#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "StandaloneAssetEditorToolkit"

void SStandaloneAssetEditorToolkitHost::Construct( const SStandaloneAssetEditorToolkitHost::FArguments& InArgs, const TSharedPtr<FTabManager>& InTabManager, const FName InitAppName )
{
	EditorCloseRequest = InArgs._OnRequestClose;
	EditorClosing = InArgs._OnClose;
	AppName = InitAppName;

	MyTabManager = InTabManager;
}

void SStandaloneAssetEditorToolkitHost::SetupInitialContent( const TSharedRef<FTabManager::FLayout>& DefaultLayout, const TSharedPtr<SDockTab>& InHostTab, const bool bCreateDefaultStandaloneMenu )
{
	// @todo toolkit major: Expose common asset editing features here! (or require the asset editor's content to do this itself!)
	//		- Add a "toolkit menu"
	//				- Toolkits can access this and add menu items as needed
	//				- In world-centric, main frame menu becomes extendable
	//						- e.g., "Blueprint", "Debug" menus added
	//				- In standalone, toolkits get their own menu
	//						- Also, the core menu is just added as the first pull-down in the standalone menu
	//				- Multiple toolkits can be active and add their own menu items!
	//				- In world-centric, the core toolkit menu is available from the drop down
	//						- No longer need drop down next to toolkit display?  Not sure... Probably still want this
	//		- Add a "toolkit toolbar"
	//				- In world-centric, draws next to the level editor tool bar (or on top of)
	//						- Could either extend existing tool bar or add additional tool bars
	//						- May need to change arrangement to allow for wider tool bars (maybe displace grid settings too)
	//				- In standalone, just draws under the toolkit's menu

	const FName AssetEditorMenuName = GetMenuName();
	if (!UToolMenus::Get()->IsMenuRegistered(AssetEditorMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(AssetEditorMenuName, "MainFrame.MainMenu");

		if (bCreateDefaultStandaloneMenu)
		{
			CreateDefaultStandaloneMenuBar(Menu);
		}
	}

	DefaultMenuWidget = SNullWidget::NullWidget;

	HostTabPtr = InHostTab;

	RestoreFromLayout(DefaultLayout);
	GenerateMenus(bCreateDefaultStandaloneMenu);
}

void SStandaloneAssetEditorToolkitHost::CreateDefaultStandaloneMenuBar(UToolMenu* MenuBar)
{
	struct Local
	{
		static void ExtendFileMenu(UToolMenu* InMenuBar)
		{
			const FName MenuName = *(InMenuBar->GetMenuName().ToString() + TEXT(".") + TEXT("File"));
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName);
			FToolMenuSection& Section = Menu->FindOrAddSection("FileLoadAndSave");
			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				InSection.FindContext<UAssetEditorToolkitMenuContext>()->Toolkit.Pin()->FillDefaultFileMenuCommands(InSection);
			}));
		}

		static void FillAssetMenu(UToolMenu* InMenu)
		{
			FToolMenuSection& Section = InMenu->AddSection("AssetEditorActions", LOCTEXT("ActionsHeading", "Actions"));
			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				InSection.FindContext<UAssetEditorToolkitMenuContext>()->Toolkit.Pin()->FillDefaultAssetMenuCommands(InSection);
			}));
		}

		static void ExtendHelpMenu(UToolMenu* InMenuBar)
		{
			const FName MenuName = *(InMenuBar->GetMenuName().ToString() + TEXT(".") + TEXT("Help"));
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName);
			FToolMenuSection& Section = Menu->AddSection("HelpBrowse", NSLOCTEXT("MainHelpMenu", "Browse", "Browse"));
			Section.InsertPosition = FToolMenuInsert("HelpOnline", EToolMenuInsertType::Before);
			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				InSection.FindContext<UAssetEditorToolkitMenuContext>()->Toolkit.Pin()->FillDefaultHelpMenuCommands(InSection);
			}));
		}
	};

	// Add asset-specific menu items to the top of the "File" menu
	Local::ExtendFileMenu(MenuBar);

	// Add the "Asset" menu, if we're editing an asset
	MenuBar->FindOrAddSection(NAME_None).AddDynamicEntry("DynamicAssetEntry", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		UAssetEditorToolkitMenuContext* Context = InSection.FindContext<UAssetEditorToolkitMenuContext>();
		if (Context->Toolkit.Pin()->IsActuallyAnAsset())
		{
			InSection.AddSubMenu(
				"Asset",
				LOCTEXT("AssetMenuLabel", "Asset"),		// @todo toolkit major: Either use "Asset", "File", or the asset type name e.g. "Blueprint" (Also update custom pull-down menus)
				LOCTEXT("AssetMenuLabel_ToolTip", "Opens a menu with commands for managing this asset"),
				FNewToolMenuDelegate::CreateStatic(&Local::FillAssetMenu)
				).InsertPosition = FToolMenuInsert("Edit", EToolMenuInsertType::After);
		}
	}));

	// Add asset-specific menu items to the "Help" menu
	Local::ExtendHelpMenu(MenuBar);
}


void SStandaloneAssetEditorToolkitHost::RestoreFromLayout( const TSharedRef<FTabManager::FLayout>& NewLayout )
{
	const TSharedRef<SDockTab> HostTab = HostTabPtr.Pin().ToSharedRef();
	HostTab->SetCanCloseTab(EditorCloseRequest);
	HostTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(this, &SStandaloneAssetEditorToolkitHost::OnTabClosed));

	this->ChildSlot[SNullWidget::NullWidget];
	MyTabManager->CloseAllAreas();

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow( HostTab );
	TSharedPtr<SWidget> RestoredUI = MyTabManager->RestoreFrom( NewLayout, ParentWindow );

	checkf(RestoredUI.IsValid(), TEXT("The layout must have a primary dock area") );
	
	MenuOverlayWidgetContent.Reset();
	MenuWidgetContent.Reset();
	this->ChildSlot
	[
		SNew( SVerticalBox )
			// Menu bar area
			+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SOverlay)
					// The menu bar
					+SOverlay::Slot()
					[
						SAssignNew(MenuWidgetContent, SBorder)
						.Padding(0)
						.BorderImage(FEditorStyle::GetBrush("NoBorder"))
						[
							DefaultMenuWidget.ToSharedRef()
						]
					]
					// The menu bar overlay
					+SOverlay::Slot()
					.HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						[
							SAssignNew(MenuOverlayWidgetContent, SBorder)
							.Padding(0)
							.BorderImage(FEditorStyle::GetBrush("NoBorder"))
						]				
					]
				]
			// Viewport/Document/Docking area
			+SVerticalBox::Slot()
				.Padding( 1.0f )
				
				// Fills all leftover space
				.FillHeight( 1.0f )
				[
					RestoredUI.ToSharedRef()
				]
	];
}

FName SStandaloneAssetEditorToolkitHost::GetMenuName() const
{
	FName MenuAppName;
	if (HostedAssetEditorToolkit.IsValid())
	{
		MenuAppName = HostedAssetEditorToolkit->GetToolMenuAppName();
	}
	else
	{
		MenuAppName = AppName;
	}

	return *(FString(TEXT("AssetEditor.")) + MenuAppName.ToString() + TEXT(".MainMenu"));
}

void SStandaloneAssetEditorToolkitHost::GenerateMenus(bool bForceCreateMenu)
{
	if( bForceCreateMenu || DefaultMenuWidget != SNullWidget::NullWidget )
	{
		const FName AssetEditorMenuName = GetMenuName();

		UAssetEditorToolkitMenuContext* ContextObject = NewObject<UAssetEditorToolkitMenuContext>();
		ContextObject->Toolkit = HostedAssetEditorToolkit;
		FToolMenuContext ToolMenuContext(HostedAssetEditorToolkit->GetToolkitCommands(), FExtender::Combine(MenuExtenders).ToSharedRef(), ContextObject);
		HostedAssetEditorToolkit->InitToolMenuContext(ToolMenuContext);
		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
		DefaultMenuWidget = MainFrameModule.MakeMainMenu( MyTabManager, AssetEditorMenuName, ToolMenuContext );

		MenuWidgetContent->SetContent(DefaultMenuWidget.ToSharedRef());
	}
}

void SStandaloneAssetEditorToolkitHost::SetMenuOverlay( TSharedRef<SWidget> NewOverlay )
{
	MenuOverlayWidgetContent->SetContent(NewOverlay);
}

SStandaloneAssetEditorToolkitHost::~SStandaloneAssetEditorToolkitHost()
{
	// Let the toolkit manager know that we're going away now
	FToolkitManager::Get().OnToolkitHostDestroyed( this );
	HostedToolkits.Reset();
	HostedAssetEditorToolkit.Reset();
}


TSharedRef< SWidget > SStandaloneAssetEditorToolkitHost::GetParentWidget()
{
	return AsShared();
}


void SStandaloneAssetEditorToolkitHost::BringToFront()
{
	// If our host window is not active, force it to front to ensure the tab will be visible
	// The tab manager won't activate a tab on an inactive window in all cases
	const TSharedPtr<SDockTab> HostTab = HostTabPtr.Pin();
	if (HostTab.IsValid())
	{
		TSharedPtr<SWindow> ParentWindow = HostTab->GetParentWindow();
		if (ParentWindow.IsValid() && !ParentWindow->IsActive())
		{
			ParentWindow->BringToFront();
		}
	}

	FGlobalTabmanager::Get()->DrawAttentionToTabManager( this->MyTabManager.ToSharedRef() );
}


TSharedRef< SDockTabStack > SStandaloneAssetEditorToolkitHost::GetTabSpot( const EToolkitTabSpot::Type TabSpot )
{
	return TSharedPtr<SDockTabStack>().ToSharedRef();
}


void SStandaloneAssetEditorToolkitHost::OnToolkitHostingStarted( const TSharedRef< class IToolkit >& Toolkit )
{
	// Keep track of the toolkit we're hosting
	HostedToolkits.Add(Toolkit);

	// The tab manager needs to know how to spawn tabs from this toolkit
	Toolkit->RegisterTabSpawners(MyTabManager.ToSharedRef());

	if (!HostedAssetEditorToolkit.IsValid())
	{
		HostedAssetEditorToolkit = StaticCastSharedRef<FAssetEditorToolkit>(Toolkit);
	}
	else
	{
		HostedAssetEditorToolkit->OnToolkitHostingStarted(Toolkit);
	}
}


void SStandaloneAssetEditorToolkitHost::OnToolkitHostingFinished( const TSharedRef< class IToolkit >& Toolkit )
{
	// The tab manager should forget how to spawn tabs from this toolkit
	Toolkit->UnregisterTabSpawners(MyTabManager.ToSharedRef());

	HostedToolkits.Remove(Toolkit);

	// Standalone Asset Editors close by shutting down their major tab.
	if (Toolkit == HostedAssetEditorToolkit)
	{
		HostedAssetEditorToolkit.Reset();

		const TSharedPtr<SDockTab> HostTab = HostTabPtr.Pin();
		if (HostTab.IsValid())
		{
			HostTab->RequestCloseTab();
		}
	}
	else if (HostedAssetEditorToolkit.IsValid())
	{
		HostedAssetEditorToolkit->OnToolkitHostingFinished(Toolkit);
	}
}


UWorld* SStandaloneAssetEditorToolkitHost::GetWorld() const 
{
	// Currently, standalone asset editors never have a world
	UE_LOG(LogInit, Warning, TEXT("IToolkitHost::GetWorld() doesn't make sense in SStandaloneAssetEditorToolkitHost currently"));
	return NULL;
}


FReply SStandaloneAssetEditorToolkitHost::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	// Check to see if any of the actions for the level editor can be processed by the current event
	// If we are in debug mode do not process commands
	if (FSlateApplication::Get().IsNormalExecution())
	{
		// Figure out if any of our toolkit's tabs is the active tab.  This is important because we want
		// the toolkit to have it's own keybinds (which may overlap the level editor's keybinds or any
		// other toolkit).  When a toolkit tab is active, we give that toolkit a chance to process
		// commands instead of the level editor.
		TSharedPtr< IToolkit > ActiveToolkit;
		{
			const TSharedPtr<SDockableTab> CurrentActiveTab;// = FSlateApplication::xxxGetGlobalTabManager()->GetActiveTab();

			for (auto HostedToolkitIt = HostedToolkits.CreateConstIterator(); HostedToolkitIt && !ActiveToolkit.IsValid(); ++HostedToolkitIt)
			{
				const auto& CurToolkit = *HostedToolkitIt;
				if (CurToolkit.IsValid())
				{
					// Iterate over this toolkits spawned tabs
					const auto& ToolkitTabsInSpots = CurToolkit->GetToolkitTabsInSpots();

					for (auto CurSpotIt(ToolkitTabsInSpots.CreateConstIterator()); CurSpotIt && !ActiveToolkit.IsValid(); ++CurSpotIt)
					{
						const auto& TabsForSpot = CurSpotIt.Value();
						for (auto CurTabIt(TabsForSpot.CreateConstIterator()); CurTabIt; ++CurTabIt)
						{
							const auto& PinnedTab = CurTabIt->Pin();
							if (PinnedTab.IsValid())
							{
								if (PinnedTab == CurrentActiveTab)
								{
									ActiveToolkit = CurToolkit;
								}
							}
						}
					}
				}
			}
		}

		//@TODO: This seems wrong (should prioritize it but not totally block it)
		if (ActiveToolkit.IsValid())
		{
			// A toolkit tab is active, so direct all command processing to it
			if (ActiveToolkit->ProcessCommandBindings(InKeyEvent))
			{
				return FReply::Handled();
			}
		}
		else
		{
			// No toolkit tab is active, so let the underlying asset editor have a chance at the keystroke
			if (HostedAssetEditorToolkit->ProcessCommandBindings(InKeyEvent))
			{
				return FReply::Handled();
			}
		}
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}


void SStandaloneAssetEditorToolkitHost::OnTabClosed(TSharedRef<SDockTab> TabClosed) const
{
	check(TabClosed == HostTabPtr.Pin());

	EditorClosing.ExecuteIfBound();
	MyTabManager->SetMenuMultiBox(nullptr);
	
	if(HostedAssetEditorToolkit.IsValid())
	{
		const TArray<UObject*>* const ObjectsBeingEdited = HostedAssetEditorToolkit->GetObjectsCurrentlyBeingEdited();
		if(ObjectsBeingEdited)
		{
			const bool IsDockedAssetEditor = TabClosed->HasSiblingTab(FName("DockedToolkit"), false/*TreatIndexNoneAsWildcard*/);
			const EAssetEditorToolkitTabLocation AssetEditorToolkitTabLocation = (IsDockedAssetEditor) ? EAssetEditorToolkitTabLocation::Docked : EAssetEditorToolkitTabLocation::Standalone;
			for(const UObject* ObjectBeingEdited : *ObjectsBeingEdited)
			{
				// Only record assets that have a valid saved package
				UPackage* const Package = ObjectBeingEdited->GetOutermost();
				if(Package && Package->GetFileSize())
				{
					GConfig->SetInt(
						TEXT("AssetEditorToolkitTabLocation"), 
						*ObjectBeingEdited->GetPathName(), 
						static_cast<int32>(AssetEditorToolkitTabLocation), 
						GEditorPerProjectIni
						);
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
