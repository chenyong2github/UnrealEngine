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

void SRCPanelGroup::Tick(const FGeometry&, const double, const float)
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

void SRCPanelGroup::Construct(const FArguments& InArgs, URemoteControlPreset* InPreset, FRCColumnSizeData InColumnSizeData)
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
			.Visibility(this, &SRCPanelGroup::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
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
			.ColorAndOpacity(this, &SRCPanelGroup::GetGroupNameTextColor)
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.Text(FText::FromName(Name))
			.OnTextCommitted(this, &SRCPanelGroup::OnLabelCommitted)
			.OnVerifyTextChanged(this, &SRCPanelGroup::OnVerifyItemLabelChanged)
			.IsReadOnly_Lambda([this]() { return !bEditMode.Get(); })
		]
		+ SHorizontalBox::Slot()
		// Rename button
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(SButton)
			.Visibility(this, &SRCPanelGroup::GetVisibilityAccordingToEditMode, EVisibility::Collapsed)
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
			.Visibility_Raw(this, &SRCPanelGroup::GetVisibilityAccordingToEditMode, EVisibility::Hidden)
			.OnClicked(this, &SRCPanelGroup::HandleDeleteGroup)
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
		.BorderImage(this, &SRCPanelGroup::GetBorderImage)
		.VAlign(VAlign_Fill)
		[
			SNew(SDropTarget)
			.VerticalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.VerticalDash"))
			.HorizontalImage(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.HorizontalDash"))
			.OnDrop_Lambda([this](TSharedPtr<FDragDropOperation> DragDropOperation) { return OnFieldDropGroup(DragDropOperation, nullptr); })
			.OnAllowDrop(this, &SRCPanelGroup::OnAllowDropFromOtherGroup)
			.OnIsRecognized(this, &SRCPanelGroup::OnAllowDropFromOtherGroup)
			[
				MakeSplitRow(LeftColumn, RightColumn)
			]
		]
	];
}

FName SRCPanelGroup::GetGroupName() const
{
	return Name;
}

void SRCPanelGroup::SetName(FName InName)
{
	Name = InName;
	NameTextBox->SetText(FText::FromName(Name));
}

void SRCPanelGroup::EnterRenameMode()
{
	bNeedsRename = true;
}

void SRCPanelGroup::GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const
{
	OutChildren.Append(Nodes);
}

FGuid SRCPanelGroup::GetId() const
{
	return Id;
}

SRCPanelTreeNode::ENodeType SRCPanelGroup::GetType() const
{
	return SRCPanelTreeNode::Group;
}

TSharedPtr<SRCPanelGroup> SRCPanelGroup::AsGroup()
{
	return StaticCastSharedRef<SRCPanelGroup>(AsShared());
}

FReply SRCPanelGroup::OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SRCPanelTreeNode> TargetField)
{
	if (TSharedPtr<FExposedEntityDragDrop> DragDropOp = Event.GetOperationAs<FExposedEntityDragDrop>())
	{
		return OnFieldDropGroup(DragDropOp, TargetField);
	}
	return FReply::Unhandled();
}

FReply SRCPanelGroup::OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SRCPanelTreeNode> TargetField)
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

bool SRCPanelGroup::OnAllowDropFromOtherGroup(TSharedPtr<FDragDropOperation> DragDropOperation)
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

FReply SRCPanelGroup::HandleDeleteGroup()
{
	OnDeleteGroup.ExecuteIfBound(AsGroup());
	return FReply::Handled();
}

FSlateColor SRCPanelGroup::GetGroupNameTextColor() const
{
	return FLinearColor(1, 1, 1, 0.7);
}

const FSlateBrush* SRCPanelGroup::GetBorderImage() const
{
	return FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.TransparentBorder");
}

EVisibility SRCPanelGroup::GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const
{
	return bEditMode.Get() ? EVisibility::Visible : NonEditModeVisibility;
}

bool SRCPanelGroup::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
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

void SRCPanelGroup::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	check(Preset.IsValid());
	FScopedTransaction Transaction(LOCTEXT("RenameGroup", "Rename Group"));
	Preset->Modify();
	Preset->Layout.RenameGroup(Id, FName(*InLabel.ToString()));
}

#undef LOCTEXT_NAMESPACE
