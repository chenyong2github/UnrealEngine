// Copyright Epic Games, Inc. All Rights Reserved.
#include "Editors/ModulationPatchEditor.h"

#include "AudioModulationEditor.h"
#include "AudioModulationStyle.h"
#include "CommonFrameRates.h"
#include "Containers/Set.h"
#include "Curves/CurveBase.h"
#include "Curves/CurveLinearColor.h"
#include "CurveEditor.h"
#include "CurveEditorCommands.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RichCurveEditorModel.h"
#include "SCurveEditorPanel.h"
#include "SoundModulationPatch.h"
#include "SoundModulationTransform.h"
#include "Tree/SCurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/SCurveEditorTreePin.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SFrameRatePicker.h"


#define LOCTEXT_NAMESPACE "ModulationPatchEditor"


const FName FModulationPatchEditor::AppIdentifier(TEXT("ModulationPatchEditorApp"));
const FName FModulationPatchEditor::CurveTabId(TEXT("ModulationPatchEditor_Curves"));
const FName FModulationPatchEditor::PropertiesTabId(TEXT("ModulationPatchEditor_Properties"));


FModulationPatchEditor::FModulationPatchEditor()
{
}

void FModulationPatchEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_ModulationPatchEditor", "Modulation Patch Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FModulationPatchEditor::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	FSlateIcon CurveIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.CurveBase");

	InTabManager->RegisterTabSpawner(CurveTabId, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args) { return SpawnTab_OutputCurve(Args); }))
		.SetDisplayName(LOCTEXT("TransformCurvesTab", "Transform Curves"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(CurveIcon);
}

void FModulationPatchEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(PropertiesTabId);
	InTabManager->UnregisterTabSpawner(CurveTabId);
}

void FModulationPatchEditor::Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundModulationPatch* InPatchToEdit)
{
	CurveEditor = MakeShared<FCurveEditor>();
	FCurveEditorInitParams InitParams;
	CurveEditor->InitCurveEditor(InitParams);
	CurveEditor->GridLineLabelFormatXAttribute = LOCTEXT("GridXLabelFormat", "{0}");

	TUniquePtr<ICurveEditorBounds> EditorBounds = MakeUnique<FStaticCurveEditorBounds>();
	EditorBounds->SetInputBounds(0.05, 1.05);
	CurveEditor->SetBounds(MoveTemp(EditorBounds));

	CurvePanel = SNew(SCurveEditorPanel, CurveEditor.ToSharedRef());

	// Support undo/redo
	InPatchToEdit->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertiesView = PropertyModule.CreateDetailView(Args);
	PropertiesView->SetObject(InPatchToEdit);

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_ModulationPatchEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
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
		InPatchToEdit,
		bToolbarFocusable,
		bUseSmallIcons);

	FAudioModulationEditorModule& AudioEditorModule = FModuleManager::LoadModuleChecked<FAudioModulationEditorModule>(TEXT("AudioModulationEditor"));
	AddMenuExtender(AudioEditorModule.GetModulationPatchMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	AddToolbarExtender(AudioEditorModule.GetModulationPatchToolbarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	AddToolbarExtender(CurvePanel->GetToolbarExtender());

	if (CurveEditor.IsValid())
	{
		RegenerateMenusAndToolbars();
	}
}

void FModulationPatchEditor::ClearExpressionCurve(int32 InInputIndex)
{
	if (CurveData.IsValidIndex(InInputIndex))
	{
		CurveData[InInputIndex].ExpressionCurve.Reset();
	}
}

void FModulationPatchEditor::GenerateExpressionCurve(int32 InInputIndex, EModPatchOutputEditorCurveSource InSource, bool bInIsUnset)
{
	if (!CurveEditor.IsValid())
	{
		return;
	}

	USoundModulationPatch* Patch = CastChecked<USoundModulationPatch>(GetEditingObject());
	if (!ensure(Patch) || !CurveData.IsValidIndex(InInputIndex))
	{
		return;
	}

	TSharedPtr<FRichCurve> Curve = CurveData[InInputIndex].ExpressionCurve;
	if (!Curve.IsValid())
	{
		Curve = MakeShared<FRichCurve>();
		CurveData[InInputIndex].ExpressionCurve = Curve;
	}

	if (!GetIsBypassed())
	{
		const FSoundControlModulationInput& Input = Patch->PatchSettings.Inputs[InInputIndex];

		int32 CurveResolution;
		switch (Input.Transform.Curve)
		{
			case ESoundModulatorCurve::Linear:
			{
				CurveResolution = 2;
			}
			break;

			case ESoundModulatorCurve::Sin:
			case ESoundModulatorCurve::SCurve:
			{
				CurveResolution = 64;
			}
			break;

			case ESoundModulatorCurve::Log:
			case ESoundModulatorCurve::Exp:
			case ESoundModulatorCurve::Exp_Inverse:
			{
				CurveResolution = 256;
			}
			break;

			default:
			{
				CurveResolution = 128;
			}
			break;
		}

		Curve->Reset();

		check(CurveResolution > 1);
		const float CurveResolutionRatio = 1.0f / (CurveResolution - 1);
		for (int32 i = 0; i < CurveResolution; ++i)
		{
			const float X = FMath::Lerp(0.0f, 1.0f, CurveResolutionRatio * i);
			float Y = X;
			Input.Transform.Apply(Y);
			Curve->AddKey(X, Y);
		}
	}

	const EModPatchOutputEditorCurveSource Source = bInIsUnset ? EModPatchOutputEditorCurveSource::Unset : EModPatchOutputEditorCurveSource::Expression;
	SetCurve(InInputIndex, *Curve.Get(), Source);
}

bool FModulationPatchEditor::RequiresNewCurve(int32 InInputIndex, const FRichCurve& InRichCurve) const
{
	const FCurveModelID CurveModelID = CurveData[InInputIndex].ModelID;
	const TUniquePtr<FCurveModel>* CurveModel = CurveEditor->GetCurves().Find(CurveModelID);
	if (!CurveModel || !CurveModel->IsValid())
	{
		return true;
	}

	FModPatchCurveEditorModel* PatchCurveModel = static_cast<FModPatchCurveEditorModel*>(CurveModel->Get());
	check(PatchCurveModel);
	if (&PatchCurveModel->GetRichCurve() != &InRichCurve)
	{
		return true;
	}

	return false;
}


void FModulationPatchEditor::SetCurve(int32 InInputIndex, FRichCurve& InRichCurve, EModPatchOutputEditorCurveSource InSource)
{
	check(CurveEditor.IsValid());

	if (!ensure(CurveData.IsValidIndex(InInputIndex)))
	{
		return;
	}

	FCurveData& CurveDataEntry = CurveData[InInputIndex];

	const bool bRequiresNewCurve = RequiresNewCurve(InInputIndex, InRichCurve);
	if (bRequiresNewCurve)
	{
		TUniquePtr<FModPatchCurveEditorModel> NewCurve = MakeUnique<FModPatchCurveEditorModel>(InRichCurve, GetEditingObject(), InSource, InInputIndex);
		CurveDataEntry.ModelID = CurveEditor->AddCurve(MoveTemp(NewCurve));
	}
	else
	{
		const TUniquePtr<FCurveModel>& CurveModel = CurveEditor->GetCurves().FindChecked(CurveDataEntry.ModelID);
		check(CurveModel.Get());
		static_cast<FModPatchCurveEditorModel*>(CurveModel.Get())->Refresh(InSource, InInputIndex);
	}

	CurveEditor->PinCurve(CurveDataEntry.ModelID);
}

bool FModulationPatchEditor::GetIsBypassed() const
{
	if (USoundModulationPatch* Patch = Cast<USoundModulationPatch>(GetEditingObject()))
	{
		return Patch->PatchSettings.bBypass;
	}

	return false;
}

FName FModulationPatchEditor::GetToolkitFName() const
{
	return FName("ModulationPatchEditor");
}

FText FModulationPatchEditor::GetBaseToolkitName() const
{
	return LOCTEXT( "AppLabel", "Modulation Patch Editor" );
}

FString FModulationPatchEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "ModulationPatch ").ToString();
}

FLinearColor FModulationPatchEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

EOrientation FModulationPatchEditor::GetSnapLabelOrientation() const
{
	return FMultiBoxSettings::UseSmallToolBarIcons.Get()
		? EOrientation::Orient_Horizontal
		: EOrientation::Orient_Vertical;
}

void FModulationPatchEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		RefreshCurves();
	}
}

TSharedRef<SDockTab> FModulationPatchEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("SoundModulationPatchDetailsTitle", "Details"))
		[
			PropertiesView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FModulationPatchEditor::SpawnTab_OutputCurve(const FSpawnTabArgs& Args)
{
	RefreshCurves();
	CurveEditor->ZoomToFit();

	TSharedRef<SDockTab> NewDockTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("ModulationPatchEditor.Tabs.Properties"))
		.Label(FText::Format(LOCTEXT("ModulationPatchFilterTitle", "Filter Transform Curve: {0}"), FText::FromString(GetEditingObject()->GetName())))
		.TabColorScale(GetTabColorScale())
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(0.0f)
		[
			CurvePanel.ToSharedRef()
		]
		];

		return NewDockTab;
}

void FModulationPatchEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshCurves();
	}
}

void FModulationPatchEditor::ResetCurves()
{
	const USoundModulationPatch* Patch = CastChecked<USoundModulationPatch>(GetEditingObject());
	check(Patch);

	CurveEditor->RemoveAllCurves();
	CurveData.Reset();
	CurveData.AddDefaulted(Patch->PatchSettings.Inputs.Num());
}

void FModulationPatchEditor::InitCurves()
{
	const USoundModulationPatch * Patch = CastChecked<USoundModulationPatch>(GetEditingObject());
	const FSoundControlModulationPatch& PatchSettings = Patch->PatchSettings;
	const int32 NumInputs = PatchSettings.Inputs.Num();
	if (NumInputs < CurveData.Num() || NumInputs > CurveData.Num())
	{
		ResetCurves();
		return;
	}

	for (int32 i = 0; i < PatchSettings.Inputs.Num(); ++i)
	{
		const FSoundControlModulationInput& Input = PatchSettings.Inputs[i];
		switch (Input.Transform.Curve)
		{
			case ESoundModulatorCurve::Exp:
			case ESoundModulatorCurve::Exp_Inverse:
			case ESoundModulatorCurve::Linear:
			case ESoundModulatorCurve::Log:
			case ESoundModulatorCurve::SCurve:
			case ESoundModulatorCurve::Sin:
			{
				if (RequiresNewCurve(i, *CurveData[i].ExpressionCurve.Get()))
				{
					ResetCurves();
				}
			}
			break;

			case ESoundModulatorCurve::Shared:
			{
				if (UCurveFloat* SharedCurve = Input.Transform.CurveShared)
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

			case ESoundModulatorCurve::Custom:
			{
				if (RequiresNewCurve(i, Input.Transform.CurveCustom))
				{
					ResetCurves();
				}
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(ESoundModulatorCurve::Count) == 8, "Possible missing case coverage for output curve.");
			}
			break;
		}
	}
}

void FModulationPatchEditor::RefreshCurves()
{
	check(CurveEditor.IsValid());

	for (const FCurveData& CurveDataEntry : CurveData)
	{
		CurveEditor->UnpinCurve(CurveDataEntry.ModelID);
	}

	USoundModulationPatch* Patch = CastChecked<USoundModulationPatch>(GetEditingObject());
	check(Patch);

	if (Patch->PatchSettings.bBypass)
	{
		ResetCurves();
		return;
	}

	InitCurves();

	for (int32 i = 0; i < Patch->PatchSettings.Inputs.Num(); ++i)
	{
		FSoundControlModulationInput& Input = Patch->PatchSettings.Inputs[i];
		switch (Input.Transform.Curve)
		{
			case ESoundModulatorCurve::Exp:
			case ESoundModulatorCurve::Exp_Inverse:
			case ESoundModulatorCurve::Linear:
			case ESoundModulatorCurve::Log:
			case ESoundModulatorCurve::SCurve:
			case ESoundModulatorCurve::Sin:
			{
				GenerateExpressionCurve(i, EModPatchOutputEditorCurveSource::Expression);
			}
			break;

			case ESoundModulatorCurve::Shared:
			{
				if (UCurveFloat* SharedCurve = Input.Transform.CurveShared)
				{
					ClearExpressionCurve(i);
					SetCurve(i, SharedCurve->FloatCurve, EModPatchOutputEditorCurveSource::Shared);
				}
				else
				{
					// Builds a dummy expression that just maps input to output in case
					// where asset isn't selected and leave source as unset
					GenerateExpressionCurve(i, EModPatchOutputEditorCurveSource::Expression, true /* bIsUnset */);
				}
			}
			break;

			case ESoundModulatorCurve::Custom:
			{
				TrimKeys(Input.Transform);
				ClearExpressionCurve(i);
				SetCurve(i, Input.Transform.CurveCustom, EModPatchOutputEditorCurveSource::Custom);
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(ESoundModulatorCurve::Count) == 8, "Possible missing case coverage for output curve.");
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

void FModulationPatchEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshCurves();
	}
}

void FModulationPatchEditor::TrimKeys(FSoundModulationTransform& OutTransform)
{
	FRichCurve& Curve = OutTransform.CurveCustom;
	while (Curve.GetNumKeys() > 0 && 0.0f > Curve.GetFirstKey().Time)
	{
		Curve.DeleteKey(Curve.GetFirstKeyHandle());
	}

	while (Curve.GetNumKeys() > 0 && 1.0f < Curve.GetLastKey().Time)
	{
		Curve.DeleteKey(Curve.GetLastKeyHandle());
	}
}
#undef LOCTEXT_NAMESPACE
