// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SPaintModeWidget.h"

#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "PaintModeSettingsCustomization.h"
#include "PaintModePainter.h"
#include "PaintModeSettings.h"

#include "Modules/ModuleManager.h"
#include "PaintModeCommands.h"
#include "DetailLayoutBuilder.h"

#include "EditorModeManager.h"
#include "EditorModes.h"
#include "MeshPaintEdMode.h"

#define LOCTEXT_NAMESPACE "PaintModePainter"

void SPaintModeWidget::Construct(const FArguments& InArgs, FPaintModePainter* InPainter)
{
	MeshPainter = InPainter;
	PaintModeSettings = Cast<UPaintModeSettings>(MeshPainter->GetPainterSettings());
	SettingsObjects.Add(MeshPainter->GetBrushSettings());
	SettingsObjects.Add(PaintModeSettings);
	CreateDetailsView();
	
	FMargin StandardPadding(0.0f, 4.0f, 0.0f, 4.0f);
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(0, 0, 0, 5)
		[
			SAssignNew(ErrorTextWidget, SErrorText)
		]
		+ SScrollBox::Slot()
		.Padding(0.0f)
		[
			SNew(SVerticalBox)
			/** Toolbar containing buttons to switch between different paint modes */
			.IsEnabled(this, &SPaintModeWidget::GetMeshPaintEditorIsEnabled)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.HAlign(HAlign_Center)
				[
					CreateToolBarWidget()->AsShared()
				]
			]
				
			/** (Instance) Vertex paint action buttons widget */
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreateVertexPaintWidget()->AsShared()
			]
				
			/** Texture paint action buttons widget */
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				CreateTexturePaintWidget()->AsShared()
			]
			+ SVerticalBox::Slot()
			.Padding(2.0f, 4.0f)
			.HAlign(HAlign_Right)
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Visibility_Lambda([this]() -> EVisibility
				{
					return PaintModeSettings->PaintMode == EPaintMode::Vertices ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text_Lambda([this]() -> FText
				{
					if (MeshPainter)
					{
						return FText::Format(FTextFormat::FromString(TEXT("Instance Color Size: {0} KB")), MeshPainter->GetVertexPaintColorBufferSize() / 1024.f);
					}
					return FText::GetEmpty();
				})
			]
			/** DetailsView containing brush and paint settings */
			+ SVerticalBox::Slot()
			.AutoHeight()				
			[
				SettingsDetailsView->AsShared()
			]

		]
	];
}

void SPaintModeWidget::CreateDetailsView()
{
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ false,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ this,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	SettingsDetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	SettingsDetailsView->SetRootObjectCustomizationInstance(MakeShareable(new FPaintModeSettingsRootObjectCustomization));
	SettingsDetailsView->SetObjects(SettingsObjects);
}

TSharedPtr<SWidget> SPaintModeWidget::CreateVertexPaintWidget()
{
	FMargin StandardPadding(0.0f, 4.0f, 0.0f, 4.0f);

	TSharedPtr<SWidget> VertexColorWidget;
	TSharedPtr<SHorizontalBox> VertexColorActionBox;
	TSharedPtr<SHorizontalBox> InstanceColorActionBox;

	SAssignNew(VertexColorWidget, SVerticalBox)
	.Visibility(this, &SPaintModeWidget::IsVertexPaintModeVisible)
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(StandardPadding)
	.HAlign(HAlign_Center)
	[	
		SAssignNew(VertexColorActionBox, SHorizontalBox)
	]
	
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(StandardPadding)
	.HAlign(HAlign_Center)
	[
		SAssignNew(InstanceColorActionBox, SHorizontalBox)
	];

	FToolBarBuilder ColorToolbarBuilder(MeshPainter->GetUICommandList(), FMultiBoxCustomization::None);
	ColorToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
	ColorToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Fill, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Fill"));
	ColorToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Propagate, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Propagate"));
	ColorToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Import, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Import"));
	ColorToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Save, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Save"));

	VertexColorActionBox->AddSlot()
	.FillWidth(1.0f)
	[
		ColorToolbarBuilder.MakeWidget()
	];

	FToolBarBuilder InstanceToolbarBuilder(MeshPainter->GetUICommandList(), FMultiBoxCustomization::None);
	InstanceToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
	InstanceToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Copy, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Copy"));
	InstanceToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Paste, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Paste"));
	InstanceToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Remove, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Remove"));
	InstanceToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().Fix, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Fix"));
	InstanceToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().PropagateVertexColorsToLODs, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Propagate"));

	InstanceColorActionBox->AddSlot()
	.FillWidth(1.0f)
	[
		InstanceToolbarBuilder.MakeWidget()
	];

	return VertexColorWidget->AsShared();
}
 
TSharedPtr<SWidget> SPaintModeWidget::CreateTexturePaintWidget()
{
	FMargin StandardPadding(0.0f, 4.0f, 0.0f, 4.0f);
	TSharedPtr<SWidget> TexturePaintWidget;
	TSharedPtr<SHorizontalBox> ActionBox;

	SAssignNew(TexturePaintWidget, SVerticalBox)
	.Visibility(this, &SPaintModeWidget::IsTexturePaintModeVisible)
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(StandardPadding)
	.HAlign(HAlign_Center)
	[
		SAssignNew(ActionBox, SHorizontalBox)
	];
	 
	FToolBarBuilder TexturePaintToolbarBuilder(MeshPainter->GetUICommandList(), FMultiBoxCustomization::None);
	TexturePaintToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
	TexturePaintToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().PropagateTexturePaint, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Propagate"));
	TexturePaintToolbarBuilder.AddToolBarButton(FPaintModeCommands::Get().SaveTexturePaint, NAME_None, FText::GetEmpty(), TAttribute<FText>(), FSlateIcon(FEditorStyle::GetStyleSetName(), "MeshPaint.Save"));

	ActionBox->AddSlot()
	.FillWidth(1.0f)
	[
		TexturePaintToolbarBuilder.MakeWidget()
	];

	return TexturePaintWidget->AsShared();
}

TSharedPtr<SWidget> SPaintModeWidget::CreateToolBarWidget()
{
	FToolBarBuilder ModeSwitchButtons(MakeShareable(new FUICommandList()), FMultiBoxCustomization::None);
	{
		FSlateIcon ColorPaintIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.MeshPaintMode.ColorPaint");
		ModeSwitchButtons.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([=]()
		{
			PaintModeSettings->PaintMode = EPaintMode::Vertices;
			PaintModeSettings->VertexPaintSettings.MeshPaintMode = EMeshPaintMode::PaintColors;
			SettingsDetailsView->SetObjects(SettingsObjects, true);
		}), FCanExecuteAction(), FIsActionChecked::CreateLambda([=]() -> bool { return PaintModeSettings->PaintMode == EPaintMode::Vertices && PaintModeSettings->VertexPaintSettings.MeshPaintMode == EMeshPaintMode::PaintColors; })), NAME_None, LOCTEXT("Mode.VertexColorPainting", "Colors"), LOCTEXT("Mode.VertexColor.Tooltip", "Vertex Color Painting mode allows painting of Vertex Colors"), ColorPaintIcon, EUserInterfaceActionType::ToggleButton);

		FSlateIcon WeightPaintIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.MeshPaintMode.WeightPaint");
		ModeSwitchButtons.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([=]()
		{
			PaintModeSettings->PaintMode = EPaintMode::Vertices;
			PaintModeSettings->VertexPaintSettings.MeshPaintMode = EMeshPaintMode::PaintWeights;
			SettingsDetailsView->SetObjects(SettingsObjects, true);
		}), FCanExecuteAction(), FIsActionChecked::CreateLambda([=]() -> bool { return PaintModeSettings->PaintMode == EPaintMode::Vertices && PaintModeSettings->VertexPaintSettings.MeshPaintMode == EMeshPaintMode::PaintWeights; })), NAME_None, LOCTEXT("Mode.VertexWeightPainting", " Weights"), LOCTEXT("Mode.VertexWeight.Tooltip", "Vertex Weight Painting mode allows painting of Vertex Weights"), WeightPaintIcon, EUserInterfaceActionType::ToggleButton);

		FSlateIcon TexturePaintIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.MeshPaintMode.TexturePaint");
		ModeSwitchButtons.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([=]()
		{
			PaintModeSettings->PaintMode = EPaintMode::Textures;
			SettingsDetailsView->SetObjects(SettingsObjects, true);
		}), FCanExecuteAction(), FIsActionChecked::CreateLambda([=]() -> bool { return PaintModeSettings->PaintMode == EPaintMode::Textures; })), NAME_None, LOCTEXT("Mode.TexturePainting", "Textures"), LOCTEXT("Mode.Texture.Tooltip", "Texture Weight Painting mode allows painting on Textures"), TexturePaintIcon, EUserInterfaceActionType::ToggleButton);
	}

	return ModeSwitchButtons.MakeWidget();
}

EVisibility SPaintModeWidget::IsVertexPaintModeVisible() const
{
	UPaintModeSettings* MeshPaintSettings = (UPaintModeSettings*)MeshPainter->GetPainterSettings();
	return (MeshPaintSettings->PaintMode == EPaintMode::Vertices) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SPaintModeWidget::IsTexturePaintModeVisible() const
{
	UPaintModeSettings* MeshPaintSettings = (UPaintModeSettings*)MeshPainter->GetPainterSettings();
	return (MeshPaintSettings->PaintMode == EPaintMode::Textures) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SPaintModeWidget::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, UProperty* PropertyThatChanged)
{
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		for (UObject* Settings : SettingsObjects)
		{
			Settings->SaveConfig();
		}
	}
}

bool SPaintModeWidget::GetMeshPaintEditorIsEnabled() const
{
	FEdModeMeshPaint* MeshPaintMode = (FEdModeMeshPaint*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_MeshPaint);
	if (MeshPaintMode)
	{
		bool bEnabled = MeshPaintMode->IsEditingEnabled();
		FText ErrorText = bEnabled ? FText::GetEmpty() : LOCTEXT("MeshPaintSM5Only", "Mesh Paint mode can only be used in SM5.");
		ErrorTextWidget->SetError(ErrorText);
		return bEnabled;
	}
	return false;
}

#undef LOCTEXT_NAMESPACE // "PaintModePainter"
