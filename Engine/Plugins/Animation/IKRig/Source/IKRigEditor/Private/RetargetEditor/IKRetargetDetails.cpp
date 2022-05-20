// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargetDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/SToolBarButtonBlock.h"
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"

#define LOCTEXT_NAMESPACE "IKRetargeterDetails"

TSharedRef<IDetailCustomization> FIKRetargeterDetails::MakeInstance()
{
	return MakeShareable(new FIKRetargeterDetails);
}

void FIKRetargeterDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TObjectPtr<UIKRetargeterController> Controller = GetAssetControllerFromSelectedObjects(DetailBuilder);
	if (!Controller)
	{
		return;
	}

	FIKRetargetEditorController* EditorController = Controller->GetEditorController();
	if (!EditorController)
	{
		return;
	}

	// the commands for the menus
	TSharedPtr<FUICommandList> Commands = Controller->GetEditorController()->Editor.Pin()->GetToolkitCommands();

	// add a new category at the top to edit the retarget pose
	IDetailCategoryBuilder& EditPoseCategoryBuilder = DetailBuilder.EditCategory( "Edit Retarget Pose", LOCTEXT("EditPoseLabel", "Edit Retarget Pose"), ECategoryPriority::Default );

	// fill list of pose names
	PoseNames.Reset();
	for (const TTuple<FName, FIKRetargetPose>& Pose : Controller->GetRetargetPoses())
	{
		PoseNames.Add(MakeShareable(new FName(Pose.Key)));
	}

	// add pose selection combo box
	FDetailWidgetRow& CurrentPoseRow = EditPoseCategoryBuilder.AddCustomRow(LOCTEXT("CurrentPoseLabel", "Current Pose"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("CurrentPose", "Current Retarget Pose"))
		.Font(DetailBuilder.GetDetailFont())
	]
	.ValueContent()
	[
		SNew(SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&PoseNames)
		.OnGenerateWidget_Lambda([](TSharedPtr<FName> InItem)
		{
			return SNew(STextBlock).Text(FText::FromName(*InItem.Get()));
		})
		.OnSelectionChanged(EditorController, &FIKRetargetEditorController::OnPoseSelected)
		[
			SNew(STextBlock).Text(EditorController, &FIKRetargetEditorController::GetCurrentPoseName)
		]
	];

	// add pose editing toolbar
	FDetailWidgetRow& ToolbarRow = EditPoseCategoryBuilder.AddCustomRow(LOCTEXT("CurrentPoseLabel", "Edit Pose"))
	.WholeRowWidget
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Center)
		[
			MakeToolbar(Commands)
		]
	];

	// add the bone size slider
	TSharedRef<IPropertyHandle> BoneSizeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UIKRetargeter, BoneDrawSize));
	EditPoseCategoryBuilder.AddProperty(BoneSizeHandle);
}

TSharedRef<SWidget>  FIKRetargeterDetails::MakeToolbar(TSharedPtr<FUICommandList> Commands)
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
		FOnGetContent::CreateSP(this, &FIKRetargeterDetails::GenerateResetMenuContent, Commands),
		LOCTEXT("ResetPose_Label", "Reset"),
		LOCTEXT("ResetPoseToolTip_Label", "Reset bones to reference pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"));

	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("Create Poses");

	ToolbarBuilder.AddComboButton(
		FUIAction(),
		FOnGetContent::CreateSP(this, &FIKRetargeterDetails::GenerateNewMenuContent, Commands),
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

TSharedRef<SWidget> FIKRetargeterDetails::GenerateResetMenuContent(TSharedPtr<FUICommandList> Commands)
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

TSharedRef<SWidget> FIKRetargeterDetails::GenerateNewMenuContent(TSharedPtr<FUICommandList> Commands)
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

TObjectPtr<UIKRetargeterController> FIKRetargeterDetails::GetAssetControllerFromSelectedObjects(IDetailLayoutBuilder& DetailBuilder) const
{
	TArray< TWeakObjectPtr<UObject> > OutObjects;
	DetailBuilder.GetObjectsBeingCustomized(OutObjects);
	if (OutObjects.IsEmpty())
	{
		return nullptr;
	}

	const TObjectPtr<UIKRetargeter> Asset = CastChecked<UIKRetargeter>(OutObjects[0].Get());
	return UIKRetargeterController::GetController(Asset);
}

#undef LOCTEXT_NAMESPACE
