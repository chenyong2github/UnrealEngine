// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelEditorMenu.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "Interfaces/IMainFrameModule.h"
#include "MRUFavoritesList.h"
#include "Framework/Commands/GenericCommands.h"
#include "IDocumentation.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "LevelEditorMenu"

void FLevelEditorMenu::RegisterLevelEditorMenus()
{
	struct Local
	{
		static void RegisterFileLoadAndSaveItems()
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.File");
			FToolMenuSection& Section = Menu->FindOrAddSection("FileLoadAndSave");
			FToolMenuInsert InsertPos(NAME_None, EToolMenuInsertType::First);

			// New Level
			Section.AddMenuEntry( FLevelEditorCommands::Get().NewLevel ).InsertPosition = InsertPos;

			// Open Level
			Section.AddMenuEntry( FLevelEditorCommands::Get().OpenLevel ).InsertPosition = InsertPos;

			// Open Asset
			//@TODO: Doesn't work when summoned from here: Section.AddMenuEntry( FGlobalEditorCommonCommands::Get().SummonOpenAssetDialog );

			// Save
			Section.AddMenuEntry( FLevelEditorCommands::Get().Save ).InsertPosition = InsertPos;
	
			// Save As
			Section.AddMenuEntry( FLevelEditorCommands::Get().SaveAs ).InsertPosition = InsertPos;

			// Save Levels
			Section.AddMenuEntry( FLevelEditorCommands::Get().SaveAllLevels ).InsertPosition = InsertPos;
		}

		static void FillFileRecentAndFavoriteFileItems()
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.File");
			FToolMenuInsert SectionInsertPos("FileRecentFiles", EToolMenuInsertType::Before);

			// Import/Export
			{
				FToolMenuSection& Section = Menu->AddSection("FileActors", LOCTEXT("ImportExportHeading", "Actors"), SectionInsertPos);
				{
					// Import Into Level
					Section.AddMenuEntry(FLevelEditorCommands::Get().ImportScene);

					// Export All
					Section.AddMenuEntry( FLevelEditorCommands::Get().ExportAll );

					// Export Selected
					Section.AddMenuEntry( FLevelEditorCommands::Get().ExportSelected );
				}
			}


			// Favorite Menus
			{
				struct FFavoriteLevelMenu
				{
					// Add a button to add/remove the currently loaded map as a favorite
					struct Local
					{
						static FText GetToggleFavoriteLabelText()
						{
							const FText LevelName = FText::FromString(FPackageName::GetShortName(GWorld->GetOutermost()->GetFName()));
							if (!FLevelEditorActionCallbacks::ToggleFavorite_IsChecked())
							{
								return FText::Format(LOCTEXT("ToggleFavorite_Add", "Add {0} to Favorites"), LevelName);
							}
							return FText::Format(LOCTEXT("ToggleFavorite_Remove", "Remove {0} from Favorites"), LevelName);
						}
					};

					static void MakeFavoriteLevelMenu(UToolMenu* InMenu)
					{
						// Add a button to add/remove the currently loaded map as a favorite
						if (FLevelEditorActionCallbacks::ToggleFavorite_CanExecute())
						{
							FToolMenuSection& Section = InMenu->AddSection("LevelEditorToggleFavorite");
							{
								TAttribute<FText> ToggleFavoriteLabel;
								ToggleFavoriteLabel.BindStatic(&Local::GetToggleFavoriteLabelText);
								Section.AddMenuEntry(FLevelEditorCommands::Get().ToggleFavorite, ToggleFavoriteLabel);
							}
							Section.AddMenuSeparator("LevelEditorToggleFavorite");
						}
						const FMainMRUFavoritesList& MRUFavorites = *FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame").GetMRUFavoritesList();
						const int32 NumFavorites = MRUFavorites.GetNumFavorites();
						
						const bool bNoIndent = false;
						const int32 AllowedFavorites = FMath::Min(NumFavorites, FLevelEditorCommands::Get().OpenFavoriteFileCommands.Num());
						for (int32 CurFavoriteIndex = 0; CurFavoriteIndex < AllowedFavorites; ++CurFavoriteIndex)
						{
							TSharedPtr< FUICommandInfo > OpenFavoriteFile = FLevelEditorCommands::Get().OpenFavoriteFileCommands[CurFavoriteIndex];
							const FString CurFavorite = FPaths::GetBaseFilename(MRUFavorites.GetFavoritesItem(CurFavoriteIndex));
							const FText ToolTip = FText::Format(LOCTEXT("FavoriteLevelToolTip", "Opens favorite level: {0}"), FText::FromString(CurFavorite));
							const FText Label = FText::FromString(FPaths::GetBaseFilename(CurFavorite));

							InMenu->FindOrAddSection("Favorite").AddMenuEntry(OpenFavoriteFile, Label, ToolTip).Name = NAME_None;
						}
					}
				};

				FToolMenuSection& Section = Menu->AddSection("FileFavoriteLevels", TAttribute<FText>(), SectionInsertPos);

				Section.AddDynamicEntry("FileFavoriteLevels", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
				{
					IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
					const FMainMRUFavoritesList& RecentsAndFavorites = *MainFrameModule.GetMRUFavoritesList();
					if (RecentsAndFavorites.GetNumItems() > 0)
					{
						InSection.AddSubMenu(
							"FavoriteLevelsSubMenu",
							LOCTEXT("FavoriteLevelsSubMenu", "Favorite Levels"),
							LOCTEXT("RecentLevelsSubMenu_ToolTip", "Select a level to load"),
							FNewToolMenuDelegate::CreateStatic(&FFavoriteLevelMenu::MakeFavoriteLevelMenu),
							false,
							FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.FavoriteLevels")
						);
					}
				}));
			}

			// Recent files
			{
				struct FRecentLevelMenu
				{
					static void MakeRecentLevelMenu( UToolMenu* InMenu )
					{
						const FMainMRUFavoritesList& MRUFavorites = *FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" ).GetMRUFavoritesList();
						const int32 NumRecents = MRUFavorites.GetNumItems();

						const int32 AllowedRecents = FMath::Min( NumRecents, FLevelEditorCommands::Get().OpenRecentFileCommands.Num() );
						for ( int32 CurRecentIndex = 0; CurRecentIndex < AllowedRecents; ++CurRecentIndex )
						{
							TSharedPtr< FUICommandInfo > OpenRecentFile = FLevelEditorCommands::Get().OpenRecentFileCommands[ CurRecentIndex ];

							const FString CurRecent = MRUFavorites.GetMRUItem( CurRecentIndex );

							const FText ToolTip = FText::Format( LOCTEXT( "RecentLevelToolTip", "Opens recent level: {0}" ), FText::FromString( CurRecent ) );
							const FText Label = FText::FromString( FPaths::GetBaseFilename( CurRecent ) );

							InMenu->FindOrAddSection("Recent").AddMenuEntry( OpenRecentFile, Label, ToolTip ).Name = NAME_None;
						}
					}
				};

				FToolMenuSection& Section = Menu->AddSection("FileRecentLevels", TAttribute<FText>(), SectionInsertPos);
				Section.AddDynamicEntry("FileRecentLevels", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
				{
					IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
					const FMainMRUFavoritesList& RecentsAndFavorites = *MainFrameModule.GetMRUFavoritesList();
					if (RecentsAndFavorites.GetNumItems() > 0)
					{
						InSection.AddSubMenu(
							"RecentLevelsSubMenu",
							LOCTEXT("RecentLevelsSubMenu", "Recent Levels"),
							LOCTEXT("RecentLevelsSubMenu_ToolTip", "Select a level to load"),
							FNewToolMenuDelegate::CreateStatic(&FRecentLevelMenu::MakeRecentLevelMenu),
							false,
							FSlateIcon(FEditorStyle::GetStyleSetName(), "MainFrame.RecentLevels")
						);
					}
				}));
			}
		}

		static void ExtendEditMenu()
		{
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.MainMenu.Edit", "MainFrame.MainMenu.Edit");
			// Edit Actor
			{
				FToolMenuSection& Section = Menu->AddSection("EditMain", LOCTEXT("MainHeading", "Edit"), FToolMenuInsert("EditHistory", EToolMenuInsertType::After));
				{		
					Section.AddMenuEntry( FGenericCommands::Get().Cut );
					Section.AddMenuEntry( FGenericCommands::Get().Copy );
					Section.AddMenuEntry( FGenericCommands::Get().Paste );

					Section.AddMenuEntry( FGenericCommands::Get().Duplicate );
					Section.AddMenuEntry( FGenericCommands::Get().Delete );
				}
			}
		}

		static void ExtendHelpMenu()
		{
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("LevelEditor.MainMenu.Help", "MainFrame.MainMenu.Help");
			FToolMenuSection& Section = Menu->AddSection("HelpBrowse", NSLOCTEXT("MainHelpMenu", "Browse", "Browse"), FToolMenuInsert("BugReporting", EToolMenuInsertType::Before));
			{
				Section.AddMenuEntry( FLevelEditorCommands::Get().BrowseDocumentation );

				Section.AddMenuEntry( FLevelEditorCommands::Get().BrowseAPIReference );

				Section.AddMenuEntry( FLevelEditorCommands::Get().BrowseCVars );

				Section.AddMenuSeparator( "HelpBrowse" );

				Section.AddMenuEntry( FLevelEditorCommands::Get().BrowseViewportControls );
			}
		}
	};

	UToolMenus* ToolMenus = UToolMenus::Get();
	ToolMenus->RegisterMenu("LevelEditor.MainMenu", "MainFrame.MainMenu", EMultiBoxType::MenuBar);
	ToolMenus->RegisterMenu("LevelEditor.MainMenu.File", "MainFrame.MainTabMenu.File");
	ToolMenus->RegisterMenu("LevelEditor.MainMenu.Window", "MainFrame.MainMenu.Window");

	// Add level loading and saving menu items
	Local::RegisterFileLoadAndSaveItems();

	// Add recent / favorites
	Local::FillFileRecentAndFavoriteFileItems();

	// Extend the Edit menu
	Local::ExtendEditMenu();

	// Extend the Help menu
	Local::ExtendHelpMenu();
}

TSharedRef< SWidget > FLevelEditorMenu::MakeLevelEditorMenu( const TSharedPtr<FUICommandList>& CommandList, TSharedPtr<class SLevelEditor> LevelEditor )
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FExtender> Extenders = LevelEditorModule.GetMenuExtensibilityManager()->GetAllExtenders();
	FToolMenuContext ToolMenuContext(CommandList, Extenders.ToSharedRef());

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
	TSharedRef< SWidget > MenuBarWidget = MainFrameModule.MakeMainTabMenu( LevelEditor->GetTabManager(), "LevelEditor.MainMenu", ToolMenuContext );

	return MenuBarWidget;
}

TSharedRef< SWidget > FLevelEditorMenu::MakeNotificationBar( const TSharedPtr<FUICommandList>& CommandList, TSharedPtr<class SLevelEditor> LevelEditor )
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( "LevelEditor");
	const TSharedPtr<FExtender> NotificationBarExtenders = LevelEditorModule.GetNotificationBarExtensibilityManager()->GetAllExtenders();

	FToolBarBuilder NotificationBarBuilder( CommandList, FMultiBoxCustomization::None, NotificationBarExtenders, Orient_Horizontal );
	NotificationBarBuilder.SetStyle(&FEditorStyle::Get(), "NotificationBar");
	{
		NotificationBarBuilder.BeginSection("Start");
		NotificationBarBuilder.EndSection();
	}

	return NotificationBarBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE