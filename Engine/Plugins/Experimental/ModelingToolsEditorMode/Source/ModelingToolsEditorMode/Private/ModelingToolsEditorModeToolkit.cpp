// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorModeToolkit.h"
#include "ModelingToolsEditorMode.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingToolsEditorModeSettings.h"
#include "ModelingToolsEditorModeStyle.h"
#include "Engine/Selection.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IDetailRootObjectCustomization.h"
#include "ISettingsModule.h"
#include "EditorModeManager.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SExpandableArea.h"

// for Tool Extensions
#include "Features/IModularFeatures.h"
#include "ModelingModeToolExtensions.h"

// for LOD setting
#include "EditorInteractiveToolsFrameworkModule.h"
#include "Tools/EditorComponentSourceFactory.h"
#include "ToolTargetManager.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorModeToolkit"


// if set to 1, then on mode initialization we include buttons for prototype modeling tools
static TAutoConsoleVariable<int32> CVarEnablePrototypeModelingTools(
	TEXT("modeling.EnablePrototypes"),
	0,
	TEXT("Enable unsupported Experimental prototype Modeling Tools"));
static TAutoConsoleVariable<int32> CVarEnablePolyModeling(
	TEXT("modeling.EnablePolyModel"),
	0,
	TEXT("Enable prototype PolyEdit tab"));



FModelingToolsEditorModeToolkit::FModelingToolsEditorModeToolkit()
{
}


FModelingToolsEditorModeToolkit::~FModelingToolsEditorModeToolkit()
{
	UModelingToolsEditorModeSettings* Settings = GetMutableDefault<UModelingToolsEditorModeSettings>();
	Settings->OnModified.Remove(AssetSettingsModifiedHandle);
	GetScriptableEditorMode()->GetInteractiveToolsContext()->OnToolNotificationMessage.RemoveAll(this);
	GetScriptableEditorMode()->GetInteractiveToolsContext()->OnToolWarningMessage.RemoveAll(this);
}


void FModelingToolsEditorModeToolkit::CustomizeModeDetailsViewArgs(FDetailsViewArgs& ArgsInOut)
{
	//ArgsInOut.ColumnWidth = 0.3f;
}

void FModelingToolsEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	// Have to create the ToolkitWidget here because FModeToolkit::Init() is going to ask for it and add
	// it to the Mode panel, and not ask again afterwards. However we have to call Init() to get the 
	// ModeDetailsView created, that we need to add to the ToolkitWidget. So, we will create the Widget
	// here but only add the rows to it after we call Init()
	TSharedPtr<SVerticalBox> ToolkitWidgetVBox = SNew(SVerticalBox);
	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(4)
		[
			ToolkitWidgetVBox->AsShared()
		];

	FModeToolkit::Init(InitToolkitHost, InOwningMode);

	GetToolkitHost()->OnActiveViewportChanged().AddSP(this, &FModelingToolsEditorModeToolkit::OnActiveViewportChanged);

	ModeWarningArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f)));
	ModeWarningArea->SetText(FText::GetEmpty());
	ModeWarningArea->SetVisibility(EVisibility::Collapsed);

	ModeHeaderArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	ModeHeaderArea->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
	ModeHeaderArea->SetJustification(ETextJustify::Center);


	ToolWarningArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f)));
	ToolWarningArea->SetText(FText::GetEmpty());

	// add the various sections to the mode toolbox
	ToolkitWidgetVBox->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
		[
			ModeWarningArea->AsShared()
		];
	ToolkitWidgetVBox->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
		[
			ModeHeaderArea->AsShared()
		];
	ToolkitWidgetVBox->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
		[
			ToolWarningArea->AsShared()
		];
	ToolkitWidgetVBox->AddSlot().HAlign(HAlign_Fill).FillHeight(1.f)
		[
			ModeDetailsView->AsShared()
		];
	ToolkitWidgetVBox->AddSlot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
		[
			MakeAssetConfigPanel()->AsShared()
		];

	ClearNotification();
	ClearWarning();

	ActiveToolName = FText::GetEmpty();
	ActiveToolMessage = FText::GetEmpty();

	GetScriptableEditorMode()->GetInteractiveToolsContext()->OnToolNotificationMessage.AddSP(this, &FModelingToolsEditorModeToolkit::PostNotification);
	GetScriptableEditorMode()->GetInteractiveToolsContext()->OnToolWarningMessage.AddSP(this, &FModelingToolsEditorModeToolkit::PostWarning);

	SAssignNew(ViewportOverlayWidget, SHorizontalBox)

	+SHorizontalBox::Slot()
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Bottom)
	.Padding(FMargin(0.0f, 0.0f, 0.f, 15.f))
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewport.OverlayBrush"))
		.Padding(8.f)
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SImage)
				.Image_Lambda([this] () { return ActiveToolIcon; })
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(STextBlock)
				.Text(this, &FModelingToolsEditorModeToolkit::GetActiveToolDisplayName)
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.0, 0.f, 2.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.TextStyle( FAppStyle::Get(), "DialogButtonText" )
				.Text(LOCTEXT("OverlayAccept", "Accept"))
				.ToolTipText(LOCTEXT("OverlayAcceptTooltip", "Accept/Commit the results of the active Tool [Enter]"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Accept); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanAcceptActiveTool(); })
				.Visibility_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->ActiveToolHasAccept() ? EVisibility::Visible : EVisibility::Collapsed; })
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Button")
				.TextStyle( FAppStyle::Get(), "DialogButtonText" )
				.Text(LOCTEXT("OverlayCancel", "Cancel"))
				.ToolTipText(LOCTEXT("OverlayCancelTooltip", "Cancel the active Tool [Esc]"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Cancel); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanCancelActiveTool(); })
				.Visibility_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->ActiveToolHasAccept() ? EVisibility::Visible : EVisibility::Collapsed; })
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.0, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.TextStyle( FAppStyle::Get(), "DialogButtonText" )
				.Text(LOCTEXT("OverlayComplete", "Complete"))
				.ToolTipText(LOCTEXT("OverlayCompleteTooltip", "Exit the active Tool [Enter]"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { GetScriptableEditorMode()->GetInteractiveToolsContext()->EndTool(EToolShutdownType::Completed); return FReply::Handled(); })
				.IsEnabled_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanCompleteActiveTool(); })
				.Visibility_Lambda([this]() { return GetScriptableEditorMode()->GetInteractiveToolsContext()->CanCompleteActiveTool() ? EVisibility::Visible : EVisibility::Collapsed; })
			]
		]	
	];

}



TSharedPtr<SWidget> FModelingToolsEditorModeToolkit::MakeAssetConfigPanel()
{
	//
	// New Asset Location drop-down
	//

	AssetLocationModes.Add(MakeShared<FString>(TEXT("AutoGen Folder (World-Relative)")));
	AssetLocationModes.Add(MakeShared<FString>(TEXT("AutoGen Folder (Global)")));
	AssetLocationModes.Add(MakeShared<FString>(TEXT("Current Folder")));
	AssetLocationMode = SNew(STextComboBox)
		.OptionsSource(&AssetLocationModes)
		.OnSelectionChanged_Lambda([&](TSharedPtr<FString> String, ESelectInfo::Type) { UpdateAssetLocationMode(String); });
	AssetSaveModes.Add(MakeShared<FString>(TEXT("AutoSave New Assets")));
	AssetSaveModes.Add(MakeShared<FString>(TEXT("Manual Save")));
	AssetSaveModes.Add(MakeShared<FString>(TEXT("Interactive")));
	AssetSaveMode = SNew(STextComboBox)
		.OptionsSource(&AssetSaveModes)
		.OnSelectionChanged_Lambda([&](TSharedPtr<FString> String, ESelectInfo::Type) { UpdateAssetSaveMode(String); });
	
	// initialize combos
	UpdateAssetPanelFromSettings();
	
	// register callback
	UModelingToolsEditorModeSettings* Settings = GetMutableDefault<UModelingToolsEditorModeSettings>();
	AssetSettingsModifiedHandle = Settings->OnModified.AddLambda([this](UObject*, FProperty*) { OnAssetSettingsModified(); });


	//
	// LOD selection dropdown
	//

	AssetLODModes.Add(MakeShared<FString>(TEXT("Max Available")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("HiRes")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD0")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD1")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD2")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD3")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD4")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD5")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD6")));
	AssetLODModes.Add(MakeShared<FString>(TEXT("LOD7")));
	AssetLODMode = SNew(STextComboBox)
		.OptionsSource(&AssetLODModes)
		.OnSelectionChanged_Lambda([&](TSharedPtr<FString> String, ESelectInfo::Type)
	{
		EStaticMeshEditingLOD NewSelectedLOD = EStaticMeshEditingLOD::LOD0;
		if (*String == *AssetLODModes[0])
		{
			NewSelectedLOD = EStaticMeshEditingLOD::MaxQuality;
		}
		else if (*String == *AssetLODModes[1])
		{
			NewSelectedLOD = EStaticMeshEditingLOD::HiResSource;
		}
		else
		{
			for (int32 k = 2; k < AssetLODModes.Num(); ++k)
			{
				if (*String == *AssetLODModes[k])
				{
					NewSelectedLOD = (EStaticMeshEditingLOD)(k - 2);
					break;
				}
			}
		}

		if (FEditorInteractiveToolsFrameworkGlobals::RegisteredStaticMeshTargetFactoryKey >= 0)
		{
			FComponentTargetFactory* Factory = FindComponentTargetFactoryByKey(FEditorInteractiveToolsFrameworkGlobals::RegisteredStaticMeshTargetFactoryKey);
			if (Factory != nullptr)
			{
				FStaticMeshComponentTargetFactory* StaticMeshFactory = static_cast<FStaticMeshComponentTargetFactory*>(Factory);
				StaticMeshFactory->CurrentEditingLOD = NewSelectedLOD;
			}
		}

		TObjectPtr<UToolTargetManager> TargetManager = GetScriptableEditorMode()->GetInteractiveToolsContext()->TargetManager;
		UStaticMeshComponentToolTargetFactory* StaticMeshTargetFactory = TargetManager->FindFirstFactoryByType<UStaticMeshComponentToolTargetFactory>();
		if (StaticMeshTargetFactory)
		{
			StaticMeshTargetFactory->SetActiveEditingLOD(NewSelectedLOD);
		}

	});


	// initialize combos
	//UpdateAssetPanelFromSettings();
	AssetLODMode->SetSelectedItem(AssetLODModes[0]);		// TODO read from settings?


	AssetLODModeLabel = SNew(STextBlock).Text(LOCTEXT("ActiveLODLabel", "Editing LOD"));

	TSharedPtr<SVerticalBox> Content = SNew(SVerticalBox)
	+ SVerticalBox::Slot().MaxHeight(20).HAlign(HAlign_Fill)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(0, 2, 2, 2).HAlign(HAlign_Right).VAlign(VAlign_Center).AutoWidth()
				[ AssetLODModeLabel->AsShared() ]
			+ SHorizontalBox::Slot().Padding(0).FillWidth(4.0f)
				[ AssetLODMode->AsShared() ]
		]

	+ SVerticalBox::Slot().MaxHeight(20).HAlign(HAlign_Fill).Padding(0, 3, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(0, 2, 2, 2).HAlign(HAlign_Right).VAlign(VAlign_Center).AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetLocationLabel", "New Asset Location"))
				]
			+ SHorizontalBox::Slot().Padding(0).FillWidth(4.0f)
				[
					AssetLocationMode->AsShared()
				]

			+ SHorizontalBox::Slot().HAlign(HAlign_Right).Padding(5, 0, 0, 0).FillWidth(1.0f)
				[
					SNew(SBox).MaxDesiredHeight(16).MaxDesiredWidth(17)
					[
						SNew(SButton)
						.ContentPadding(0)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
						.OnClicked_Lambda( [this]() {OnShowAssetSettings(); return FReply::Handled(); } )
						[
							SNew(SImage).Image(FSlateIcon(FEditorStyle::GetStyleSetName(), "FoliageEditMode.Settings").GetIcon())
						]
					]
				]
		];


	TSharedPtr<SExpandableArea> AssetConfigPanel = SNew(SExpandableArea)
		.HeaderPadding(FMargin(2.0f))
		.Padding(FMargin(2.f))
		.BorderImage(FEditorStyle::Get().GetBrush("DetailsView.CategoryTop"))
		.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
		.BodyBorderBackgroundColor(FLinearColor::Transparent)
		.AreaTitleFont(FEditorStyle::Get().GetFontStyle("EditorModesPanel.CategoryFontStyle"))
		.BodyContent()
		[
			Content->AsShared()
		]
		.HeaderContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ModelingSettingsPanelHeader", "Modeling Mode Quick Settings"))
			.Justification(ETextJustify::Center)
			.Font(FEditorStyle::Get().GetFontStyle("EditorModesPanel.CategoryFontStyle"))
		];

	return AssetConfigPanel;

}




void FModelingToolsEditorModeToolkit::UpdateActiveToolProperties()
{
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
	if (CurTool != nullptr)
	{
		ModeDetailsView->SetObjects(CurTool->GetToolProperties(true));
	}
}

void FModelingToolsEditorModeToolkit::RefreshPropertyObject(UObject* ChangedObject)
{
	ModeDetailsView->ForceRefresh();
}


void FModelingToolsEditorModeToolkit::PostNotification(const FText& Message)
{
	ClearNotification();

	ActiveToolMessage = Message;

	const FName LevelEditorStatusBarName = "LevelEditor.StatusBar";
	ActiveToolMessageHandle = GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PushStatusBarMessage(LevelEditorStatusBarName, ActiveToolMessage);
}

void FModelingToolsEditorModeToolkit::ClearNotification()
{
	ActiveToolMessage = FText::GetEmpty();

	const FName LevelEditorStatusBarName = "LevelEditor.StatusBar";
	GEditor->GetEditorSubsystem<UStatusBarSubsystem>()->PopStatusBarMessage(LevelEditorStatusBarName, ActiveToolMessageHandle);
	ActiveToolMessageHandle.Reset();
}


void FModelingToolsEditorModeToolkit::PostWarning(const FText& Message)
{
	ToolWarningArea->SetText(Message);
	ToolWarningArea->SetVisibility(EVisibility::Visible);
}

void FModelingToolsEditorModeToolkit::ClearWarning()
{
	ToolWarningArea->SetText(FText());
	ToolWarningArea->SetVisibility(EVisibility::Collapsed);
}



FName FModelingToolsEditorModeToolkit::GetToolkitFName() const
{
	return FName("ModelingToolsEditorMode");
}

FText FModelingToolsEditorModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("ModelingToolsEditorModeToolkit", "DisplayName", "ModelingToolsEditorMode Tool");
}

static const FName PrimitiveTabName(TEXT("Shapes"));
static const FName CreateTabName(TEXT("Create"));
static const FName AttributesTabName(TEXT("Attributes"));
static const FName TriModelingTabName(TEXT("TriModel"));
static const FName PolyModelingTabName(TEXT("PolyModel"));
static const FName MeshProcessingTabName(TEXT("MeshOps"));
static const FName UVTabName(TEXT("UVs"));
static const FName TransformTabName(TEXT("Transform"));
static const FName DeformTabName(TEXT("Deform"));
static const FName VolumesTabName(TEXT("Volumes"));
static const FName PrototypesTabName(TEXT("Prototypes"));
static const FName HairTabName(TEXT("Hair"));
static const FName PolyEditTabName(TEXT("PolyEdit"));
static const FName VoxToolsTabName(TEXT("VoxOps"));
static const FName LODToolsTabName(TEXT("LODs"));


const TArray<FName> FModelingToolsEditorModeToolkit::PaletteNames_Standard = { PrimitiveTabName, CreateTabName, PolyModelingTabName, TriModelingTabName, DeformTabName, TransformTabName, MeshProcessingTabName, VoxToolsTabName, AttributesTabName, UVTabName, VolumesTabName, LODToolsTabName, HairTabName };


void FModelingToolsEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames = PaletteNames_Standard;

	TArray<FName> ExistingNames;
	for ( FName Name : PaletteNames )
	{
		ExistingNames.Add(Name);
	}


	bool bEnablePrototypes = (CVarEnablePrototypeModelingTools.GetValueOnGameThread() > 0);
	if (bEnablePrototypes)
	{
		PaletteNames.Add(PrototypesTabName);
		ExistingNames.Add(PrototypesTabName);
	}

	bool bEnablePolyModel = (CVarEnablePolyModeling.GetValueOnGameThread() > 0);
	if (bEnablePolyModel)
	{
		PaletteNames.Add(PolyEditTabName);
		ExistingNames.Add(PolyEditTabName);
	}

	if (IModularFeatures::Get().IsModularFeatureAvailable(IModelingModeToolExtension::GetModularFeatureName()))
	{
		TArray<IModelingModeToolExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<IModelingModeToolExtension>(
			IModelingModeToolExtension::GetModularFeatureName());
		for (int32 k = 0; k < Extensions.Num(); ++k)
		{
			FText ExtensionName = Extensions[k]->GetExtensionName();
			FText SectionName = Extensions[k]->GetToolSectionName();
			FName SectionIndex(SectionName.ToString());
			if (ExistingNames.Contains(SectionIndex))
			{
				UE_LOG(LogTemp, Warning, TEXT("Modeling Mode Extension [%s] uses existing Section Name [%s] - buttons may not be visible"), *ExtensionName.ToString(), *SectionName.ToString());
			}
			else
			{
				PaletteNames.Add(SectionIndex);
				ExistingNames.Add(SectionIndex);
			}
		}
	}
}


FText FModelingToolsEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{ 
	return FText::FromName(Palette);
}

void FModelingToolsEditorModeToolkit::BuildToolPalette(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder) 
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();
	BuildToolPalette_Experimental(PaletteIndex, ToolbarBuilder);
}



void FModelingToolsEditorModeToolkit::BuildToolPalette_Standard(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder)
{
	check(false);
}


void FModelingToolsEditorModeToolkit::BuildToolPalette_Experimental(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder)
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();

	if (PaletteIndex == PrimitiveTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddBoxPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddSpherePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddCylinderPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddConePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddTorusPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddArrowPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddRectanglePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddDiscPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddStairsPrimitiveTool);
	}
	else if (PaletteIndex == CreateTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolygonTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolyPathTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawAndRevolveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRevolveBoundaryTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginCombineMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDuplicateMeshesTool);
	}
	else if (PaletteIndex == TransformTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAlignObjectsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditPivotTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginBakeTransformTool);
	}
	else if (PaletteIndex == DeformTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginSculptMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshSculptMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSmoothMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginOffsetMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSpaceDeformerTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginLatticeDeformerTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDisplaceMeshTool);
	}
	else if (PaletteIndex == MeshProcessingTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginSimplifyMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginWeldEdgesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemoveOccludedTrianglesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSelfUnionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginProjectToTargetTool);
	}
	else if (PaletteIndex == LODToolsTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginLODManagerTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGenerateStaticMeshLODAssetTool);
	}
	else if (PaletteIndex == VoxToolsTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelSolidifyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBlendTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMorphologyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBooleanTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMergeTool);
	}
	else if (PaletteIndex == TriModelingTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginTriEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginHoleFillTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPlaneCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMirrorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolygonCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshTrimTool);
	}
	else if (PaletteIndex == PolyModelingTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyDeformTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupEdgeInsertionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEdgeLoopInsertionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshBooleanTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginCutMeshWithMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSubdividePolyTool);
	}
	else if (PaletteIndex == AttributesTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditNormalsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditTangentsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyGroupsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshGroupPaintTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshAttributePaintTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditMeshMaterialsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginBakeMeshAttributeMapsTool);
	}
	else if (PaletteIndex == UVTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginGlobalUVGenerateTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupUVGenerateTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVProjectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVSeamEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformUVIslandsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVLayoutTool);
	}
	else if (PaletteIndex == VolumesTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginVolumeToMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshToVolumeTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginBspConversionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginPhysicsInspectorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSetCollisionGeometryTool);
		//ToolbarBuilder.AddToolBarButton(Commands.BeginEditCollisionGeometryTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginExtractCollisionGeometryTool);
	}
	else if (PaletteIndex == HairTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroomToMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroomCardsEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGenerateLODMeshesTool);
	}
	else if (PaletteIndex == PrototypesTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddPatchTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginShapeSprayTool);
	}
	else if (PaletteIndex == PolyEditTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_FaceSelect);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_EdgeSelect);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_VertexSelect);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_AllSelect);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_LoopSelect);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_RingSelect);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_Extrude);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_Offset);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_Inset);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_Outset);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyModelTool_CutFaces);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupEdgeInsertionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEdgeLoopInsertionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginSubdividePolyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyEditTool);
	}
	else
	{
		TArray<IModelingModeToolExtension*> Extensions = IModularFeatures::Get().GetModularFeatureImplementations<IModelingModeToolExtension>(
			IModelingModeToolExtension::GetModularFeatureName());
		for (int32 k = 0; k < Extensions.Num(); ++k)
		{
			FText SectionName = Extensions[k]->GetToolSectionName();
			FName SectionIndex(SectionName.ToString());
			if (PaletteIndex == SectionIndex)
			{
				FExtensionToolQueryInfo ExtensionQueryInfo;
				ExtensionQueryInfo.bIsInfoQueryOnly = true;
				TArray<FExtensionToolDescription> ToolSet;
				Extensions[k]->GetExtensionTools(ExtensionQueryInfo, ToolSet);
				for (const FExtensionToolDescription& ToolInfo : ToolSet)
				{
					ToolbarBuilder.AddToolBarButton(ToolInfo.ToolCommand);
				}
			}
		}
	}
}

void FModelingToolsEditorModeToolkit::OnToolPaletteChanged(FName PaletteName) 
{
}



void FModelingToolsEditorModeToolkit::EnableShowRealtimeWarning(bool bEnable)
{
	if (bShowRealtimeWarning != bEnable)
	{
		bShowRealtimeWarning = bEnable;
		UpdateShowWarnings();
	}
}

void FModelingToolsEditorModeToolkit::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	UpdateActiveToolProperties();

	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
	CurTool->OnPropertySetsModified.AddSP(this, &FModelingToolsEditorModeToolkit::UpdateActiveToolProperties);
	CurTool->OnPropertyModifiedDirectlyByTool.AddSP(this, &FModelingToolsEditorModeToolkit::RefreshPropertyObject);

	ModeHeaderArea->SetVisibility(EVisibility::Collapsed);
	ActiveToolName = CurTool->GetToolInfo().ToolDisplayName;

	// try to update icon
	FString ActiveToolIdentifier = GetScriptableEditorMode()->GetToolManager()->GetActiveToolName(EToolSide::Left);
	ActiveToolIdentifier.InsertAt(0, ".");
	FName ActiveToolIconName = ISlateStyle::Join(FModelingToolsManagerCommands::Get().GetContextName(), TCHAR_TO_ANSI(*ActiveToolIdentifier));
	ActiveToolIcon = FModelingToolsEditorModeStyle::Get()->GetOptionalBrush(ActiveToolIconName);


	GetToolkitHost()->AddViewportOverlayWidget(ViewportOverlayWidget.ToSharedRef());

	// disable LOD level picker once Tool is active
	AssetLODMode->SetEnabled(false);
	AssetLODModeLabel->SetEnabled(false);
}

void FModelingToolsEditorModeToolkit::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	if (IsHosted())
	{
		GetToolkitHost()->RemoveViewportOverlayWidget(ViewportOverlayWidget.ToSharedRef());
	}

	ModeDetailsView->SetObject(nullptr);
	ActiveToolName = FText::GetEmpty();
	ModeHeaderArea->SetVisibility(EVisibility::Visible);
	ModeHeaderArea->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
	ClearNotification();
	ClearWarning();
	UInteractiveTool* CurTool = GetScriptableEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
	if ( CurTool )
	{
		CurTool->OnPropertySetsModified.RemoveAll(this);
		CurTool->OnPropertyModifiedDirectlyByTool.RemoveAll(this);
	}

	// re-enable LOD level picker
	AssetLODMode->SetEnabled(true);
	AssetLODModeLabel->SetEnabled(true);
}

void FModelingToolsEditorModeToolkit::OnActiveViewportChanged(TSharedPtr<IAssetViewport> OldViewport, TSharedPtr<IAssetViewport> NewViewport)
{
	// Only worry about handling this notification if Modeling has an active tool
	if (!ActiveToolName.IsEmpty())
	{
		// Check first to see if this changed because the old viewport was deleted and if not, remove our hud
		if (OldViewport)	
		{
			GetToolkitHost()->RemoveViewportOverlayWidget(ViewportOverlayWidget.ToSharedRef(), OldViewport);
		}

		// Add the hud to the new viewport
		GetToolkitHost()->AddViewportOverlayWidget(ViewportOverlayWidget.ToSharedRef(), NewViewport);
	}
}

void FModelingToolsEditorModeToolkit::UpdateShowWarnings()
{
	if (bShowRealtimeWarning )
	{
		if (ModeWarningArea->GetVisibility() == EVisibility::Collapsed)
		{
			ModeWarningArea->SetText(LOCTEXT("ModelingModeToolkitRealtimeWarning", "Realtime Mode is required for Modeling Tools to work correctly. Please enable Realtime Mode in the Viewport Options or with the Ctrl+r hotkey."));
			ModeWarningArea->SetVisibility(EVisibility::Visible);
		}
	}
	else
	{
		ModeWarningArea->SetText(FText());
		ModeWarningArea->SetVisibility(EVisibility::Collapsed);
	}

}


void FModelingToolsEditorModeToolkit::UpdateAssetLocationMode(TSharedPtr<FString> NewString)
{
	UModelingToolsEditorModeSettings* Settings = GetMutableDefault<UModelingToolsEditorModeSettings>();
	if (NewString == AssetLocationModes[0])
	{
		Settings->AssetGenerationLocation = EModelingModeAssetGenerationLocation::AutoGeneratedWorldRelativeAssetPath;
	}
	if (NewString == AssetLocationModes[1])
	{
		Settings->AssetGenerationLocation = EModelingModeAssetGenerationLocation::AutoGeneratedGlobalAssetPath;
	}
	else if ( NewString == AssetLocationModes[2])
	{
		Settings->AssetGenerationLocation = EModelingModeAssetGenerationLocation::CurrentAssetBrowserPathIfAvailable;
	}
	else
	{
		Settings->AssetGenerationLocation = EModelingModeAssetGenerationLocation::AutoGeneratedWorldRelativeAssetPath;
	}

	Settings->SaveConfig();
}

void FModelingToolsEditorModeToolkit::UpdateAssetSaveMode(TSharedPtr<FString> NewString)
{
	UModelingToolsEditorModeSettings* Settings = GetMutableDefault<UModelingToolsEditorModeSettings>();
	if (NewString == AssetSaveModes[0])
	{
		Settings->AssetGenerationMode = EModelingModeAssetGenerationBehavior::AutoGenerateAndAutosave;
	}
	else if (NewString == AssetSaveModes[1])
	{
		Settings->AssetGenerationMode = EModelingModeAssetGenerationBehavior::AutoGenerateButDoNotAutosave;
	}
	else if (NewString == AssetSaveModes[2])
	{
		Settings->AssetGenerationMode = EModelingModeAssetGenerationBehavior::InteractivePromptToSave;
	}
	else
	{
		Settings->AssetGenerationMode = EModelingModeAssetGenerationBehavior::AutoGenerateButDoNotAutosave;
	}

	Settings->SaveConfig();
}

void FModelingToolsEditorModeToolkit::UpdateAssetPanelFromSettings()
{
	const UModelingToolsEditorModeSettings* Settings = GetDefault<UModelingToolsEditorModeSettings>();

	switch (Settings->AssetGenerationLocation)
	{
	case EModelingModeAssetGenerationLocation::CurrentAssetBrowserPathIfAvailable:
		AssetLocationMode->SetSelectedItem(AssetLocationModes[2]);
		break;
	case EModelingModeAssetGenerationLocation::AutoGeneratedGlobalAssetPath:
		AssetLocationMode->SetSelectedItem(AssetLocationModes[1]);
		break;
	case EModelingModeAssetGenerationLocation::AutoGeneratedWorldRelativeAssetPath:
	default:
		AssetLocationMode->SetSelectedItem(AssetLocationModes[0]);
		break;
	}

	switch (Settings->AssetGenerationMode)
	{
	case EModelingModeAssetGenerationBehavior::AutoGenerateButDoNotAutosave:
		AssetSaveMode->SetSelectedItem(AssetSaveModes[1]);
		break;
	case EModelingModeAssetGenerationBehavior::InteractivePromptToSave:
		AssetSaveMode->SetSelectedItem(AssetSaveModes[2]);
		break;
	default:
		AssetSaveMode->SetSelectedItem(AssetSaveModes[0]);
		break;
	}
}


void FModelingToolsEditorModeToolkit::OnAssetSettingsModified()
{
	UpdateAssetPanelFromSettings();
}

void FModelingToolsEditorModeToolkit::OnShowAssetSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Project", "Plugins", "ModelingMode");
	}
}


#undef LOCTEXT_NAMESPACE
