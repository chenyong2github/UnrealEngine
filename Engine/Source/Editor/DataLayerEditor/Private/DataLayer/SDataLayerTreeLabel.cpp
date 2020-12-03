// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataLayerTreeLabel.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerMode.h"
#include "ScopedTransaction.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

void SDataLayerTreeLabel::Construct(const FArguments& InArgs, FDataLayerTreeItem& DataLayerItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());
	TreeItemPtr = StaticCastSharedRef<FDataLayerTreeItem>(DataLayerItem.AsShared());
	DataLayerPtr = DataLayerItem.GetDataLayer();
	HighlightText = SceneOutliner.GetFilterHighlightText();

	TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;

	auto MainContent = SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
	.VAlign(VAlign_Center)
	[
		SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
		.Text(this, &SDataLayerTreeLabel::GetDisplayText)
		.ToolTipText(this, &SDataLayerTreeLabel::GetTooltipText)
		.HighlightText(HighlightText)
		.ColorAndOpacity(this, &SDataLayerTreeLabel::GetForegroundColor)
		.OnTextCommitted(this, &SDataLayerTreeLabel::OnLabelCommitted)
		.OnVerifyTextChanged(this, &SDataLayerTreeLabel::OnVerifyItemLabelChanged)
		.IsSelected(FIsSelected::CreateSP(&InRow, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively))
		.IsReadOnly_Lambda([Item = DataLayerItem.AsShared(), this]()
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
		.Text(this, &SDataLayerTreeLabel::GetTypeText)
		.Visibility(this, &SDataLayerTreeLabel::GetTypeTextVisibility)
		.HighlightText(HighlightText)
	];

	if (WeakSceneOutliner.Pin()->GetMode()->IsInteractive())
	{
		DataLayerItem.RenameRequestEvent.BindSP(InlineTextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
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
				.Image(this, &SDataLayerTreeLabel::GetIcon)
				.ToolTipText(this, &SDataLayerTreeLabel::GetIconTooltip)
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

FText SDataLayerTreeLabel::GetDisplayText() const
{
	const UDataLayer* DataLayer = DataLayerPtr.Get();
	return DataLayer ? FText::FromName(DataLayer->GetDataLayerLabel()) : LOCTEXT("DataLayerLabelForMissingDataLayer", "(Deleted Data Layer)");
}

FText SDataLayerTreeLabel::GetTooltipText() const
{
	if (const UDataLayer* DataLayer = DataLayerPtr.Get())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ID_Name"), LOCTEXT("CustomColumnMode_InternalName", "ID Name"));
		Args.Add(TEXT("Name"), FText::FromName(DataLayer->GetFName()));
		return FText::Format(LOCTEXT("DataLayerNameTooltip", "{ID_Name}: {Name}"), Args);
	}
	return FText();
}

FText SDataLayerTreeLabel::GetTypeText() const
{
	const UDataLayer* DataLayer = DataLayerPtr.Get();
	return DataLayer ? FText::FromName(DataLayer->GetClass()->GetFName()) : FText();
}

EVisibility SDataLayerTreeLabel::GetTypeTextVisibility() const
{
	return HighlightText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

const FSlateBrush* SDataLayerTreeLabel::GetIcon() const
{
	const UDataLayer* DataLayer = DataLayerPtr.Get();
	if (DataLayer && WeakSceneOutliner.IsValid())
	{
		const FName IconName = DataLayer->GetClass()->GetFName();
		if (const FSlateBrush* CachedBrush = WeakSceneOutliner.Pin()->GetCachedIconForClass(IconName))
		{
			return CachedBrush;
		}

		const FSlateBrush* FoundSlateBrush = FEditorStyle::GetBrush(TEXT("DataLayer.Icon16x"));
		WeakSceneOutliner.Pin()->CacheIconForClass(IconName, FoundSlateBrush);
		return FoundSlateBrush;
	}
	return nullptr;
}

FText SDataLayerTreeLabel::GetIconTooltip() const
{
	return TreeItemPtr.Pin().IsValid() && DataLayerPtr.Get() ? FText::FromString(DataLayerPtr.Get()->GetClass()->GetName()) : FText();
}

FSlateColor SDataLayerTreeLabel::GetForegroundColor() const
{
	if (TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItemPtr.Pin()))
	{
		return BaseColor.GetValue();
	}

	const UDataLayer* DataLayer = DataLayerPtr.Get();
	return (!DataLayer || !DataLayer->GetWorld()) ? FLinearColor(0.2f, 0.2f, 0.25f) : FSlateColor::UseForeground();
}

bool SDataLayerTreeLabel::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (InLabel.IsEmptyOrWhitespace())
	{
		OutErrorMessage = LOCTEXT("EmptyDataLayerLabel", "DataLayer must be given a name");
		return false;
	}

	UDataLayer* FoundDataLayer;
	if (UDataLayerEditorSubsystem::Get()->TryGetDataLayerFromLabel(*InLabel.ToString(), FoundDataLayer) && FoundDataLayer != DataLayerPtr.Get())
	{
		OutErrorMessage = LOCTEXT("RenameFailed_AlreadyExists", "This DataLayer already exists");
		return false;
	}

	return true;
}

void SDataLayerTreeLabel::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	UDataLayer* DataLayer = DataLayerPtr.Get();
	if (DataLayer && !InLabel.ToString().Equals(DataLayer->GetDataLayerLabel().ToString(), ESearchCase::CaseSensitive))
	{
		const FScopedTransaction Transaction(LOCTEXT("SceneOutlinerRenameDataLayerTransaction", "Rename Data Layer"));
		UDataLayerEditorSubsystem::Get()->RenameDataLayer(DataLayer, *InLabel.ToString());

		if (WeakSceneOutliner.IsValid())
		{
			WeakSceneOutliner.Pin()->SetKeyboardFocus();
		}
	}
}

#undef LOCTEXT_NAMESPACE 