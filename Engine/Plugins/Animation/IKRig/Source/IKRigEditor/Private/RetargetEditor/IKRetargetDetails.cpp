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
	const TSharedRef<FUICommandList>& Commands = Controller->GetEditorController()->Editor.Pin()->GetToolkitCommands();
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
}

TSharedRef<SWidget>  FIKRetargeterDetails::MakeToolbar(const TSharedRef<FUICommandList>& Commands)
{
	FToolBarBuilder ToolbarBuilder(Commands, FMultiBoxCustomization::None);
	
	ToolbarBuilder.BeginSection("Edit Current Pose");

	ToolbarBuilder.AddToolBarButton(
		FIKRetargetCommands::Get().EditRetargetPose,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Edit"));

	ToolbarBuilder.AddToolBarButton(
		FIKRetargetCommands::Get().SetToRefPose,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Refresh"));

	ToolbarBuilder.EndSection();
	ToolbarBuilder.BeginSection("Create Poses");

	ToolbarBuilder.AddToolBarButton(
		FIKRetargetCommands::Get().NewRetargetPose,
		NAME_None,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Plus"));

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
