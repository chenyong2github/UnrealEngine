// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/SIKRetargetPoseEditor.h"

#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"

#define LOCTEXT_NAMESPACE "SIKRetargetPoseEditor"

void SIKRetargetPoseEditor::Construct(
	const FArguments& InArgs,
	TSharedRef<FIKRetargetEditorController> InEditorController)
{
	EditorController = InEditorController;
	
	// fill list of pose names
	PoseNames.Reset();
	for (const TTuple<FName, FIKRetargetPose>& Pose : EditorController.Pin()->AssetController->GetRetargetPoses())
	{
		PoseNames.Add(MakeShareable(new FName(Pose.Key)));
	}

	// the commands for the menus
	TSharedPtr<FUICommandList> Commands = EditorController.Pin()->Editor.Pin()->GetToolkitCommands();
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// add pose selection label
			+SHorizontalBox::Slot()
			.Padding(2.0f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CurrentPose", "Current Retarget Pose:"))
			]

			// add pose selection combo box
			+SHorizontalBox::Slot()
			.Padding(2.0f)
			[
				SNew(SComboBox<TSharedPtr<FName>>)
				.OptionsSource(&PoseNames)
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
				{
					return SNew(STextBlock).Text(FText::FromName(*InItem.Get()));
				})
				.OnSelectionChanged(EditorController.Pin().Get(), &FIKRetargetEditorController::OnPoseSelected)
				[
					SNew(STextBlock).Text(EditorController.Pin().Get(), &FIKRetargetEditorController::GetCurrentPoseName)
				]
			]
		]

		+SVerticalBox::Slot()
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.HAlign(HAlign_Center)
			[
				MakeToolbar(Commands)
			]
		]
	];
}

TSharedRef<SWidget>  SIKRetargetPoseEditor::MakeToolbar(TSharedPtr<FUICommandList> Commands)
{
	FToolBarBuilder ToolbarBuilder(Commands, FMultiBoxCustomization::None);
	
	ToolbarBuilder.BeginSection("Edit Current Pose");

	ToolbarBuilder.AddToolBarButton(
		FIKRetargetCommands::Get().EditRetargetPose,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Edit"));

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &SIKRetargetPoseEditor::GenerateResetMenuContent, Commands),
		LOCTEXT("ResetPose_Label", "Reset"),
		LOCTEXT("ResetPoseToolTip_Label", "Reset bones to reference pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));

	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Create Poses");

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &SIKRetargetPoseEditor::GenerateNewMenuContent, Commands),
		LOCTEXT("CreatePose_Label", "Create"),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"));

	ToolbarBuilder.AddToolBarButton(
		FIKRetargetCommands::Get().DeleteRetargetPose,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Delete"));

	ToolbarBuilder.AddToolBarButton(
		FIKRetargetCommands::Get().RenameRetargetPose,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Settings"));

	ToolbarBuilder.EndSection();

	return ToolbarBuilder.MakeWidget();
}

TSharedRef<SWidget> SIKRetargetPoseEditor::GenerateResetMenuContent(TSharedPtr<FUICommandList> Commands)
{
	FMenuBuilder MenuBuilder(true, Commands);

	MenuBuilder.AddMenuEntry(
		FIKRetargetCommands::Get().ResetSelectedBones,
		TEXT("Reset Selected"),
		TAttribute<FText>(),
		TAttribute<FText>());

	MenuBuilder.AddMenuEntry(
		FIKRetargetCommands::Get().ResetSelectedAndChildrenBones,
		TEXT("Reset Selected And Children"),
		TAttribute<FText>(),
		TAttribute<FText>());

	MenuBuilder.AddMenuEntry(
		FIKRetargetCommands::Get().ResetAllBones,
		TEXT("Reset All"),
		TAttribute<FText>(),
		TAttribute<FText>());

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SIKRetargetPoseEditor::GenerateNewMenuContent(TSharedPtr<FUICommandList> Commands)
{
	FMenuBuilder MenuBuilder(true, Commands);

	MenuBuilder.BeginSection("Create", LOCTEXT("CreatePoseOperations", "Create New Retarget Pose"));
	{
		MenuBuilder.AddMenuEntry(
		FIKRetargetCommands::Get().NewRetargetPose,
		TEXT("Create"),
		TAttribute<FText>(),
		TAttribute<FText>());

		MenuBuilder.AddMenuEntry(
			FIKRetargetCommands::Get().DuplicateRetargetPose,
			TEXT("Create"),
			TAttribute<FText>(),
			TAttribute<FText>());
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Import",LOCTEXT("ImportPoseOperations", "Import Retarget Pose"));
	{
		MenuBuilder.AddMenuEntry(
		FIKRetargetCommands::Get().ImportRetargetPose,
		TEXT("Import"),
		TAttribute<FText>(),
		TAttribute<FText>());
	
		MenuBuilder.AddMenuEntry(
			FIKRetargetCommands::Get().ImportRetargetPoseFromAnim,
			TEXT("ImportFromSequence"),
			TAttribute<FText>(),
			TAttribute<FText>());
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Export",LOCTEXT("EmportPoseOperations", "Export Retarget Pose"));
	{
		MenuBuilder.AddMenuEntry(
			FIKRetargetCommands::Get().ExportRetargetPose,
			TEXT("Export"),
			TAttribute<FText>(),
			TAttribute<FText>());
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
