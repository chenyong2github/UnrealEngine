// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Editors/ModulationSettingsEditor.h"

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
#include "ModulationSettingsEditor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RichCurveEditorModel.h"
#include "SCurveEditorPanel.h"
#include "SoundModulationPatch.h"
#include "Tree/SCurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/SCurveEditorTreePin.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SFrameRatePicker.h"
#include "ModulationSettingsCurveEditorViewStacked.h"


#define LOCTEXT_NAMESPACE "ModulationSettingsEditor"


const FName FModulationSettingsEditor::AppIdentifier(TEXT("ModulationSettingsEditorApp"));
const FName FModulationSettingsEditor::CurveTabId(TEXT("ModulationSettingsEditor_Curves"));
const FName FModulationSettingsEditor::PropertiesTabId(TEXT("ModulationSettingsEditor_Properties"));

FModulationSettingsEditor::FModulationSettingsEditor()
{
}

void FModulationSettingsEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_ModulationSettingsEditor", "Modulation Settings Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FModulationSettingsEditor::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	FSlateIcon CurveIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.CurveBase");

	InTabManager->RegisterTabSpawner(CurveTabId, FOnSpawnTab::CreateLambda([this](const FSpawnTabArgs& Args) { return SpawnTab_OutputCurve(Args); }))
		.SetDisplayName(LOCTEXT("TransformCurvesTab", "Transform Curves"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(CurveIcon);
}

void FModulationSettingsEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(PropertiesTabId);
	InTabManager->UnregisterTabSpawner(CurveTabId);
}

void FModulationSettingsEditor::Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, USoundModulationSettings* InSettingsToEdit)
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
	InSettingsToEdit->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertiesView = PropertyModule.CreateDetailView(Args);
	PropertiesView->SetObject(InSettingsToEdit);

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_ModulationSettingsEditor_Layout_v1")
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
		InSettingsToEdit,
		bToolbarFocusable,
		bUseSmallIcons);

	FAudioModulationEditorModule& AudioEditorModule = FModuleManager::LoadModuleChecked<FAudioModulationEditorModule>(TEXT("AudioModulationEditor"));
	AddMenuExtender(AudioEditorModule.GetModulationSettingsMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	AddToolbarExtender(AudioEditorModule.GetModulationSettingsToolbarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	AddToolbarExtender(CurvePanel->GetToolbarExtender());

	if (CurveEditor.IsValid())
	{
		RegenerateMenusAndToolbars();
	}
}

void FModulationSettingsEditor::GenerateExpressionCurve(const FSoundModulationOutputBase& InOutput, int32 InCurveIndex, bool bIsUnset)
{
	if (!CurveEditor.IsValid())
	{
		return;
	}

	ExpressionCurves.SetNum(InCurveIndex + 1, false /* bAllowShrinking */);
	TSharedPtr<FRichCurve> NewCurve = MakeShared<FRichCurve>();
	ExpressionCurves[InCurveIndex] = NewCurve;

	int32 CurveResolution;
	switch (InOutput.Transform.Curve)
	{
		case ESoundModulatorOutputCurve::Linear:
		{
			CurveResolution = 2;
		}
		break;

		case ESoundModulatorOutputCurve::Sin:
		case ESoundModulatorOutputCurve::SCurve:
		{
			CurveResolution = 64;
		}
		break;

		case ESoundModulatorOutputCurve::Log:
		case ESoundModulatorOutputCurve::Exp:
		case ESoundModulatorOutputCurve::Exp_Inverse:
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

	const float CurveResolutionRatio = 1.0f / (CurveResolution - 1);
	for (int32 i = 0; i < CurveResolution; ++i)
	{
		const float X = FMath::Lerp(InOutput.Transform.InputMin, InOutput.Transform.InputMax, CurveResolutionRatio * i);
		float Y = X;
		InOutput.Transform.Apply(Y);

		NewCurve->AddKey(X, Y);
	}

	const EModSettingsOutputEditorCurveSource Source = bIsUnset ? EModSettingsOutputEditorCurveSource::Unset : EModSettingsOutputEditorCurveSource::Expression;
	SetCurveAtOrderIndex(InCurveIndex, *NewCurve.Get(), Source, nullptr);
}

void FModulationSettingsEditor::SetCurveAtOrderIndex(int32 InCurveIndex, FRichCurve& InRichCurve, EModSettingsOutputEditorCurveSource InSource, UCurveFloat* InSharedCurve)
{
	check(CurveEditor.IsValid());

	bool bIsNew = false;
	while (InCurveIndex >= CurveModels.Num())
	{
		CurveModels.Add(FCurveModelID::Unique());
		bIsNew = true;
	}

	// Find if the incoming rich curve is already set.  If so, early out
	if (!bIsNew)
	{
		FCurveModel* CurrentModel = CurveEditor->FindCurve(CurveModels[InCurveIndex]);
		if (CurrentModel && CurrentModel->GetCurve() == &InRichCurve)
		{
			FModCurveEditorModel* ModModel = static_cast<FModCurveEditorModel*>(CurrentModel);
			if (InCurveIndex < static_cast<int32>(EModSettingsEditorCurveOutput::Control))
			{
				ModModel->Refresh(static_cast<EModSettingsEditorCurveOutput>(InCurveIndex), nullptr, InSharedCurve);
			}
			else
			{
				check(!InSharedCurve);

				FName ControlName = GetControlName(InCurveIndex);
				ModModel->Refresh(EModSettingsEditorCurveOutput::Control, &ControlName, InSharedCurve);
			}
			CurveEditor->PinCurve(CurveModels[InCurveIndex]);
			return;
		}
	}

	// Curves don't match, so remove what's already there
	if (FCurveModel* CurveModel = CurveEditor->FindCurve(CurveModels[InCurveIndex]))
	{
		CurveEditor->RemoveCurve(CurveModels[InCurveIndex]);
	}

	// Finally, create a FCurveModel add the new FRichCurve
	if (InCurveIndex < static_cast<int32>(EModSettingsEditorCurveOutput::Control))
	{
		const EModSettingsEditorCurveOutput CurveOutput = static_cast<EModSettingsEditorCurveOutput>(InCurveIndex);
		TUniquePtr<FModCurveEditorModel> NewCurve = MakeUnique<FModCurveEditorModel>(InRichCurve, GetEditingObject(), CurveOutput, InSource, InSharedCurve);
		CurveModels[InCurveIndex] = CurveEditor->AddCurve(MoveTemp(NewCurve));
	}
	else
	{
		const FName Name = CastChecked<USoundModulationSettings>(GetEditingObject())->Controls[GetControlIndex(InCurveIndex)].Control;
		TUniquePtr<FModCurveEditorModel> NewCurve = MakeUnique<FModCurveEditorModel>(InRichCurve, GetEditingObject(), Name, InSource, InSharedCurve);
		CurveModels[InCurveIndex] = CurveEditor->AddCurve(MoveTemp(NewCurve));
	}

	CurveEditor->PinCurve(CurveModels[InCurveIndex]);
}

int32 FModulationSettingsEditor::GetControlModulationCount() const
{
	USoundModulationSettings* Settings = Cast<USoundModulationSettings>(GetEditingObject());
	if (!Settings)
	{
		return 0;
	}

	return Settings->Controls.Num();
}

int32 FModulationSettingsEditor::GetCurveCount() const
{
	USoundModulationSettings* Settings = Cast<USoundModulationSettings>(GetEditingObject());
	if (!Settings)
	{
		return 0;
	}

	return static_cast<int32>(EModSettingsEditorCurveOutput::Count) - 1 + Settings->Controls.Num();
}

int32 FModulationSettingsEditor::GetCurveOrderIndex(EModSettingsEditorCurveOutput InCurveOutput, int32 InControlIndex) const
{
	switch (InCurveOutput)
	{
	case EModSettingsEditorCurveOutput::Volume:
	case EModSettingsEditorCurveOutput::Pitch:
	case EModSettingsEditorCurveOutput::Highpass:
	case EModSettingsEditorCurveOutput::Lowpass:
	{
		return static_cast<int32>(InCurveOutput);
	}
	break;

	case EModSettingsEditorCurveOutput::Control:
	{
		return static_cast<int32>(InCurveOutput) + InControlIndex;
	}
	break;

	default:
		static_assert(static_cast<int32>(EModSettingsEditorCurveOutput::Count) == 5, "Possible missing case coverage for enum EModSettingsEditorCurveOutput");
		return -1;
	}
}

int32 FModulationSettingsEditor::GetControlIndex(int32 InCurveIndex) const
{
	const int32 ControlIndexOffset = static_cast<int32>(EModSettingsEditorCurveOutput::Control);
	if (InCurveIndex < ControlIndexOffset)
	{
		return -1;
	}

	return InCurveIndex - ControlIndexOffset;
}

FName FModulationSettingsEditor::GetControlName(int32 InCurveIndex) const
{
	const int32 ControlIndexOffset = static_cast<int32>(EModSettingsEditorCurveOutput::Control);
	if (InCurveIndex < ControlIndexOffset)
	{
		return NAME_None;
	}

	USoundModulationSettings* Settings = Cast<USoundModulationSettings>(GetEditingObject());
	if (!Settings)
	{
		return NAME_None;
	}

	const int32 ControlIndex = InCurveIndex - static_cast<int32>(EModSettingsEditorCurveOutput::Control);
	return Settings->Controls[ControlIndex].Control;
}

FSoundModulationOutputBase* FModulationSettingsEditor::FindModulationOutput(int32 InCurveIndex)
{
	USoundModulationSettings* Settings = Cast<USoundModulationSettings>(GetEditingObject());
	if (!Settings)
	{
		return nullptr;
	}

	if (InCurveIndex >= static_cast<int32>(EModSettingsEditorCurveOutput::Control))
	{
		const int32 ControlIndex = InCurveIndex - static_cast<int32>(EModSettingsEditorCurveOutput::Control);
		return &Settings->Controls[ControlIndex].Output;
	}

	const EModSettingsEditorCurveOutput CurveOutput = static_cast<EModSettingsEditorCurveOutput>(InCurveIndex);
	switch (CurveOutput)
	{
		case EModSettingsEditorCurveOutput::Volume:
		{
			return static_cast<FSoundModulationOutputBase*>(&Settings->Volume.Output);
		}
		break;

		case EModSettingsEditorCurveOutput::Pitch:
		{
			return static_cast<FSoundModulationOutputBase*>(&Settings->Pitch.Output);
		}
		break;

		case EModSettingsEditorCurveOutput::Highpass:
		{
			return static_cast<FSoundModulationOutputBase*>(&Settings->Highpass.Output);
		}
		break;

		case EModSettingsEditorCurveOutput::Lowpass:
		{
			return static_cast<FSoundModulationOutputBase*>(&Settings->Lowpass.Output);
		}
		break;

		default:
		{
			static_assert(static_cast<int32>(EModSettingsEditorCurveOutput::Count) == 5, "Possible missing case coverage for enum EModSettingsEditorCurveOutput");
			return nullptr;
		}
		break;
	}
}

FName FModulationSettingsEditor::GetToolkitFName() const
{
	return FName("ModulationSettingsEditor");
}

FText FModulationSettingsEditor::GetBaseToolkitName() const
{
	return LOCTEXT( "AppLabel", "Modulation Settings Editor" );
}

FString FModulationSettingsEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "ModulationSettings ").ToString();
}

FLinearColor FModulationSettingsEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.0f, 0.0f, 0.2f, 0.5f );
}

EOrientation FModulationSettingsEditor::GetSnapLabelOrientation() const
{
	return FMultiBoxSettings::UseSmallToolBarIcons.Get()
		? EOrientation::Orient_Horizontal
		: EOrientation::Orient_Vertical;
}

void FModulationSettingsEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		UpdateCurves();
	}
}

TSharedRef<SDockTab> FModulationSettingsEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("SoundModulationSettingsDetailsTitle", "Details"))
		[
			PropertiesView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FModulationSettingsEditor::SpawnTab_OutputCurve(const FSpawnTabArgs& Args)
{
	UpdateCurves();
	CurveEditor->ZoomToFit();

	TSharedRef<SDockTab> NewDockTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("ModulationSettingsEditor.Tabs.Properties"))
		.Label(FText::Format(LOCTEXT("ModulationSettingsFilterTitle", "Filter Transform Curve: {0}"), FText::FromString(GetEditingObject()->GetName())))
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

void FModulationSettingsEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		UpdateCurves();
	}

	CastChecked<USoundModulationSettings>(GetEditingObject())->OnPostEditChange(nullptr);
}

void FModulationSettingsEditor::UpdateCurve(int32 InCurveIndex)
{
	check(CurveEditor.IsValid());

	FSoundModulationOutputBase* Output = FindModulationOutput(InCurveIndex);
	if (!Output)
	{
		return;
	}

	switch (Output->Transform.Curve)
	{
		case ESoundModulatorOutputCurve::Exp:
		case ESoundModulatorOutputCurve::Exp_Inverse:
		case ESoundModulatorOutputCurve::Linear:
		case ESoundModulatorOutputCurve::Log:
		case ESoundModulatorOutputCurve::SCurve:
		case ESoundModulatorOutputCurve::Sin:
		{
			GenerateExpressionCurve(*Output, InCurveIndex);
		}
		break;

		case ESoundModulatorOutputCurve::Shared:
		{
			if (UCurveFloat* SharedCurve = Output->Transform.CurveShared)
			{
				SetCurveAtOrderIndex(InCurveIndex, SharedCurve->FloatCurve, EModSettingsOutputEditorCurveSource::Shared, SharedCurve);
			}
			else
			{
				// Builds a dummy expression that just maps input to output in case
				// where asset isn't selected and leave source as unset
				GenerateExpressionCurve(*Output, InCurveIndex, true /* bIsUnset */);
			}
		}
		break;

		case ESoundModulatorOutputCurve::Custom:
		{
			TrimKeys(*Output);
			SetCurveAtOrderIndex(InCurveIndex, Output->Transform.CurveCustom, EModSettingsOutputEditorCurveSource::Custom);
		}
		break;

		default:
		{
			static_assert(static_cast<int32>(ESoundModulatorOutputCurve::Count) == 8, "Possible missing case coverage for output curve.");
		}
		break;
	}
}

void FModulationSettingsEditor::UpdateCurves()
{
	if (!CurveEditor.IsValid())
	{
		return;
	}

	for (int32 i = 0; i < GetCurveCount(); ++i)
	{
		UpdateCurve(i);
	}

	// Remove old curves from editor
	for (int32 i = GetCurveCount(); i < CurveModels.Num(); ++i)
	{
		CurveEditor->RemoveCurve(CurveModels[i]);
	}
}

void FModulationSettingsEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		UpdateCurves();
	}

	CastChecked<USoundModulationSettings>(GetEditingObject())->OnPostEditChange(nullptr);
}

void FModulationSettingsEditor::TrimKeys(FSoundModulationOutputBase& InOutput) const
{
	FRichCurve& Curve = InOutput.Transform.CurveCustom;
	while (Curve.GetNumKeys() > 0 && InOutput.Transform.InputMin > Curve.GetFirstKey().Time)
	{
		Curve.DeleteKey(Curve.GetFirstKeyHandle());
	}

	while (Curve.GetNumKeys() > 0 && InOutput.Transform.InputMax < Curve.GetLastKey().Time)
	{
		Curve.DeleteKey(Curve.GetLastKeyHandle());
	}
}
#undef LOCTEXT_NAMESPACE
