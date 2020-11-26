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
#include "LevelEditorMenuContext.h"
#include "Widgets/Input/SSpinBox.h"

#include "Widgets/Input/SCheckBox.h"

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
							Section.AddSeparator("LevelEditorToggleFavorite");
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
			{
				// Edit Actor
				{
					FToolMenuSection& Section = Menu->AddSection("EditMain", LOCTEXT("MainHeading", "Edit"), FToolMenuInsert("EditHistory", EToolMenuInsertType::After));

					Section.AddMenuEntry(FGenericCommands::Get().Cut);
					Section.AddMenuEntry(FGenericCommands::Get().Copy);
					Section.AddMenuEntry(FGenericCommands::Get().Paste);

					Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
					Section.AddMenuEntry(FGenericCommands::Get().Delete);
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

				Section.AddSeparator( "HelpBrowse" );

				Section.AddMenuEntry( FLevelEditorCommands::Get().BrowseViewportControls );
			}
		}

		static void ExtendMenuBar()
		{
			UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");

			FToolMenuSection& Section = Menu->FindOrAddSection(NAME_None);

			FToolMenuEntry& Entry =
				Section.AddSubMenu(
					"Build",
					LOCTEXT("BuildMenu", "Build"),
					LOCTEXT("BuildMenu_ToolTip", "Level Build Options"),
					FNewToolMenuChoice()
				);

			Entry.InsertPosition = FToolMenuInsert("Help", EToolMenuInsertType::Before);
		}
	};

	UToolMenus* ToolMenus = UToolMenus::Get();
	ToolMenus->RegisterMenu("LevelEditor.MainMenu", "MainFrame.MainMenu", EMultiBoxType::MenuBar);
	ToolMenus->RegisterMenu("LevelEditor.MainMenu.File", "MainFrame.MainTabMenu.File");
	ToolMenus->RegisterMenu("LevelEditor.MainMenu.Window", "MainFrame.MainMenu.Window");

	Local::ExtendMenuBar();

	// Add level loading and saving menu items
	Local::RegisterFileLoadAndSaveItems();

	// Add recent / favorites
	Local::FillFileRecentAndFavoriteFileItems();

	// Extend the Edit menu
	Local::ExtendEditMenu();

	// Extend the Help menu
	Local::ExtendHelpMenu();

	RegisterBuildMenu();

}

TSharedRef< SWidget > FLevelEditorMenu::MakeLevelEditorMenu( const TSharedPtr<FUICommandList>& CommandList, TSharedPtr<class SLevelEditor> LevelEditor )
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FExtender> Extenders = LevelEditorModule.GetMenuExtensibilityManager()->GetAllExtenders();
	FToolMenuContext ToolMenuContext(CommandList, Extenders.ToSharedRef());

	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
	TSharedRef< SWidget > MenuBarWidget = MainFrameModule.MakeMainMenu( LevelEditor->GetTabManager(), "LevelEditor.MainMenu", ToolMenuContext );

	return MenuBarWidget;
}

void FLevelEditorMenu::RegisterBuildMenu()
{
	static const FName BaseMenuName = "LevelEditor.MainMenu.Build";
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(BaseMenuName);

	struct FLightingMenus
	{
	public:

		static void RegisterMenus(const FName InBaseMenuName)
		{
			FLightingMenus::RegisterLightingQualityMenu(InBaseMenuName);
			FLightingMenus::RegisterLightingInfoMenu(InBaseMenuName);
		}

	private:

		/** Generates a lighting quality sub-menu */
		static void RegisterLightingQualityMenu(const FName InBaseMenuName)
		{
			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingQuality"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingQuality", LOCTEXT("LightingQualityHeading", "Quality Level"));
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingQuality_Production);
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingQuality_High);
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingQuality_Medium);
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingQuality_Preview);
			}
		}

		/** Generates a lighting density sub-menu */
		static void RegisterLightingDensityMenu(const FName InBaseMenuName)
		{
			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingDensity"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingDensity", LOCTEXT("LightingDensityHeading", "Density Rendering"));
				TSharedRef<SWidget> Ideal = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(FMargin(27.0f, 0.0f, 0.0f, 0.0f))
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.f)
						.MaxValue(100.f)
						.Value(FLevelEditorActionCallbacks::GetLightingDensityIdeal())
						.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityIdeal)
					];

				Section.AddEntry(FToolMenuEntry::InitWidget("Ideal", Ideal, LOCTEXT("LightingDensity_Ideal", "Ideal Density")));

				TSharedRef<SWidget> Maximum = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.01f)
						.MaxValue(100.01f)
						.Value(FLevelEditorActionCallbacks::GetLightingDensityMaximum())
						.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityMaximum)
					];

				Section.AddEntry(FToolMenuEntry::InitWidget("Maximum", Maximum, LOCTEXT("LightingDensity_Maximum", "Maximum Density")));

				TSharedRef<SWidget> ClrScale = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(FMargin(35.0f, 0.0f, 0.0f, 0.0f))
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.f)
						.MaxValue(10.f)
						.Value(FLevelEditorActionCallbacks::GetLightingDensityColorScale())
						.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityColorScale)
					];

				Section.AddEntry(FToolMenuEntry::InitWidget("ColorScale", ClrScale, LOCTEXT("LightingDensity_ColorScale", "Color Scale")));

				TSharedRef<SWidget> GrayScale = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.Padding(FMargin(11.0f, 0.0f, 0.0f, 0.0f))
					.FillWidth(1.0f)
					[
						SNew(SSpinBox<float>)
						.MinValue(0.f)
						.MaxValue(10.f)
						.Value(FLevelEditorActionCallbacks::GetLightingDensityGrayscaleScale())
						.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingDensityGrayscaleScale)
					];

				Section.AddEntry(FToolMenuEntry::InitWidget("GrayscaleScale", GrayScale, LOCTEXT("LightingDensity_GrayscaleScale", "Grayscale Scale")));

				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingDensity_RenderGrayscale);
			}
		}

		/** Generates a lighting resolution sub-menu */
		static void RegisterLightingResolutionMenu(const FName InBaseMenuName)
		{
			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingResolution"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingResolution1", LOCTEXT("LightingResolutionHeading1", "Primitive Types"));
				TSharedRef<SWidget> Meshes = SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FEditorStyle::Get(), "Menu.CheckBox")
						.ToolTipText(LOCTEXT("StaticMeshesToolTip", "Static Meshes will be adjusted if checked."))
						.IsChecked_Static(&FLevelEditorActionCallbacks::IsLightingResolutionStaticMeshesChecked)
						.OnCheckStateChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionStaticMeshes)
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StaticMeshes", "Static Meshes"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(4.0f, 0.0f, 11.0f, 0.0f))
					[
						SNew(SSpinBox<float>)
						.MinValue(4.f)
						.MaxValue(4096.f)
						.ToolTipText(LOCTEXT("LightingResolutionStaticMeshesMinToolTip", "The minimum lightmap resolution for static mesh adjustments. Anything outside of Min/Max range will not be touched when adjusting."))
						.Value(FLevelEditorActionCallbacks::GetLightingResolutionMinSMs())
						.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMinSMs)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSpinBox<float>)
						.MinValue(4.f)
					.MaxValue(4096.f)
					.ToolTipText(LOCTEXT("LightingResolutionStaticMeshesMaxToolTip", "The maximum lightmap resolution for static mesh adjustments. Anything outside of Min/Max range will not be touched when adjusting."))
					.Value(FLevelEditorActionCallbacks::GetLightingResolutionMaxSMs())
					.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMaxSMs)
					];
				Section.AddEntry(FToolMenuEntry::InitWidget("Meshes", Meshes, FText::GetEmpty(), true));

				TSharedRef<SWidget> BSPs = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.Style(FEditorStyle::Get(), "Menu.CheckBox")
					.ToolTipText(LOCTEXT("BSPSurfacesToolTip", "BSP Surfaces will be adjusted if checked."))
					.IsChecked_Static(&FLevelEditorActionCallbacks::IsLightingResolutionBSPSurfacesChecked)
					.OnCheckStateChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionBSPSurfaces)
					.Content()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BSPSurfaces", "BSP Surfaces"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(6.0f, 0.0f, 4.0f, 0.0f))
				[
					SNew(SSpinBox<float>)
					.MinValue(1.f)
					.MaxValue(63556.f)
					.ToolTipText(LOCTEXT("LightingResolutionBSPsMinToolTip", "The minimum lightmap resolution of a BSP surface to adjust. When outside of the Min/Max range, the BSP surface will no be altered."))
					.Value(FLevelEditorActionCallbacks::GetLightingResolutionMinBSPs())
					.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMinBSPs)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SSpinBox<float>)
					.MinValue(1.f)
					.MaxValue(63556.f)
					.ToolTipText(LOCTEXT("LightingResolutionBSPsMaxToolTip", "The maximum lightmap resolution of a BSP surface to adjust. When outside of the Min/Max range, the BSP surface will no be altered."))
					.Value(FLevelEditorActionCallbacks::GetLightingResolutionMaxBSPs())
					.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetLightingResolutionMaxBSPs)
				];
				Section.AddEntry(FToolMenuEntry::InitWidget("BSPs", BSPs, FText::GetEmpty(), true));
			}

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingResolution2", LOCTEXT("LightingResolutionHeading2", "Select Options"));
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingResolution_CurrentLevel);
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingResolution_SelectedLevels);
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingResolution_AllLoadedLevels);
				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingResolution_SelectedObjectsOnly);
			}

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingResolution3", LOCTEXT("LightingResolutionHeading3", "Ratio"));
				TSharedRef<SWidget> Ratio = SNew(SSpinBox<int32>)
					.MinValue(0)
					.MaxValue(400)
					.ToolTipText(LOCTEXT("LightingResolutionRatioToolTip", "Ratio to apply (New Resolution = Ratio / 100.0f * CurrentResolution)."))
					.Value(FLevelEditorActionCallbacks::GetLightingResolutionRatio())
					.OnEndSliderMovement_Static(&FLevelEditorActionCallbacks::SetLightingResolutionRatio)
					.OnValueCommitted_Static(&FLevelEditorActionCallbacks::SetLightingResolutionRatioCommit);
				Section.AddEntry(FToolMenuEntry::InitWidget("Ratio", Ratio, LOCTEXT("LightingResolutionRatio", "Ratio")));
			}
		}

		/** Generates a lighting info dialogs sub-menu */
		static void RegisterLightingInfoMenu(const FName InBaseMenuName)
		{
			RegisterLightingDensityMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingInfo"));
			RegisterLightingResolutionMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingInfo"));

			UToolMenu* SubMenu = UToolMenus::Get()->RegisterMenu(UToolMenus::JoinMenuPaths(InBaseMenuName, "LightingInfo"));

			{
				FToolMenuSection& Section = SubMenu->AddSection("LevelEditorBuildLightingInfo", LOCTEXT("LightingInfoHeading", "Lighting Info Dialogs"));
				Section.AddSubMenu(
					"LightingDensity",
					LOCTEXT("LightingDensityRenderingSubMenu", "LightMap Density Rendering Options"),
					LOCTEXT("LightingDensityRenderingSubMenu_ToolTip", "Shows the LightMap Density Rendering viewmode options."),
					FNewToolMenuChoice());

				Section.AddSubMenu(
					"LightingResolution",
					LOCTEXT("LightingResolutionAdjustmentSubMenu", "LightMap Resolution Adjustment"),
					LOCTEXT("LightingResolutionAdjustmentSubMenu_ToolTip", "Shows the LightMap Resolution Adjustment options."),
					FNewToolMenuChoice());

				Section.AddMenuEntry(FLevelEditorCommands::Get().LightingStaticMeshInfo, LOCTEXT("BuildLightingInfo_LightingStaticMeshInfo", "Lighting StaticMesh Info..."));
			}
		}
	};

	{
		FToolMenuSection& Section = Menu->AddSection("Level", LOCTEXT("LevelHeading", "Level"));

		Section.AddMenuEntry(FLevelEditorCommands::Get().Build, LOCTEXT("Build", "Build All Levels"));
	}

	FLightingMenus::RegisterMenus(BaseMenuName);

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLighting", LOCTEXT("LightingHeading", "Lighting"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildLightingOnly, LOCTEXT("BuildLightingOnlyHeading", "Build Lighting Only"));

		Section.AddSubMenu(
			"LightingQuality",
			LOCTEXT("LightingQualitySubMenu", "Lighting Quality"),
			LOCTEXT("LightingQualitySubMenu_ToolTip", "Allows you to select the quality level for precomputed lighting"),
			FNewToolMenuChoice());

		Section.AddSubMenu(
			"LightingInfo",
			LOCTEXT("BuildLightingInfoSubMenu", "Lighting Info"),
			LOCTEXT("BuildLightingInfoSubMenu_ToolTip", "Access the lighting info dialogs"),
			FNewToolMenuChoice());

		Section.AddMenuEntry(FLevelEditorCommands::Get().LightingBuildOptions_UseErrorColoring);
		Section.AddMenuEntry(FLevelEditorCommands::Get().LightingBuildOptions_ShowLightingStats);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorReflections", LOCTEXT("ReflectionHeading", "Reflections"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildReflectionCapturesOnly);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorVisibility", LOCTEXT("VisibilityHeading", "Visibility"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildLightingOnly_VisibilityOnly);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorGeometry", LOCTEXT("GeometryHeading", "Geometry"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildGeometryOnly);
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildGeometryOnly_OnlyCurrentLevel);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorNavigation", LOCTEXT("NavigationHeading", "Navigation"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildPathsOnly);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLOD", LOCTEXT("LODHeading", "Hierarchical LOD"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildLODsOnly);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorTextureStreaming", LOCTEXT("TextureStreamingHeading", "Texture Streaming"));
		Section.AddDynamicEntry("BuildTextureStreamingOnly", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				if (CVarStreamingUseNewMetrics.GetValueOnAnyThread() != 0) // There is no point of in building texture streaming data with the old system.
				{
					InSection.AddMenuEntry(FLevelEditorCommands::Get().BuildTextureStreamingOnly);
				}
			}));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildVirtualTextureOnly);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorLandscape", LOCTEXT("LandscapeHeading", "Landscape"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().BuildAllLandscape);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorAutomation", LOCTEXT("AutomationHeading", "Automation"));
		Section.AddMenuEntry(
			FLevelEditorCommands::Get().BuildAndSubmitToSourceControl,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.BuildAndSubmit")
		);
	}

	// Map Check
	{
		FToolMenuSection& Section = Menu->AddSection("LevelEditorVerification", LOCTEXT("VerificationHeading", "Verification"));
		Section.AddMenuEntry(FLevelEditorCommands::Get().MapCheck, LOCTEXT("OpenMapCheck", "Map Check"));
	}

}
#undef LOCTEXT_NAMESPACE