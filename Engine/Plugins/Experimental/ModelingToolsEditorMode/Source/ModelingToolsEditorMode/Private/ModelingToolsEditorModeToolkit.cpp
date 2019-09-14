// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorModeToolkit.h"
#include "ModelingToolsEditorMode.h"
#include "Engine/Selection.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorModeToolkit"

FModelingToolsEditorModeToolkit::FModelingToolsEditorModeToolkit()
{
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

	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
				[
					ToolHeaderLabel->AsShared()
				]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).AutoHeight().Padding(10, 10, 10, 20)
				[
					ToolMessageArea->AsShared()
				]

			+ SVerticalBox::Slot().HAlign(HAlign_Fill).AutoHeight().MaxHeight(500.0f)
				[
					DetailsView->AsShared()
				]

		];
		
	FModeToolkit::Init(InitToolkitHost);



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
	});


	GetToolsEditorMode()->OnToolNotificationMessage.AddLambda([this](const FText& Message)
	{
		PostNotification(Message);
	});


}



void FModelingToolsEditorModeToolkit::PostNotification(const FText& Message)
{
	ToolMessageArea->SetText(Message);
}

void FModelingToolsEditorModeToolkit::ClearNotification()
{
	ToolMessageArea->SetText(FText());
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

#undef LOCTEXT_NAMESPACE
