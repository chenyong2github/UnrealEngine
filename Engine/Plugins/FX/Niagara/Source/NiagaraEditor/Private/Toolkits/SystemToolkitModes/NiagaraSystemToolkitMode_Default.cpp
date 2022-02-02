// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemToolkitMode_Default.h"

#include "NiagaraEditorCommands.h"
#include "NiagaraSystemToolkit.h"
#include "NiagaraDebuggerCommon.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorSettings.h"

#define LOCTEXT_NAMESPACE "NiagaraSystemToolkitMode_Default"

FNiagaraSystemToolkitMode_Default::FNiagaraSystemToolkitMode_Default(TWeakPtr<FNiagaraSystemToolkit> InSystemToolkit) : FNiagaraSystemToolkitModeBase(FNiagaraSystemToolkit::DefaultModeName, InSystemToolkit)
{
	TabLayout = FTabManager::NewLayout("Standalone_Niagara_System_Layout_v24")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					// Top Level Left
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(.75f)
					->Split
					(
						// Inner Left Top
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
						->SetSizeCoefficient(0.75f)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(.25f)
							->AddTab(ViewportTabID, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.15f)
							->AddTab(SystemParametersTabID, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.6f)
							->AddTab(SystemOverviewTabID, ETabState::OpenedTab)
							->AddTab(ScratchPadTabID, ETabState::OpenedTab)
							->AddTab(BakerTabID, ETabState::ClosedTab)
							->SetForegroundTab(SystemOverviewTabID)
						)
					)
					->Split
					(
						// Inner Left Bottom
						FTabManager::NewStack()
						->SetSizeCoefficient(0.25f)
						->AddTab(CurveEditorTabID, ETabState::OpenedTab)
						->AddTab(MessageLogTabID, ETabState::OpenedTab)
						->AddTab(SequencerTabID, ETabState::OpenedTab)
						->AddTab(ScriptStatsTabID, ETabState::ClosedTab)
					)
				)
				->Split
				(
					// Top Level Right
					FTabManager::NewStack()
					->SetSizeCoefficient(0.25f)
					->AddTab(SelectedEmitterStackTabID, ETabState::OpenedTab)
					->AddTab(SelectedEmitterGraphTabID, ETabState::ClosedTab)
					->AddTab(SystemScriptTabID, ETabState::ClosedTab)
					->AddTab(SystemDetailsTabID, ETabState::ClosedTab)
					->AddTab(DebugSpreadsheetTabID, ETabState::ClosedTab)
					->AddTab(PreviewSettingsTabId, ETabState::ClosedTab)
					->AddTab(GeneratedCodeTabID, ETabState::ClosedTab)
				)
			)
		);

	ExtendToolbar();
}

void FNiagaraSystemToolkitMode_Default::ExtendToolbar()
{
	struct Local
	{
		static TSharedRef<SWidget> FillSimulationOptionsMenu(FNiagaraSystemToolkit* Toolkit)
		{
			FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleAutoPlay);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleResetSimulationOnChange);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleResimulateOnChangeWhilePaused);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleResetDependentSystems);
			return MenuBuilder.MakeWidget();
		}

		static TSharedRef<SWidget> GenerateBakerMenu(FNiagaraSystemToolkit* Toolkit)
		{
			FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());

			MenuBuilder.AddMenuEntry(
				LOCTEXT("BakerTab", "Open Baker Tab"),
				LOCTEXT("BakerTabTooltip", "Opens the flip book tab."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([TabManager=Toolkit->GetTabManager()]() { TabManager->TryInvokeTab(FNiagaraSystemToolkitModeBase::BakerTabID); }))
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("RenderBaker", "Render Baker"),
				LOCTEXT("RenderBakerTooltip", "Renders the Baker using the current settings."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(Toolkit, &FNiagaraSystemToolkit::RenderBaker))
			);

			return MenuBuilder.MakeWidget();
		}

		static TSharedRef<SWidget> FillDebugOptionsMenu(FNiagaraSystemToolkit* Toolkit)
		{
			FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());

#if WITH_NIAGARA_DEBUGGER
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().OpenDebugHUD);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().OpenDebugOutliner);
#endif
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().OpenAttributeSpreadsheet);
			return MenuBuilder.MakeWidget();
		}

		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, FNiagaraSystemToolkit* Toolkit)
		{
			ToolbarBuilder.BeginSection("Apply");
			{
				if (Toolkit->HasEmitter())
				{
					ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().Apply,
						NAME_None, TAttribute<FText>(), TAttribute<FText>(),
						FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "NiagaraEditor.Apply"),
						FName(TEXT("ApplyNiagaraEmitter")));
				}
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().ApplyScratchPadChanges,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "NiagaraEditor.ApplyScratchPadChanges"),
					FName(TEXT("ApplyScratchPadChanges")));
			}
			ToolbarBuilder.EndSection();
			ToolbarBuilder.BeginSection("Compile");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().Compile,
					NAME_None,
					TAttribute<FText>(),
					TAttribute<FText>(Toolkit, &FNiagaraSystemToolkit::GetCompileStatusTooltip),
					TAttribute<FSlateIcon>(Toolkit, &FNiagaraSystemToolkit::GetCompileStatusImage),
					FName(TEXT("CompileNiagaraSystem")));
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateRaw(Toolkit, &FNiagaraSystemToolkit::GenerateCompileMenuContent),
					LOCTEXT("BuildCombo_Label", "Auto-Compile Options"),
					LOCTEXT("BuildComboToolTip", "Auto-Compile options menu"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Build"),
					true);
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("NiagaraThumbnail");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().SaveThumbnailImage, NAME_None,
					LOCTEXT("GenerateThumbnail", "Thumbnail"),
					LOCTEXT("GenerateThumbnailTooltip","Generate a thumbnail image."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.SaveThumbnailImage"));
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("NiagaraPreviewOptions");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().ToggleBounds, NAME_None,
					LOCTEXT("ShowBounds", "Bounds"),
					LOCTEXT("ShowBoundsTooltip", "Show the bounds for the scene."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.ToggleBounds"));
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateRaw(Toolkit, &FNiagaraSystemToolkit::GenerateBoundsMenuContent, Toolkit->GetToolkitCommands()),
					LOCTEXT("BoundsMenuCombo_Label", "Bounds Options"),
					LOCTEXT("BoundsMenuCombo_ToolTip", "Bounds options"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.ToggleBounds"),
					true
				);
			}
			ToolbarBuilder.EndSection();
			
#if STATS
			ToolbarBuilder.BeginSection("NiagaraStatisticsOptions");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().ToggleStatPerformance, NAME_None,
                    LOCTEXT("NiagaraShowPerformance", "Performance"),
                    LOCTEXT("NiagaraShowPerformanceTooltip", "Show runtime performance for particle scripts."),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.ToggleMaterialStats"));
				ToolbarBuilder.AddComboButton(
                    FUIAction(),
                    FOnGetContent::CreateRaw(Toolkit, &FNiagaraSystemToolkit::GenerateStatConfigMenuContent, Toolkit->GetToolkitCommands()),
                    FText(),
                    LOCTEXT("NiagaraShowPerformanceCombo_ToolTip", "Runtime performance options"),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.ToggleMaterialStats"),
                    true);
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateStatic(Local::FillDebugOptionsMenu, Toolkit),
					LOCTEXT("DebugOptions", "Debug"),
					LOCTEXT("DebugOptionsTooltip", "Debug options"),
					FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "Tab.Debugger")
				);
			}
			ToolbarBuilder.EndSection();
#endif
			
			ToolbarBuilder.BeginSection("PlaybackOptions");
			{
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateStatic(Local::FillSimulationOptionsMenu, Toolkit),
					LOCTEXT("SimulationOptions", "Simulation"),
					LOCTEXT("SimulationOptionsTooltip", "Simulation options"),
					FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "NiagaraEditor.SimulationOptions")
				);
			}
			ToolbarBuilder.EndSection();

			if ( GetDefault<UNiagaraEditorSettings>()->bEnableBaker )
			{
				ToolbarBuilder.BeginSection("Baker");
				{
					ToolbarBuilder.AddComboButton(
						FUIAction(),
						FOnGetContent::CreateStatic(Local::GenerateBakerMenu, Toolkit),
						LOCTEXT("Baker", "Baker"),
						LOCTEXT("BakerTooltip", "Options for Baker rendering."),
						FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "NiagaraEditor.Baker")
					);
				}
				ToolbarBuilder.EndSection();
			}

			if(GNiagaraScalabilityModeEnabled)
			{
				ToolbarBuilder.BeginSection("Scalability");
				{
					FUIAction ScalabilityToggleAction(FExecuteAction::CreateRaw(Toolkit, &FNiagaraSystemToolkit::SetCurrentMode, FNiagaraSystemToolkit::ScalabilityModeName));
					ScalabilityToggleAction.GetActionCheckState = FGetActionCheckState::CreateLambda([Toolkit]()
					{
						return Toolkit->GetCurrentMode() == FNiagaraSystemToolkit::ScalabilityModeName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					});
					
					ToolbarBuilder.AddToolBarButton(
						ScalabilityToggleAction,
						NAME_None, 
						LOCTEXT("ScalabilityLabel", "Scalability"),
						LOCTEXT("ScalabilityTooltip", "Turn on scalability mode to optimize your effects for various platforms and quality settings."),
						FSlateIcon(FNiagaraEditorStyle::Get().GetStyleSetName(), "NiagaraEditor.SimulationOptions")
					);
				}
				ToolbarBuilder.EndSection();
			}
		}
	};

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		SystemToolkit.Pin()->GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, SystemToolkit.Pin().Get())
		);

	//SystemToolkit.Pin()->AddToolbarExtender(ToolbarExtender);
	
	// FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	// SystemToolkit.Pin()->AddToolbarExtender(NiagaraEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(SystemToolkit.Pin()->GetToolkitCommands(), SystemToolkit.Pin()->GetObjectsBeingEdited()));
}

void FNiagaraSystemToolkitMode_Default::RegisterTabFactories(TSharedPtr<FTabManager> InTabManager)
{
	// this will register the common tab factories shared across modes and will call ExtendToolbar
	FNiagaraSystemToolkitModeBase::RegisterTabFactories(InTabManager);

	// add additional tab factories here that are exclusive to this mode
}

#undef LOCTEXT_NAMESPACE
