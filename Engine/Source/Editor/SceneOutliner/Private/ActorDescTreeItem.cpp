// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorDescTreeItem.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerDragDrop.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ActorEditorUtils.h"
#include "ClassIconFinder.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerMode.h"
#include "Logging/MessageLog.h"
#include "SSocketChooser.h"

#define LOCTEXT_NAMESPACE "SceneOutliner_ActorDescTreeItem"

const FSceneOutlinerTreeItemType FActorDescTreeItem::Type(&ISceneOutlinerTreeItem::Type);

struct SActorDescTreeLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SActorDescTreeLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FActorDescTreeItem& ActorDescItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());

		TreeItemPtr = StaticCastSharedRef<FActorDescTreeItem>(ActorDescItem.AsShared());
		ActorDescPtr = ActorDescItem.ActorDesc;

		HighlightText = SceneOutliner.GetFilterHighlightText();

		TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;

		auto MainContent = SNew(SHorizontalBox)
			// Main actor desc label
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
				.Text(this, &SActorDescTreeLabel::GetDisplayText)
				.ToolTipText(this, &SActorDescTreeLabel::GetTooltipText)
				.HighlightText(HighlightText)
				.ColorAndOpacity(this, &SActorDescTreeLabel::GetForegroundColor)
				.OnTextCommitted(this, &SActorDescTreeLabel::OnLabelCommitted)
				.OnVerifyTextChanged(this, &SActorDescTreeLabel::OnVerifyItemLabelChanged)
				.IsSelected(FIsSelected::CreateSP(&InRow, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively))
				.IsReadOnly_Lambda([Item = ActorDescItem.AsShared(), this]()
				{
					return !CanExecuteRenameRequest(Item.Get());
				})
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0.0f, 0.f, 3.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SActorDescTreeLabel::GetTypeText)
				.Visibility(this, &SActorDescTreeLabel::GetTypeTextVisibility)
				.HighlightText(HighlightText)
			];

		if (WeakSceneOutliner.Pin()->GetMode()->IsInteractive())
		{
			ActorDescItem.RenameRequestEvent.BindSP(InlineTextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		}

		ChildSlot
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FSceneOutlinerDefaultTreeItemMetrics::IconPadding())
				[
					SNew(SBox)
					.WidthOverride(FSceneOutlinerDefaultTreeItemMetrics::IconSize())
					.HeightOverride(FSceneOutlinerDefaultTreeItemMetrics::IconSize())
					[
						SNew(SImage)
						.Image(this, &SActorDescTreeLabel::GetIcon)
						.ToolTipText(this, &SActorDescTreeLabel::GetIconTooltip)
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f)
				[
					MainContent
				]
			];
	}

private:
	TWeakPtr<FActorDescTreeItem> TreeItemPtr;
	const FWorldPartitionActorDesc* ActorDescPtr = nullptr;
	TAttribute<FText> HighlightText;

	FText GetDisplayText() const
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ActorLabel"), FText::FromName(ActorDescPtr->GetActorLabel()));
		Args.Add(TEXT("UnloadedTag"), LOCTEXT("UnloadedActorLabel", "(Unloaded)"));
		return FText::Format(LOCTEXT("UnloadedActorDisplay", "{ActorLabel} {UnloadedTag}"), Args);
	}

	FText GetTooltipText() const
	{
		return FText();
	}

	FText GetTypeText() const
	{
		return ActorDescPtr ? FText::FromName(ActorDescPtr->GetActorClass()->GetFName()) : FText();
	}

	EVisibility GetTypeTextVisibility() const
	{
		return HighlightText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	}

	const FSlateBrush* GetIcon() const
	{
		if (ActorDescPtr && WeakSceneOutliner.IsValid())
		{
			const FName IconName = ActorDescPtr->GetActorClass()->GetFName();
			
			const FSlateBrush* CachedBrush = WeakSceneOutliner.Pin()->GetCachedIconForClass(IconName);
			if (CachedBrush != nullptr)
			{
				return CachedBrush;
			}
			else if (IconName != NAME_None)
			{

				const FSlateBrush* FoundSlateBrush = FSlateIconFinder::FindIconForClass(ActorDescPtr->GetActorClass()).GetIcon();
				WeakSceneOutliner.Pin()->CacheIconForClass(IconName, FoundSlateBrush);
				return FoundSlateBrush;
			}
		}
		return nullptr;
	}

	const FSlateBrush* GetIconOverlay() const
	{
		return nullptr;
	}

	FText GetIconTooltip() const
	{
		return FText();
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		const auto TreeItem = TreeItemPtr.Pin();
		if (auto BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem))
		{
			return BaseColor.GetValue();
		}

		if (ActorDescPtr == nullptr)
		{
			// Deleted actor!
			return FLinearColor(0.2f, 0.2f, 0.25f);
		}

		return FSceneOutlinerCommonLabelData::DarkColor;
	}

	bool OnVerifyItemLabelChanged(const FText&, FText&)
	{
		// don't allow label change for unloaded actor items
		return false;
	}

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		// not supported.
	}
};

FActorDescTreeItem::FActorDescTreeItem(const FWorldPartitionActorDesc* InActorDesc)
	: ISceneOutlinerTreeItem(Type)
	, ActorDesc(InActorDesc)
{
	check (InActorDesc != nullptr);
	ID = FSceneOutlinerTreeItemID(ActorDesc->GetGuid());
}

FSceneOutlinerTreeItemID FActorDescTreeItem::GetID() const
{
	return ID;
}

FString FActorDescTreeItem::GetDisplayString() const
{
	return ActorDesc ? ActorDesc->GetActorLabel().ToString() : LOCTEXT("ActorLabelForMissingActor", "(Deleted Actor)").ToString();
}

bool FActorDescTreeItem::CanInteract() const
{
	return false;
}

TSharedRef<SWidget> FActorDescTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SActorDescTreeLabel, *this, Outliner, InRow);
}

void FActorDescTreeItem::OnVisibilityChanged(const bool bNewVisibility)
{

}

bool FActorDescTreeItem::GetVisibility() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
