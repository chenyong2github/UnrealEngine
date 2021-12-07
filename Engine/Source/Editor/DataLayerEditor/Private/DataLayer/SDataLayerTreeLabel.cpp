// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataLayerTreeLabel.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "Math/ColorList.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerMode.h"
#include "DataLayerTransaction.h"
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
		.Font(this, &SDataLayerTreeLabel::GetDisplayNameFont)
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
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Visibility_Lambda([this] { return (DataLayerPtr.IsValid() && DataLayerPtr->IsLocked() && !DataLayerPtr->GetWorld()->IsPlayInEditor()) ? EVisibility::Visible : EVisibility::Collapsed; })
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FEditorStyle::GetBrush(TEXT("PropertyWindow.Locked")))
			.ToolTipText(LOCTEXT("LockedRuntimeDataLayerEditing", "Locked editing. (To allow editing, in Data Layer Outliner, go to Advanced -> Allow Runtime Data Layer Editing)"))
		]
	];
}

bool SDataLayerTreeLabel::ShouldBeHighlighted() const
{
	const FSceneOutlinerTreeItemPtr TreeItem = TreeItemPtr.Pin();
	FDataLayerTreeItem* DataLayerTreeItem = TreeItem ? TreeItem->CastTo<FDataLayerTreeItem>() : nullptr;
	return DataLayerTreeItem && DataLayerTreeItem->ShouldBeHighlighted();
}

FSlateFontInfo SDataLayerTreeLabel::GetDisplayNameFont() const
{
	if (ShouldBeHighlighted())
	{
		return FAppStyle::Get().GetFontStyle("DataLayerBrowser.LabelFontBold");
	}
	else
	{
		return FAppStyle::Get().GetFontStyle("DataLayerBrowser.LabelFont");
	}
}

FText SDataLayerTreeLabel::GetDisplayText() const
{
	const UDataLayer* DataLayer = DataLayerPtr.Get();
	bool bIsDataLayerActive = false;
	FText DataLayerRuntimeStateText = FText::GetEmpty();
	if (DataLayer && DataLayer->IsRuntime() && DataLayer->GetWorld()->IsPlayInEditor())
	{
		const UDataLayerSubsystem* DataLayerSubsystem = DataLayer->GetWorld()->GetSubsystem<UDataLayerSubsystem>();
		DataLayerRuntimeStateText = FText::Format(LOCTEXT("DataLayerRuntimeState", " ({0})"), FTextStringHelper::CreateFromBuffer(GetDataLayerRuntimeStateName(DataLayerSubsystem->GetDataLayerEffectiveRuntimeState(DataLayer))));
	}
	
	static const FText DataLayerDeleted = LOCTEXT("DataLayerLabelForMissingDataLayer", "(Deleted Data Layer)");
	return DataLayer ? FText::Format(LOCTEXT("DataLayerDisplayText", "{0}{1}"), FText::FromName(DataLayer->GetDataLayerLabel()), DataLayerRuntimeStateText) : DataLayerDeleted;
}

FText SDataLayerTreeLabel::GetTooltipText() const
{
	if (const FSceneOutlinerTreeItemPtr TreeItem = TreeItemPtr.Pin())
	{
		return FText::FromString(TreeItem->GetDisplayString());
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
		const TCHAR* IconName = DataLayer->GetDataLayerIconName();
		if (const FSlateBrush* CachedBrush = WeakSceneOutliner.Pin()->GetCachedIconForClass(IconName))
		{
			return CachedBrush;
		}

		const FSlateBrush* FoundSlateBrush = FEditorStyle::GetBrush(IconName);
		WeakSceneOutliner.Pin()->CacheIconForClass(IconName, FoundSlateBrush);
		return FoundSlateBrush;
	}
	return nullptr;
}

FText SDataLayerTreeLabel::GetIconTooltip() const
{
	const UDataLayer* DataLayer = DataLayerPtr.Get();
	return DataLayer ? (DataLayer->IsRuntime() ? FText(LOCTEXT("RuntimeDataLayer", "Runtime Data Layer")) : FText(LOCTEXT("EditorDataLayer", "Editor Data Layer"))) : FText();
}

FSlateColor SDataLayerTreeLabel::GetForegroundColor() const
{
	if (TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItemPtr.Pin()))
	{
		return BaseColor.GetValue();
	}

	const UDataLayer* DataLayer = DataLayerPtr.Get();
	if (DataLayer)
	{
		if (DataLayer->GetWorld()->IsPlayInEditor())
		{
			if (DataLayer->IsRuntime())
			{
				const UDataLayerSubsystem* DataLayerSubsystem = DataLayer->GetWorld()->GetSubsystem<UDataLayerSubsystem>();
				EDataLayerRuntimeState State = DataLayerSubsystem->GetDataLayerEffectiveRuntimeState(DataLayer);
				switch (State)
				{
				case EDataLayerRuntimeState::Activated:
					return FColorList::LimeGreen;
				case EDataLayerRuntimeState::Loaded:
					return FColorList::NeonBlue;
				case EDataLayerRuntimeState::Unloaded:
					return FColorList::DarkSlateGrey;
				}
			}
			else
			{
				return FSceneOutlinerCommonLabelData::DarkColor;
			}
		}
		else if (DataLayer->IsLocked())
		{
			return FSceneOutlinerCommonLabelData::DarkColor;
		}
	}
	if (!DataLayer || !DataLayer->GetWorld())
	{
		return FLinearColor(0.2f, 0.2f, 0.25f);
	}
	if (ShouldBeHighlighted())
	{
		return FAppStyle::Get().GetSlateColor("Colors.AccentBlue");
	}
	return FSlateColor::UseForeground();
}

bool SDataLayerTreeLabel::OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
{
	if (InLabel.IsEmptyOrWhitespace())
	{
		OutErrorMessage = LOCTEXT("EmptyDataLayerLabel", "Data Layer must be given a name");
		return false;
	}

	UDataLayer* FoundDataLayer;
	if (UDataLayerEditorSubsystem::Get()->TryGetDataLayerFromLabel(*InLabel.ToString(), FoundDataLayer) && FoundDataLayer != DataLayerPtr.Get())
	{
		OutErrorMessage = LOCTEXT("RenameFailed_AlreadyExists", "This Data Layer already exists");
		return false;
	}

	return true;
}

void SDataLayerTreeLabel::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	UDataLayer* DataLayer = DataLayerPtr.Get();
	if (DataLayer && !InLabel.ToString().Equals(DataLayer->GetDataLayerLabel().ToString(), ESearchCase::CaseSensitive))
	{
		const FScopedDataLayerTransaction Transaction(LOCTEXT("SceneOutlinerRenameDataLayerTransaction", "Rename Data Layer"), DataLayer->GetWorld());
		UDataLayerEditorSubsystem::Get()->RenameDataLayer(DataLayer, *InLabel.ToString());

		if (WeakSceneOutliner.IsValid())
		{
			WeakSceneOutliner.Pin()->SetKeyboardFocus();
		}
	}
}

#undef LOCTEXT_NAMESPACE 