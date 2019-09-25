// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorModeToolkit.h"
#include "ModelingToolsEditorMode.h"
#include "ModelingToolsManagerActions.h"
#include "Engine/Selection.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "IDetailRootObjectCustomization.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorModeToolkit"


// if set to 1, then on mode initialization we include buttons for prototype modeling tools
static TAutoConsoleVariable<int32> CVarEnablePrototypeModelingTools(
	TEXT("modeling.EnablePrototypes"),
	0,
	TEXT("Enable unsupported Experimental prototype Modeling Tools"));


FModelingToolsEditorModeToolkit::FModelingToolsEditorModeToolkit()
{
}



/**
 * Customization for Tool properties multi-object DetailsView, that just hides per-object header
 */
class FModelingToolsDetailRootObjectCustomization : public IDetailRootObjectCustomization
{
public:
	FModelingToolsDetailRootObjectCustomization()
	{
	}

	virtual TSharedPtr<SWidget> CustomizeObjectHeader(const UObject* InRootObject) override
	{
		return SNew(STextBlock).Text( FText::FromString(InRootObject->GetName())  );
	}

	virtual bool IsObjectVisible(const UObject* InRootObject) const override
	{
		return true;
	}

	virtual bool ShouldDisplayHeader(const UObject* InRootObject) const override
	{
		return false;
	}
};



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

	// add customization that hides header labels
	TSharedPtr<FModelingToolsDetailRootObjectCustomization> RootObjectCustomization 
		= MakeShared<FModelingToolsDetailRootObjectCustomization>();
	DetailsView->SetRootObjectCustomizationInstance(RootObjectCustomization);

	ToolHeaderLabel = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	ToolHeaderLabel->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
	ToolHeaderLabel->SetJustification(ETextJustify::Center);

	//const FTextBlockStyle DefaultText = FTextBlockStyle()
	//	.SetFont(DEFAULT_FONT("Bold", 10));

	ToolMessageArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor::White * 0.7f));
	ToolMessageArea->SetText(LOCTEXT("ToolMessageLabel", ""));

	ToolWarningArea = SNew(STextBlock)
		.AutoWrapText(true)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.15f, 0.15f)));
	ToolWarningArea->SetText(LOCTEXT("ToolMessageLabel", ""));


	

	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
				[
					ToolHeaderLabel->AsShared()
				]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).AutoHeight().Padding(10, 10, 10, 15)
				[
					ToolMessageArea->AsShared()
				]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).AutoHeight().Padding(10, 5, 10, 15)
				[
					ToolWarningArea->AsShared()
				]

			+ SVerticalBox::Slot().HAlign(HAlign_Fill).FillHeight(1.f)
				[
					DetailsView->AsShared()
				]

		];
		
	FModeToolkit::Init(InitToolkitHost);

	ClearNotification();
	ClearWarning();


	GetToolsEditorMode()->GetToolManager()->OnToolStarted.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		// Update properties panel
		UInteractiveTool* CurTool = GetToolsEditorMode()->GetToolManager()->GetActiveTool(EToolSide::Left);
		DetailsView->SetObjects(CurTool->GetToolProperties());

		ToolHeaderLabel->SetText(CurTool->GetClass()->GetDisplayNameText());
		//ToolHeaderLabel->SetText(FString("(Tool Name Here)"));
	});
	GetToolsEditorMode()->GetToolManager()->OnToolEnded.AddLambda([this](UInteractiveToolManager* Manager, UInteractiveTool* Tool)
	{
		DetailsView->SetObject(nullptr);
		ToolHeaderLabel->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
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



void FModelingToolsEditorModeToolkit::PostNotification(const FText& Message)
{
	ToolMessageArea->SetText(Message);
	ToolMessageArea->SetVisibility(EVisibility::Visible);
}

void FModelingToolsEditorModeToolkit::ClearNotification()
{
	ToolMessageArea->SetText(FText());
	ToolMessageArea->SetVisibility(EVisibility::Collapsed);
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


const TArray<FName> FModelingToolsEditorModeToolkit::PaletteNames_Standard = { FName(TEXT("Modeling")), FName(TEXT("Utilities")) };
const TArray<FName> FModelingToolsEditorModeToolkit::PaletteNames_Experimental = { FName(TEXT("Create")), FName(TEXT("Edit")), FName(TEXT("UVs/Normals")), FName(TEXT("Utilities")) };


void FModelingToolsEditorModeToolkit::GetToolPaletteNames(TArray<FName>& InPaletteName) const
{
	bool bEnablePrototypes = (CVarEnablePrototypeModelingTools.GetValueOnGameThread() > 0);
	InPaletteName = (bEnablePrototypes) ? PaletteNames_Experimental : PaletteNames_Standard;
}


FText FModelingToolsEditorModeToolkit::GetToolPaletteDisplayName(FName Palette) 
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

	if (PaletteIndex == FName(TEXT("Modeling")))
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolygonTool);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSmoothMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDisplaceMeshTool);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.BeginSculptMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshSculptMeshTool);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.BeginSimplifyMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPlaneCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMergeTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBooleanTool);

	}
	else if (PaletteIndex == FName(TEXT("Utilities")))
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVProjectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditNormalsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginWeldEdgesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
	}
}

void FModelingToolsEditorModeToolkit::BuildToolPalette_Experimental(FName PaletteIndex, class FToolBarBuilder& ToolbarBuilder)
{
	const FModelingToolsManagerCommands& Commands = FModelingToolsManagerCommands::Get();

	ToolbarBuilder.AddToolBarButton(Commands.AcceptActiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.CancelActiveTool);
	ToolbarBuilder.AddToolBarButton(Commands.CompleteActiveTool);

	ToolbarBuilder.AddSeparator();


	if (PaletteIndex == FName(TEXT("Create")) )
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginAddPrimitiveTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDrawPolygonTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginShapeSprayTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelMergeTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginVoxelBooleanTool);
	} 
	else if ( PaletteIndex == FName(TEXT("Edit")) ) 
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginTransformMeshesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSelectionTool);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.BeginSculptMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshSculptMeshTool);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyEditTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSmoothMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginDisplaceMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshSpaceDeformerTool);

		ToolbarBuilder.AddSeparator();

		ToolbarBuilder.AddToolBarButton(Commands.BeginPlaneCutTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolygonOnMeshTool);

	}
	else if (PaletteIndex == FName(TEXT("UVs/Normals")))
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginUVProjectionTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginParameterizeMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginEditNormalsTool);
	}
	else if (PaletteIndex == FName(TEXT("Utilities")))
	{
		ToolbarBuilder.AddToolBarButton(Commands.BeginPolyGroupsTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginSimplifyMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginRemeshMeshTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginWeldEdgesTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginMeshInspectorTool);
		ToolbarBuilder.AddToolBarButton(Commands.BeginAttributeEditorTool);
	}
}

void FModelingToolsEditorModeToolkit::OnToolPaletteChanged(FName PaletteName) 
{
}


#undef LOCTEXT_NAMESPACE
