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
#include "Tree/SCurveEditorTree.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "Tree/SCurveEditorTreePin.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SFrameRatePicker.h"
#include "ModulationPatchCurveEditorViewStacked.h"
#include "SoundModulationPatch.h"


#define LOCTEXT_NAMESPACE "ModulationPatchEditor"


const FName FModulationPatchEditor::AppIdentifier(TEXT("ModulationPatchEditorApp"));
const FName FModulationPatchEditor::CurveTabId(TEXT("ModulationPatchEditor_Curves"));
const FName FModulationPatchEditor::PropertiesTabId(TEXT("ModulationPatchEditor_Properties"));


FModulationPatchEditor::FModulationPatchEditor()
	: CurveModel(FCurveModelID::Unique())
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

void FModulationPatchEditor::GenerateExpressionCurve(const FSoundModulationOutputTransform& InTransform, bool bIsUnset)
{
	if (!CurveEditor.IsValid())
	{
		return;
	}

	TSharedPtr<FRichCurve> NewCurve = MakeShared<FRichCurve>();
	ExpressionCurve = NewCurve;

	if (!GetIsBypassed())
	{
		int32 CurveResolution;
		switch (InTransform.Curve)
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
			const float X = FMath::Lerp(InTransform.InputMin, InTransform.InputMax, CurveResolutionRatio * i);
			float Y = X;
			InTransform.Apply(Y);

			NewCurve->AddKey(X, Y);
		}
	}

	const EModPatchOutputEditorCurveSource Source = bIsUnset ? EModPatchOutputEditorCurveSource::Unset : EModPatchOutputEditorCurveSource::Expression;
	SetCurve(*NewCurve.Get(), Source, nullptr);
}

void FModulationPatchEditor::SetCurve(FRichCurve& InRichCurve, EModPatchOutputEditorCurveSource InSource, UCurveFloat* InSharedCurve)
{
	check(CurveEditor.IsValid());

	CurveEditor->RemoveAllCurves();
	TUniquePtr<FModPatchCurveEditorModel> NewCurve = MakeUnique<FModPatchCurveEditorModel>(InRichCurve, GetEditingObject(), InSource, InSharedCurve);
	CurveModel = CurveEditor->AddCurve(MoveTemp(NewCurve));
	CurveEditor->PinCurve(CurveModel);
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
		UpdateCurve();
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
	UpdateCurve();
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
		UpdateCurve();
	}
}

void FModulationPatchEditor::UpdateCurve()
{
	check(CurveEditor.IsValid());

	CurveEditor->UnpinCurve(CurveModel);

	USoundModulationPatch* Patch = CastChecked<USoundModulationPatch>(GetEditingObject());

	if (Patch->PatchSettings.bBypass)
	{
		CurveEditor->RemoveAllCurves();
		return;
	}

	FSoundModulationOutputTransform Transform = Patch->PatchSettings.Transform;
	switch (Transform.Curve)
	{
		case ESoundModulatorOutputCurve::Exp:
		case ESoundModulatorOutputCurve::Exp_Inverse:
		case ESoundModulatorOutputCurve::Linear:
		case ESoundModulatorOutputCurve::Log:
		case ESoundModulatorOutputCurve::SCurve:
		case ESoundModulatorOutputCurve::Sin:
		{
			GenerateExpressionCurve(Transform);
		}
		break;

		case ESoundModulatorOutputCurve::Shared:
		{
			if (UCurveFloat* SharedCurve = Transform.CurveShared)
			{
				SetCurve(SharedCurve->FloatCurve, EModPatchOutputEditorCurveSource::Shared, SharedCurve);
			}
			else
			{
				// Builds a dummy expression that just maps input to output in case
				// where asset isn't selected and leave source as unset
				GenerateExpressionCurve(Transform, true /* bIsUnset */);
			}
		}
		break;

		case ESoundModulatorOutputCurve::Custom:
		{
			TrimKeys(Transform);
			SetCurve(Transform.CurveCustom, EModPatchOutputEditorCurveSource::Custom);
		}
		break;

		default:
		{
			static_assert(static_cast<int32>(ESoundModulatorOutputCurve::Count) == 8, "Possible missing case coverage for output curve.");
		}
		break;
	}
}

void FModulationPatchEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		UpdateCurve();
	}
}

void FModulationPatchEditor::TrimKeys(FSoundModulationOutputTransform& OutTransform) const
{
	FRichCurve& Curve = OutTransform.CurveCustom;
	while (Curve.GetNumKeys() > 0 && OutTransform.InputMin > Curve.GetFirstKey().Time)
	{
		Curve.DeleteKey(Curve.GetFirstKeyHandle());
	}

	while (Curve.GetNumKeys() > 0 && OutTransform.InputMax < Curve.GetLastKey().Time)
	{
		Curve.DeleteKey(Curve.GetLastKeyHandle());
	}
}
#undef LOCTEXT_NAMESPACE
