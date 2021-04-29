// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelFieldGroup.h"

#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "SDropTarget.h"
#include "SRCPanelTreeNode.h"
#include "SRCPanelExposedField.h"
#include "SRCPanelDragHandle.h"
#include "SRemoteControlPanel.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanel"

void SFieldGroup::Tick(const FGeometry&, const double, const float)
{
	if (bNeedsRename)
	{
		if (NameTextBox)
		{
			NameTextBox->EnterEditingMode();
		}
		bNeedsRename = false;
	}
}

void SFieldGroup::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, FRCColumnSizeData InColumnSizeData)
{
	Preset = InPreset;
	
	Id = InArgs._Id;
	Name = InArgs._Name;
	Nodes = InArgs._Children;
	ColumnSizeData = MoveTemp(InColumnSizeData);
	
	OnFieldDropEvent = InArgs._OnFieldDropEvent;
	OnGetGroupId = InArgs._OnGetGroupId;
	OnDeleteGroup = InArgs._OnDeleteGroup;
	bEditMode = InArgs._EditMode;

	TSharedRef<SWidget> LeftColumn = 
		SNew(SHorizontalBox)
		// Drag and drop handle
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Center)
		.Padding(FMargin(4.0f, 0.0f))
		.AutoWidth()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f) )
			.Visibility(this, &SFieldGroup::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
			[
				SNew(SRCPanelDragHandle<FFieldGroupDragDropOp>, Id)
				.Widget(SharedThis(this))
			]
		]
		+ SHorizontalBox::Slot()
		// Group name
		.FillWidth(1.0f)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0.f, 0.f, 0.f, 2.f))
		.AutoWidth()
		[
			SAssignNew(NameTextBox, SInlineEditableTextBlock)
			.ColorAndOpacity(this, &SFieldGroup::GetGroupNameTextColor)
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.Text(FText::FromName(Name))
			.OnTextCommitted(this, &SFieldGroup::OnLabelCommitted)
			.OnVerifyTextChanged(this, &SFieldGroup::OnVerifyItemLabelChanged)
			.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); })
		]
		+ SHorizontalBox::Slot()
		// Rename button
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(SButton)
			.Visibility(this, &SFieldGroup::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
			.ButtonStyle(FEditorStyle::Get(), "FlatButton")
			.OnClicked_Lambda([this] () 
				{
					bNeedsRename = true;
					return FReply::Handled();	
				})
			[
				SNew(STextBlock)
				.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf044"))) /*fa-edit*/)
			]
		];

	TSharedRef<SWidget> RightColumn =
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Right)
		.Padding(0, 2.0f)
		.FillWidth(1.f)
		[
			SNew(SButton)
			.Visibility_Raw(this, &SFieldGroup::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
			.OnClicked(this, &SFieldGroup::HandleDeleteGroup)
			.ButtonStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.UnexposeButton")
			[
				SNew(STextBlock)
				.TextStyle(FRemoteControlPanelStyle::Get(), "RemoteControlPanel.Button.TextStyle")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(FText::FromString(FString(TEXT("\xf00d"))) /*fa-times*/)
			]
		];
	
	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.f)
		.BorderImage(this, &SFieldGroup::GetBorderImage)
		.VAlign(VAlign_Fill)
		[
			SNew(SDropTarget)
			.VerticalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.VerticalDash"))
			.HorizontalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HorizontalDash"))
			.OnDrop_Lambda([this](TSharedPtr<FDragDropOperation> DragDropOperation) { return OnFieldDropGroup(DragDropOperation, nullptr); })
			.OnAllowDrop(this, &SFieldGroup::OnAllowDropFromOtherGroup)
			.OnIsRecognized(this, &SFieldGroup::OnAllowDropFromOtherGroup)
			[
				MakeSplitRow(LeftColumn, RightColumn)
			]
		]
	];
}

FName SFieldGroup::GetGroupName() const
{
	return Name;
}

void SFieldGroup::SetName(FName InName)
{
	Name = InName;
	NameTextBox->SetText(FText::FromName(Name));
}

void SFieldGroup::GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const
{
	OutChildren.Append(Nodes);
}

FGuid SFieldGroup::GetId() const
{
	return Id;
}

SRCPanelTreeNode::ENodeType SFieldGroup::GetType() const
{
	return SRCPanelTreeNode::Group;
}

TSharedPtr<SFieldGroup> SFieldGroup::AsGroup()
{
	return StaticCastSharedRef<SFieldGroup>(AsShared());
}

FReply SFieldGroup::OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SRCPanelTreeNode> TargetField)
{
	if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = Event.GetOperationAs<FExposedEntityDragDrop>())
	{
		return OnFieldDropGroup(DragDropOp, TargetField);
	}
	return FReply::Unhandled();
}

FReply SFieldGroup::OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SRCPanelTreeNode> TargetField)
{
	if (DragDropOperation)
	{
		if (DragDropOperation->IsOfType<FExposedEntityDragDrop>() && OnFieldDropEvent.IsBound())
		{
			return OnFieldDropEvent.Execute(DragDropOperation, TargetField, AsGroup());
		}
		else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>() && OnFieldDropEvent.IsBound())
		{
			return OnFieldDropEvent.Execute(DragDropOperation, nullptr, AsGroup());
		}
	}

	return FReply::Unhandled();
}

bool SFieldGroup::OnAllowDropFromOtherGroup(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FExposedEntityDragDrop>())
	{
		if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = StaticCastSharedPtr<FExposedEntityDragDrop>(DragDropOperation))
		{
			if (OnGetGroupId.IsBound())
			{
				FGuid OriginGroupId = OnGetGroupId.Execute(DragDropOp->GetId());
				if (OriginGroupId != Id)
				{
					return true;
				}
			}
		}
	}
	else if (DragDropOperation->IsOfType<FFieldGroupDragDropOp>())
	{
		if (TSharedPtr<FFieldGroupDragDropOp> DragDropOp = StaticCastSharedPtr<FFieldGroupDragDropOp>(DragDropOperation))
		{
			return DragDropOp->GetGroupId() != Id;
		}
	}

	return false;
}

FReply SFieldGroup::HandleDeleteGroup()
{
	OnDeleteGroup.ExecuteIfBound(AsGroup());
	return FReply::Handled();
}

FSlateColor SFieldGroup::GetGroupNameTextColor() const
{
	return FLinearColor(1, 1, 1, 0.7);
}

const FSlateBrush* SFieldGroup::GetBorderImage() const
{
	return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.GroupBorder");
}

EVisibility SFieldGroup::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

bool SFieldGroup::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	check(Preset.IsValid());
	FName TentativeName = FName(*InLabel.ToString());
	if (TentativeName != Name && !!Preset->Layout.GetGroupByName(TentativeName))
	{
		OutErrorMessage = LOCTEXT("NameAlreadyExists", "This name already exists.");
		return false;
	}

	return true;
}

void SFieldGroup::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	check(Preset.IsValid());
	FScopedTransaction Transaction(LOCTEXT("RenameGroup", "Rename Group"));
	Preset->Modify();
	Preset->Layout.RenameGroup(Id, FName(*InLabel.ToString()));
}

#undef LOCTEXT_NAMESPACE
