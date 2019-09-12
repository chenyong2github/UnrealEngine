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


TSharedRef<SButton> FModelingToolsEditorModeToolkit::MakeToolButton(const FText& ButtonLabel, const FString& ToolIdentifier)
{
	return SNew(SButton).Text(ButtonLabel)
		.OnClicked_Lambda([this, ToolIdentifier]() { GetToolsContext()->StartTool(ToolIdentifier); return FReply::Handled(); })
		.IsEnabled_Lambda([this, ToolIdentifier]() { return GetToolsContext()->CanStartTool(ToolIdentifier); });
}

SVerticalBox::FSlot& FModelingToolsEditorModeToolkit::MakeToolButtonSlotV(const FText& ButtonLabel, const FString& ToolIdentifier)
{
	return SVerticalBox::Slot()
		.HAlign(HAlign_Center)
		.AutoHeight()
		[MakeToolButton(ButtonLabel, ToolIdentifier)];
}

SHorizontalBox::FSlot& FModelingToolsEditorModeToolkit::MakeToolButtonSlotH(const FText& ButtonLabel, const FString& ToolIdentifier)
{
	return SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.AutoWidth()
		[MakeToolButton(ButtonLabel, ToolIdentifier)];
}


SVerticalBox::FSlot& FModelingToolsEditorModeToolkit::MakeSetToolLabelV(const FText& LabelText)
{
	return SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(5)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LabelText)
		];
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


	ToolHeaderLabel = SNew(STextBlock).AutoWrapText(true);
	ToolHeaderLabel->SetText(LOCTEXT("SelectToolLabel", "Select a Tool from the Toolbar"));
	ToolHeaderLabel->SetJustification(ETextJustify::Center);

	SAssignNew(ToolkitWidget, SBorder)
		.HAlign(HAlign_Fill)
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(5)
				[ToolHeaderLabel->AsShared()]

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
	});

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
