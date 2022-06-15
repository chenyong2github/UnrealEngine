// Copyright Epic Games, Inc. All Rights Reserved.
#include "WaveTableBankEditor.h"

#include "CommonFrameRates.h"
#include "Containers/Set.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "CurveEditor.h"
#include "CurveEditorCommands.h"
#include "Styling/AppStyle.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RichCurveEditorModel.h"
#include "SCurveEditorPanel.h"
#include "Tree/SCurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/SCurveEditorTreePin.h"
#include "WaveTableSettings.h"
#include "WaveTableSampler.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SFrameRatePicker.h"


#define LOCTEXT_NAMESPACE "WaveTableEditor"



namespace WaveTable
{
	namespace Editor
	{
		const FName FWaveTableBankEditor::AppIdentifier(TEXT("WaveTableEditorApp"));
		const FName FWaveTableBankEditor::CurveTabId(TEXT("WaveTableEditor_Curves"));
		const FName FWaveTableBankEditor::PropertiesTabId(TEXT("WaveTableEditor_Properties"));

		FWaveTableBankEditor::FWaveTableBankEditor()
		{
		}

		void FWaveTableBankEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
		{
			WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_WaveTableEditor", "WaveTable Editor"));

			FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

			InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FWaveTableBankEditor::SpawnTab_Properties))
				.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
				.SetGroup(WorkspaceMenuCategory.ToSharedRef())
				.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

			FSlateIcon CurveIcon(FAppStyle::GetAppStyleSetName(), "WaveTableEditor.Tabs.Properties");
			InTabManager->RegisterTabSpawner(CurveTabId, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args) { return SpawnTab_OutputCurve(Args); }))
				.SetDisplayName(LOCTEXT("TransformCurvesTab", "Transform Curves"))
				.SetGroup(WorkspaceMenuCategory.ToSharedRef())
				.SetIcon(CurveIcon);
		}

		void FWaveTableBankEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
		{
			InTabManager->UnregisterTabSpawner(PropertiesTabId);
			InTabManager->UnregisterTabSpawner(CurveTabId);
		}

		void FWaveTableBankEditor::Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* InParentObject)
		{
			check(InParentObject);

			CurveEditor = MakeShared<FCurveEditor>();
			FCurveEditorInitParams InitParams;
			CurveEditor->InitCurveEditor(InitParams);
			CurveEditor->GridLineLabelFormatXAttribute = LOCTEXT("GridXLabelFormat", "{0}");

			TUniquePtr<ICurveEditorBounds> EditorBounds = MakeUnique<FStaticCurveEditorBounds>();
			EditorBounds->SetInputBounds(0.05, 1.05);
			CurveEditor->SetBounds(MoveTemp(EditorBounds));

			CurvePanel = SNew(SCurveEditorPanel, CurveEditor.ToSharedRef());

			// Support undo/redo
			InParentObject->SetFlags(RF_Transactional);
			GEditor->RegisterForUndo(this);

			FDetailsViewArgs Args;
			Args.bHideSelectionTip = true;
			Args.NotifyHook = this;

			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertiesView = PropertyModule.CreateDetailView(Args);
			PropertiesView->SetObject(InParentObject);

			TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_WaveTableEditor_Layout_v1")
				->AddArea
				(
					FTabManager::NewPrimaryArea()
					->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewSplitter()
						->SetSizeCoefficient(0.9f)
						->SetOrientation(Orient_Horizontal)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.225f)
							->AddTab(PropertiesTabId, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewSplitter()
							->SetSizeCoefficient(0.775f)
							->SetOrientation(Orient_Vertical)
							->Split
							(
								FTabManager::NewStack()
								->SetHideTabWell(true)
								->SetSizeCoefficient(0.33f)
								->AddTab(CurveTabId, ETabState::OpenedTab)
							)
						)
					)
				);

			const bool bCreateDefaultStandaloneMenu = true;
			const bool bCreateDefaultToolbar = true;
			const bool bToolbarFocusable = false;
			const bool bUseSmallIcons = true;
			FAssetEditorToolkit::InitAssetEditor(
				Mode,
				InitToolkitHost,
				AppIdentifier,
				StandaloneDefaultLayout,
				bCreateDefaultStandaloneMenu,
				bCreateDefaultToolbar,
				InParentObject,
				bToolbarFocusable,
				bUseSmallIcons);

		// 	Editor::FModule& WaveTableModule = FModuleManager::LoadModuleChecked<Editor::FModule>(TEXT("WaveTableEditor"));
		// 	AddMenuExtender(WaveTableModule.GetWaveTableMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
		// 	AddToolbarExtender(WaveTableModule.GetWaveTableToolbarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
			AddToolbarExtender(CurvePanel->GetToolbarExtender());

			if (CurveEditor.IsValid())
			{
				RegenerateMenusAndToolbars();
			}
		}

		void FWaveTableBankEditor::ClearExpressionCurve(int32 InCurveIndex)
		{
			if (CurveData.IsValidIndex(InCurveIndex))
			{
				CurveData[InCurveIndex].ExpressionCurve.Reset();
			}
		}

		bool FWaveTableBankEditor::RequiresNewCurve(int32 InCurveIndex, const FRichCurve& InRichCurve) const
		{
			const FCurveModelID CurveModelID = CurveData[InCurveIndex].ModelID;
			const TUniquePtr<FCurveModel>* CurveModel = CurveEditor->GetCurves().Find(CurveModelID);
			if (!CurveModel || !CurveModel->IsValid())
			{
				return true;
			}

			FWaveTableCurveModelBase* PatchCurveModel = static_cast<FWaveTableCurveModelBase*>(CurveModel->Get());
			check(PatchCurveModel);
			if (&PatchCurveModel->GetRichCurve() != &InRichCurve)
			{
				return true;
			}

			return false;
		}

		void FWaveTableBankEditor::SetCurve(int32 InCurveIndex, FRichCurve& InRichCurve, EWaveTableCurveSource InSource)
		{
			check(CurveEditor.IsValid());

			if (!ensure(CurveData.IsValidIndex(InCurveIndex)))
			{
				return;
			}

			FCurveData& CurveDataEntry = CurveData[InCurveIndex];

			const bool bRequiresNewCurve = RequiresNewCurve(InCurveIndex, InRichCurve);
			if (bRequiresNewCurve)
			{
				TUniquePtr<FWaveTableCurveModelBase> NewCurve = ConstructCurveModel(InRichCurve, GetEditingObject(), InSource);
				NewCurve->Refresh(InCurveIndex);
				CurveDataEntry.ModelID = CurveEditor->AddCurve(MoveTemp(NewCurve));
			}
			else
			{
				const TUniquePtr<FCurveModel>& CurveModel = CurveEditor->GetCurves().FindChecked(CurveDataEntry.ModelID);
				check(CurveModel.Get());
				static_cast<FWaveTableCurveModelBase*>(CurveModel.Get())->Refresh(InCurveIndex);
			}

			CurveEditor->PinCurve(CurveDataEntry.ModelID);
		}

		FName FWaveTableBankEditor::GetToolkitFName() const
		{
			return FName("WaveTableEditor");
		}

		FText FWaveTableBankEditor::GetBaseToolkitName() const
		{
			return LOCTEXT( "AppLabel", "WaveTable Editor" );
		}

		FString FWaveTableBankEditor::GetWorldCentricTabPrefix() const
		{
			return LOCTEXT("WorldCentricTabPrefix", "WaveTable ").ToString();
		}

		FLinearColor FWaveTableBankEditor::GetWorldCentricTabColorScale() const
		{
			return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
		}

		EOrientation FWaveTableBankEditor::GetSnapLabelOrientation() const
		{
			return FMultiBoxSettings::UseSmallToolBarIcons.Get()
				? EOrientation::Orient_Horizontal
				: EOrientation::Orient_Vertical;
		}

		void FWaveTableBankEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
		{
			if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
			{
				RefreshCurves();
			}
		}

		TSharedRef<SDockTab> FWaveTableBankEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
		{
			check(Args.GetTabId() == PropertiesTabId);

			return SNew(SDockTab)
				.Label(LOCTEXT("SoundWaveTableDetailsTitle", "Details"))
				[
					PropertiesView.ToSharedRef()
				];
		}

		TSharedRef<SDockTab> FWaveTableBankEditor::SpawnTab_OutputCurve(const FSpawnTabArgs& Args)
		{
			RefreshCurves();
			CurveEditor->ZoomToFit();

			TSharedRef<SDockTab> NewDockTab = SNew(SDockTab)
				.Label(FText::Format(LOCTEXT("WaveTableFilterTitle", "Filter Transform Curve: {0}"), FText::FromString(GetEditingObject()->GetName())))
				.TabColorScale(GetTabColorScale())
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(0.0f)
					[
						CurvePanel.ToSharedRef()
					]
				];

				return NewDockTab;
		}

		void FWaveTableBankEditor::PostUndo(bool bSuccess)
		{
			if (bSuccess)
			{
				RefreshCurves();
			}
		}

		void FWaveTableBankEditor::ResetCurves()
		{
			check(CurveEditor.IsValid());

			CurveEditor->RemoveAllCurves();
			CurveData.Reset();
			CurveData.AddDefaulted(GetNumCurves());
		}

		void FWaveTableBankEditor::InitCurves()
		{
			for (int32 i = 0; i < GetNumCurves(); ++i)
			{
				EWaveTableCurve CurveType = GetCurveType(i);
				switch (CurveType)
				{
					case EWaveTableCurve::Exp:
					case EWaveTableCurve::Exp_Inverse:
					case EWaveTableCurve::Linear:
					case EWaveTableCurve::Log:
					case EWaveTableCurve::SCurve:
					case EWaveTableCurve::Sin:
					case EWaveTableCurve::File:
					{
						if (RequiresNewCurve(i, *CurveData[i].ExpressionCurve.Get()))
						{
							ResetCurves();
						}
					}
					break;

					case EWaveTableCurve::Shared:
					{
						if (const UCurveFloat* SharedCurve = GetSharedCurve(i))
						{
							if (RequiresNewCurve(i, SharedCurve->FloatCurve))
							{
								ResetCurves();
							}
						}
						else if (RequiresNewCurve(i, *CurveData[i].ExpressionCurve.Get()))
						{
							ResetCurves();
						}
					}
					break;

					case EWaveTableCurve::Custom:
					{
						if (RequiresNewCurve(i, GetCustomCurveChecked(i)))
						{
							ResetCurves();
						}
					}
					break;

					default:
					{
						static_assert(static_cast<int32>(EWaveTableCurve::Count) == 9, "Possible missing case coverage for output curve.");
					}
					break;
				}
			}
		}

		void FWaveTableBankEditor::RefreshCurves()
		{
			check(CurveEditor.IsValid());

			for (const FCurveData& CurveDataEntry : CurveData)
			{
				CurveEditor->UnpinCurve(CurveDataEntry.ModelID);
			}

			if (GetIsPropertyEditorDisabled())
			{
				ResetCurves();
				return;
			}

			const int32 NumCurves = GetNumCurves();
			if (NumCurves == CurveData.Num())
			{
				InitCurves();
			}
			else
			{
				ResetCurves();
			}

			for (int32 i = 0; i < GetNumCurves(); ++i)
			{
				const EWaveTableCurve CurveType = GetCurveType(i);
				if (!CurveData.IsValidIndex(i))
				{
					continue;
				}

				FCurveData& CurveDataEntry = CurveData[i];

				switch (CurveType)
				{
					case EWaveTableCurve::Exp:
					case EWaveTableCurve::Exp_Inverse:
					case EWaveTableCurve::Linear:
					case EWaveTableCurve::Log:
					case EWaveTableCurve::SCurve:
					case EWaveTableCurve::Sin:
					case EWaveTableCurve::File:
					{
						GenerateExpressionCurve(CurveDataEntry, i, EWaveTableCurveSource::Expression);
					}
					break;

					case EWaveTableCurve::Shared:
					{
						if (UCurveFloat* SharedCurve = GetSharedCurve(i))
						{
							ClearExpressionCurve(i);
							SetCurve(i, SharedCurve->FloatCurve, EWaveTableCurveSource::Shared);
						}
						else
						{
							// Builds a dummy expression that just maps input to output in case
							// where asset isn't selected and leave source as unset
							GenerateExpressionCurve(CurveDataEntry, i, EWaveTableCurveSource::Expression, true /* bIsUnset */);
						}
					}
					break;

					case EWaveTableCurve::Custom:
					{
						FRichCurve& CustomCurve = GetCustomCurveChecked(i);
						TrimKeys(CustomCurve);
						ClearExpressionCurve(i);
						SetCurve(i, CustomCurve, EWaveTableCurveSource::Custom);
					}
					break;

					default:
					{
						static_assert(static_cast<int32>(EWaveTableCurve::Count) == 9, "Possible missing case coverage for output curve.");
					}
					break;
				}
			}

			// Collect and remove stale curves from editor
			TArray<FCurveModelID> ToRemove;
			TSet<FCurveModelID> ActiveModelIDs;
			for (const FCurveData& CurveDataEntry : CurveData)
			{
				ActiveModelIDs.Add(CurveDataEntry.ModelID);
			}

			const TMap<FCurveModelID, TUniquePtr<FCurveModel>>& Curves = CurveEditor->GetCurves();
			for (const TPair<FCurveModelID, TUniquePtr<FCurveModel>>& Pair : Curves)
			{
				if (!ActiveModelIDs.Contains(Pair.Key))
				{
					ToRemove.Add(Pair.Key);
				}
			}

			for (FCurveModelID ModelID : ToRemove)
			{
				CurveEditor->RemoveCurve(ModelID);
			}
		}

		void FWaveTableBankEditor::PostRedo(bool bSuccess)
		{
			if (bSuccess)
			{
				RefreshCurves();
			}
		}

		void FWaveTableBankEditor::TrimKeys(FRichCurve& OutCurve)
		{
			while (OutCurve.GetNumKeys() > 0 && 0.0f > OutCurve.GetFirstKey().Time)
			{
				OutCurve.DeleteKey(OutCurve.GetFirstKeyHandle());
			}

			while (OutCurve.GetNumKeys() > 0 && 1.0f < OutCurve.GetLastKey().Time)
			{
				OutCurve.DeleteKey(OutCurve.GetLastKeyHandle());
			}
		}
	} // namespace Editor
} // namespace WaveTable
#undef LOCTEXT_NAMESPACE
