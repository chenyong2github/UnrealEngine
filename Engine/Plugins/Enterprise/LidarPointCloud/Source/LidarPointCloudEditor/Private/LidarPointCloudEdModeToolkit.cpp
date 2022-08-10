// Copyright Epic Games, Inc. All Rights Reserved.

#include "LidarPointCloudEdModeToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "InteractiveToolManager.h"
#include "LidarPointCloudEditorCommands.h"
#include "LidarPointCloudEditorTools.h"
#include "LidarPointCloudEdMode.h"
#include "Selection.h"
#include "SlateOptMacros.h"
#include "StatusBarSubsystem.h"
#include "Toolkits/AssetEditorModeUILayer.h"

#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "LidarEditMode"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SLidarEditorWidget::Construct(const FArguments& InArgs)
{
	LidarEditorMode = (ULidarEditorMode*)GLevelEditorModeTools().GetActiveMode(FLidarEditorModes::EM_Lidar);
	
	// Everything (or almost) uses this padding, change it to expand the padding.
	FMargin StandardPadding(5.f, 2.f);
	FMargin HeaderPadding(8.f, 4.f);

	FSlateFontInfo HeaderFont = FAppStyle::GetFontStyle(TEXT("DetailsView.CategoryFontStyle")); HeaderFont.Size = 11;
	FSlateFontInfo StandardFont = FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"));
	FSlateFontInfo LabelFont = FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")); LabelFont.Size = 9;

#define HEADEREX(ContText, Category) +SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 5.0f))[SNew(SHeaderRow)+SHeaderRow::Column(ContText).HAlignCell(HAlign_Left).FillWidth(1).HeaderContentPadding(HeaderPadding)[SNew(STextBlock).Text(LOCTEXT(Category, ContText)).Font(HeaderFont)]]
#define HEADER(ContText) HEADEREX(ContText, ContText"Header")
#define BUTTON(Category, ConText, Action) +SHorizontalBox::Slot().Padding(StandardPadding).FillWidth(0.5f)[SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT(Category,ConText)).OnClicked_Lambda([this]{ Action return FReply::Handled(); })]
#define BUTTON_POINTS(Category, ConText, Action) +SHorizontalBox::Slot().Padding(StandardPadding).FillWidth(0.5f)[SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT(Category,ConText)).OnClicked_Lambda([this]{ Action return FReply::Handled(); }).IsEnabled(this, &SLidarEditorWidget::IsPointSelection)]
#define BUTTON_ACTORS(Category, ConText, Action) +SHorizontalBox::Slot().Padding(StandardPadding).FillWidth(0.5f)[SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT(Category,ConText)).OnClicked_Lambda([this]{ Action return FReply::Handled(); }).IsEnabled(this, &SLidarEditorWidget::IsActorSelection)]

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON("SelectionClear", "Clear",
			{
				if(IsActorSelection())
				{
					FLidarPointCloudEditorHelper::ClearActorSelection();
				}
				else
				{
					FLidarPointCloudEditorHelper::ClearSelection();
				}
			})
			BUTTON("SelectionInvert", "Invert",
			{
				if(IsActorSelection())
				{
					FLidarPointCloudEditorHelper::InvertActorSelection();
				}
				else
				{
					FLidarPointCloudEditorHelper::InvertSelection();
				}
			})
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.0f, 5.0f))
		[
			SNew(SHeaderRow)
			.Visibility(this, &SLidarEditorWidget::GetBrushVisibility)
			+SHeaderRow::Column("Brush")
			.HAlignCell(HAlign_Left)
			.FillWidth(1)
			.HeaderContentPadding(HeaderPadding)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BrushHeader", "Brush"))
				.Font(HeaderFont)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SLidarEditorWidget::GetBrushVisibility)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BrushRadius", "Brush Radius"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SNumericEntryBox<float>)
				.Font(StandardFont)
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(65536.0f)
				.MaxSliderValue(4096.0f)
				.MinDesiredValueWidth(50.0f)
				.SliderExponent(3.0f)
				.Value_Lambda([this]
				{
					return BrushTool ? BrushTool->BrushRadius : 0;
				})
				.OnValueChanged_Lambda([this](float Value)
				{
					if(BrushTool)
					{
						BrushTool->BrushRadius = Value;
					}
				})
			]
		]
		HEADER("Cleanup")
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_POINTS("CleanupHideSelected", "Hide Selected", {FLidarPointCloudEditorHelper::HideSelected();})
			BUTTON_POINTS("CleanupResetVisibility", "Reset Visibility", {FLidarPointCloudEditorHelper::ResetVisibility();})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_POINTS("CleanupDeleteSelected", "Delete Selected", {FLidarPointCloudEditorHelper::DeleteSelected();})
			BUTTON("CleanupDeleteHidden", "Delete Hidden", {FLidarPointCloudEditorHelper::DeleteHidden();})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_POINTS("CleanupCropToSelection", "Crop To Selection",
			{
				FLidarPointCloudEditorHelper::InvertSelection();
				FLidarPointCloudEditorHelper::DeleteSelected();
			})
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SSpacer)
			]
		]
		HEADER("Collisions")
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CollisionsError", "Max Error"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SNumericEntryBox<float>)
				.Font(StandardFont)
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(65536.0f)
				.MaxSliderValue(512.0f)
				.MinDesiredValueWidth(50.0f)
				.Value_Lambda([this]{ return MaxCollisionError; })
				.OnValueChanged_Lambda([this](float Value) { MaxCollisionError = Value; })
				.IsEnabled(this, &SLidarEditorWidget::IsActorSelection)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_ACTORS("CollisionsAdd", "Add Collision",
			{
				FLidarPointCloudEditorHelper::SetCollisionErrorForSelection(MaxCollisionError);
				FLidarPointCloudEditorHelper::BuildCollisionForSelection();
			})
			BUTTON_ACTORS("CollisionsRemove", "Remove Collision", {FLidarPointCloudEditorHelper::RemoveCollisionForSelection();})
		]
		HEADER("Normals")
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NormalsQuality", "Quality"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SNumericEntryBox<int32>)
				.Font(StandardFont)
				.AllowSpin(true)
				.MinValue(0)
				.MaxValue(100)
				.Value_Lambda([this]{ return NormalsQuality; })
				.OnValueChanged_Lambda([this](int32 Value) { NormalsQuality = Value; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NormalsNoiseTolerance", "Noise Tolerance"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SNumericEntryBox<float>)
				.Font(StandardFont)
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(20.0f)
				.MaxSliderValue(5.0f)
				.MinDesiredValueWidth(50.0f)
				.Value_Lambda([this]{ return NormalsNoiseTolerance; })
				.OnValueChanged_Lambda([this](float Value) { NormalsNoiseTolerance = Value; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON("NormalsCalculate", "Calculate Normals",
			{
				FLidarPointCloudEditorHelper::SetNormalsQuality(NormalsQuality, NormalsNoiseTolerance);
				if(IsActorSelection())
				{
					FLidarPointCloudEditorHelper::CalculateNormals();
				}
				else
				{
					FLidarPointCloudEditorHelper::CalculateNormalsForSelection();
				}
			})
		]
		HEADER("Meshing")
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MeshingError", "Max Error"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SNumericEntryBox<float>)
				.Font(StandardFont)
				.AllowSpin(true)
				.MinValue(0.0f)
				.MaxValue(65536.0f)
				.MaxSliderValue(512.0f)
				.MinDesiredValueWidth(50.0f)
				.Value_Lambda([this]{ return MaxMeshingError; })
				.OnValueChanged_Lambda([this](float Value) { MaxMeshingError = Value; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MeshingMerge", "Merge Meshes"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]{ return bMergeMeshes ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState Value) { bMergeMeshes = Value == ECheckBoxState::Checked; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MeshingRetain", "Retain Transform"))
				.Font(LabelFont)
			]
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]{ return !bMergeMeshes && bRetainTransform ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState Value) { bRetainTransform = Value == ECheckBoxState::Checked; })
				.IsEnabled_Lambda([this] { return !bMergeMeshes; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON("MeshingBuild", "Create Static Mesh", {FLidarPointCloudEditorHelper::MeshSelected(IsPointSelection(), MaxMeshingError, bMergeMeshes, !bMergeMeshes && bRetainTransform);})
		]
		HEADER("Alignment")
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_ACTORS("AlignmentCoords", "Original Coordinates", {FLidarPointCloudEditorHelper::SetOriginalCoordinateForSelection();})
			BUTTON_ACTORS("AlignmentOrigin", "World Origin", {FLidarPointCloudEditorHelper::AlignSelectionAroundWorldOrigin();})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_ACTORS("AlignmentReset", "Reset Alignment", {FLidarPointCloudEditorHelper::CenterSelection();})
			+ SHorizontalBox::Slot()
			.Padding(StandardPadding)
			.FillWidth(0.5f)
			[
				SNew(SSpacer)
			]
		]
		HEADEREX("Merge & Extract", "MergeExtract")
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_POINTS("MergeExtractSelection", "Extract Selection", {FLidarPointCloudEditorHelper::Extract();})
			BUTTON_POINTS("MergeExtractCopy", "Extract as Copy", {FLidarPointCloudEditorHelper::ExtractAsCopy();})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			BUTTON_ACTORS("MergeExtractActors", "Merge Actors", {FLidarPointCloudEditorHelper::MergeSelectionByComponent(true);})
			BUTTON_ACTORS("MergeExtractData", "Merge Data", {FLidarPointCloudEditorHelper::MergeSelectionByData(true);})
		]
	];

#undef BUTTON_ACTORS
#undef BUTTON_POINTS
#undef BUTTON
#undef HEADER
#undef HEADEREX
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FLidarPointCloudEdModeToolkit::~FLidarPointCloudEdModeToolkit()
{
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolNotificationMessage.RemoveAll(this);
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolWarningMessage.RemoveAll(this);
}

void FLidarPointCloudEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	EditorWidget = SNew(SLidarEditorWidget);
	
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolNotificationMessage.AddSP(this, &FLidarPointCloudEdModeToolkit::SetActiveToolMessage);
	GetScriptableEditorMode()->GetInteractiveToolsContext(EToolsContextScope::EdMode)->OnToolWarningMessage.AddSP(this, &FLidarPointCloudEdModeToolkit::SetActiveToolMessage);
}

FName FLidarPointCloudEdModeToolkit::GetToolkitFName() const
{
	return FName("LidarEditMode");
}

FText FLidarPointCloudEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT( "ToolkitName", "Lidar" );
}

void FLidarPointCloudEdModeToolkit::GetToolPaletteNames(TArray<FName>& InPaletteName) const
{
	InPaletteName.Add(LidarEditorPalletes::Manage);
}

FText FLidarPointCloudEdModeToolkit::GetToolPaletteDisplayName(FName PaletteName) const
{
	return LOCTEXT("LidarMode_Manage", "Manage");
}

FText FLidarPointCloudEdModeToolkit::GetActiveToolDisplayName() const
{
	if (UInteractiveTool* ActiveTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left))
	{
		return ActiveTool->GetClass()->GetDisplayNameText();
	}

	return LOCTEXT("LidarNoActiveTool", "LidarNoActiveTool");
}

FText FLidarPointCloudEdModeToolkit::GetActiveToolMessage() const
{
	return ActiveToolMessageCache;
}

void FLidarPointCloudEdModeToolkit::SetActiveToolMessage(const FText& Message)
{
	ActiveToolMessageCache = Message;
	
	if (ModeUILayer.IsValid())
	{
		TSharedPtr<FAssetEditorModeUILayer> ModeUILayerPtr = ModeUILayer.Pin();
		GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PopStatusBarMessage(ModeUILayerPtr->GetStatusBarName(), ActiveToolMessageHandle);
		ActiveToolMessageHandle = GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PushStatusBarMessage(ModeUILayerPtr->GetStatusBarName(), Message);
	}

	ActiveToolMessageHandle.Reset();
}

void FLidarPointCloudEdModeToolkit::SetActorSelection(bool bNewActorSelection)
{
	if(EditorWidget.IsValid())
	{
		EditorWidget->bActorSelection = bNewActorSelection;
	}
}

void FLidarPointCloudEdModeToolkit::SetBrushTool(ULidarEditorToolPaintSelection* NewBrushTool)
{
	if(EditorWidget.IsValid())
	{
		EditorWidget->BrushTool = NewBrushTool;
	}
}

#undef LOCTEXT_NAMESPACE
