// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorModeToolkit.h"
#include "ModelingToolsEditorMode.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingToolsEditorModeSettings.h"
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

#define LOCTEXT_NAMESPACE "FModelingToolsEditorModeToolkit"


// if set to 1, then on mode initialization we include buttons for prototype modeling tools
static TAutoConsoleVariable<int32> CVarEnablePrototypeModelingTools(
	TEXT("modeling.EnablePrototypes"),
	0,
	TEXT("Enable unsupported Experimental prototype Modeling Tools"));


FModelingToolsEditorModeToolkit::FModelingToolsEditorModeToolkit()
{
}


FModelingToolsEditorModeToolkit::~FModelingToolsEditorModeToolkit()
{
	UModelingToolsEditorModeSettings* Settings = GetMutableDefault<UModelingToolsEditorModeSettings>();
	Settings->OnModified.Remove(AssetSettingsModifiedHandle);
}

void FModelingToolsEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ false,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

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


	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
				[
					ModeWarningArea->AsShared()
				]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
				[
					ModeHeaderArea->AsShared()
				]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).AutoHeight().Padding(5)
				[
					ToolWarningArea->AsShared()
				]

			+ SVerticalBox::Slot().HAlign(HAlign_Fill).FillHeight(1.f)
				[
					DetailsView->AsShared()
				]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
				[
					MakeAssetConfigPanel()->AsShared()
				]
		];
		
	FModeToolkit::Init(InitToolkitHost);

	ClearNotification();
	ClearWarning();

	ActiveToolName = FText::GetEmpty();
	ActiveToolMessage = FText::GetEmpty();

	GetToolsEditorMode()->GetToolManager()->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		UpdateActiveToolProperties();

		UInteractiveTool* CurTool = GetToolsEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
		CurTool->OnPropertySetsModified.AddLambda([this]() { UpdateActiveToolProperties(); });

		ModeHeaderArea->SetVisibility(EVisibility::Collapsed);
		ActiveToolName = CurTool->GetToolInfo().ToolDisplayName;
	});
	GetToolsEditorMode()->GetToolManager()->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		DetailsView->SetObject(nullptr);
		ActiveToolName = FText::GetEmpty();
		ModeHeaderArea->SetVisibility(EVisibility::Visible);
		ModeHeaderArea->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
		ClearNotification();
		ClearWarning();
	});


	GetToolsEditorMode()->OnToolNotificationMessage.AddLambda([this](const FText& Message)
	{
		PostNotification(Message);
	});
	GetToolsEditorMode()->OnToolWarningMessage.AddLambda([this](const FText& Message)
	{
		PostWarning(Message);
	});
}



TSharedPtr<SWidget> FModelingToolsEditorModeToolkit::MakeAssetConfigPanel()
{
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


	TSharedPtr<SVerticalBox> Content = SNew(SVerticalBox)
	//+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0)
	//	[
	//		SNew(SSeparator)
	//	]
	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(0, 4, 0, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(0).HAlign(HAlign_Left).VAlign(VAlign_Center).AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetPanelHeaderLabel", "Generated Asset Settings"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
			+ SHorizontalBox::Slot().HAlign(HAlign_Right).Padding(0).FillWidth(1.0f)
				[
					SNew(SBox).MaxDesiredHeight(16).MaxDesiredWidth(17)
					[
						SNew(SButton)
						.ContentPadding(0)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Default")
						.OnClicked_Lambda( [this]() {OnShowAssetSettings(); return FReply::Handled(); } )
						[
							SNew(SImage)
							.Image(FSlateIcon(FEditorStyle::GetStyleSetName(), "FoliageEditMode.Settings").GetIcon())
						]
					]
				]
		]
	+ SVerticalBox::Slot().MaxHeight(20).HAlign(HAlign_Fill).Padding(0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().Padding(0, 2, 2, 2).HAlign(HAlign_Right).VAlign(VAlign_Center).AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetLocationLabel", "Location"))
				]
			+ SHorizontalBox::Slot().Padding(0).FillWidth(4.0f)
				[
					AssetLocationMode->AsShared()
				]
			+ SHorizontalBox::Slot().Padding(10, 2, 2, 2).HAlign(HAlign_Right).VAlign(VAlign_Center).AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetSaveModeLabel", "Save Mode"))
				]
			+ SHorizontalBox::Slot().Padding(0).FillWidth(3.0f)
				[
					AssetSaveMode->AsShared()
				]
		];

	//return Content;


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
	UInteractiveTool* CurTool = GetToolsEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
	if (CurTool != nullptr)
	{
		DetailsView->SetObjects(CurTool->GetToolProperties(true));
	}
}


void FModelingToolsEditorModeToolkit::PostNotification(const FText& Message)
{
	ActiveToolMessage = Message;
}

void FModelingToolsEditorModeToolkit::ClearNotification()
{
	ActiveToolMessage = FText::GetEmpty();
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

class FEdMode* FModelingToolsEditorModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FModelingToolsEditorMode::EM_ModelingToolsEditorModeId);
}

FModelingToolsEditorMode * FModelingToolsEditorModeToolkit::GetToolsEditorMode() const
{
	return (FModelingToolsEditorMode *)GetEditorMode();
}

UEdModeInteractiveToolsContext* FModelingToolsEditorModeToolkit::GetToolsContext() const
{
	return GetToolsEditorMode()->GetToolsContext();
}

static const FName PrimitiveTabName(TEXT("Primitives"));
static const FName CreateTabName(TEXT("Create"));
static const FName EditTabName(TEXT("Edit"));
static const FName SculptTabName(TEXT("Sculpt"));
static const FName TrianglesTabName(TEXT("Triangles"));
static const FName PolyGroupsTabName(TEXT("PolyGroups"));
static const FName UVNormalTabName(TEXT("UVs/Normals"));
static const FName TransformTabName(TEXT("Transform"));
static const FName DeformTabName(TEXT("Deform"));
static const FName VolumesTabName(TEXT("Volumes"));
static const FName PrototypesTabName(TEXT("Prototypes"));
static const FName HairTabName(TEXT("Hair"));


const TArray<FName> FModelingToolsEditorModeToolkit::PaletteNames_Standard = { PrimitiveTabName, CreateTabName, TransformTabName, DeformTabName, PolyGroupsTabName, TrianglesTabName, UVNormalTabName, VolumesTabName, HairTabName };
const TArray<FName> FModelingToolsEditorModeToolkit::PaletteNames_Experimental = { PrimitiveTabName, CreateTabName, TransformTabName, DeformTabName, PolyGroupsTabName, TrianglesTabName, UVNormalTabName, VolumesTabName, HairTabName, PrototypesTabName };


void FModelingToolsEditorModeToolkit::GetToolPaletteNames(TArray<FName>& InPaletteName) const
{
	bool bEnablePrototypes = (CVarEnablePrototypeModelingTools.GetValueOnGameThread() > 0);
	InPaletteName = (bEnablePrototypes) ? PaletteNames_Experimental : PaletteNames_Standard;
}


FText FModelingToolsEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) const
{ 
	return FText::FromName(Palette);
}

void FModelingToolsEditorModeToolkit::BuildToolPalette(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder) 
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();
	bool bEnablePrototypes = (CVarEnablePrototypeModelingTools.GetValueOnGameThread() > 0);
	if (bEnablePrototypes)
	{
		BuildToolPalette_Experimental(PaletteIndex, ToolbarBuilder);
	}
	else
	{
		BuildToolPalette_Standard(PaletteIndex, ToolbarBuilder);
	}
}



void FModelingToolsEditorModeToolkit::BuildToolPalette_Standard(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder)
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();

	ToolbarBuilder.AddToolBarButton(Commands.AcceptActiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.CancelActiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.CompleteActiveTool);

	ToolbarBuilder.AddSeparator();


	if (PaletteIndex == PrimitiveTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddBoxPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddCylinderPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddConePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddArrowPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddRectanglePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddRoundedRectanglePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddDiscPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddPuncturedDiscPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddTorusPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddSpherePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddSphericalBoxPrimitiveTool);
	}
	else if (PaletteIndex == CreateTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolygonTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolyPathTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawAndRevolveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRevolveBoundaryTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshBooleanTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSelfUnionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelSolidifyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBlendTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMorphologyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBooleanTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMergeTool);
	}
	else if (PaletteIndex == TransformTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAlignObjectsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditPivotTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginBakeTransformTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginCombineMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDuplicateMeshesTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == DeformTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginSculptMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshSculptMeshTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginPlaneCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMirrorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolygonCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSmoothMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginOffsetMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDisplaceMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSpaceDeformerTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshAttributePaintTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == TrianglesTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginTriEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSimplifyMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginProjectToTargetTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditMeshMaterialsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginWeldEdgesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemoveOccludedTrianglesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginHoleFillTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == PolyGroupsTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyGroupsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyDeformTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupEdgeInsertionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEdgeLoopInsertionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == UVNormalTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyGroupsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditNormalsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditTangentsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginGlobalUVGenerateTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupUVGenerateTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVProjectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVSeamEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformUVIslandsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVLayoutTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginBakeMeshAttributeMapsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
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
	ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
	ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
	ToolbarBuilder.AddSeparator();
	ToolbarBuilder.AddToolBarButton(Commands.BeginGroomToMeshTool);
	ToolbarBuilder.AddToolBarButton(Commands.BeginGroomCardsEditorTool);
	ToolbarBuilder.AddToolBarButton(Commands.BeginGenerateLODMeshesTool);
	ToolbarBuilder.AddSeparator();
	ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
	ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
}


void FModelingToolsEditorModeToolkit::BuildToolPalette_Experimental(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder)
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();

	ToolbarBuilder.AddToolBarButton(Commands.AcceptActiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.CancelActiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.CompleteActiveTool);

	ToolbarBuilder.AddSeparator();

	if (PaletteIndex == PrimitiveTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddBoxPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddCylinderPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddConePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddArrowPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddRectanglePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddRoundedRectanglePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddDiscPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddPuncturedDiscPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddTorusPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddSpherePrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddSphericalBoxPrimitiveTool);
	}
	else if (PaletteIndex == CreateTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolygonTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolyPathTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawAndRevolveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRevolveBoundaryTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshBooleanTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSelfUnionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelSolidifyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBlendTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMorphologyTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBooleanTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMergeTool);
	}
	else if (PaletteIndex == TransformTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAlignObjectsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditPivotTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginBakeTransformTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginCombineMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDuplicateMeshesTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == DeformTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginSculptMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshSculptMeshTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginPlaneCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMirrorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolygonCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSmoothMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginOffsetMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDisplaceMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSpaceDeformerTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshAttributePaintTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == TrianglesTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginTriEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSimplifyMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginProjectToTargetTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditMeshMaterialsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginWeldEdgesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemoveOccludedTrianglesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginHoleFillTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == PolyGroupsTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyGroupsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyDeformTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupEdgeInsertionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEdgeLoopInsertionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == UVNormalTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyGroupsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditNormalsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditTangentsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginGlobalUVGenerateTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroupUVGenerateTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVProjectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVSeamEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformUVIslandsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVLayoutTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginBakeMeshAttributeMapsTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
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
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroomToMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGroomCardsEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginGenerateLODMeshesTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
	}
	else if (PaletteIndex == PrototypesTabName)
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddSeparator();
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddPatchTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginShapeSprayTool);
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
