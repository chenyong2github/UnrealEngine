// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataLayerTreeLabel.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTreeItem.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "WorldPartition/DataLayer/DataLayerSubsystem.h"
#include "Math/ColorList.h"
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
	bInEditingMode = false;

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
		.OnEnterEditingMode(this, &SDataLayerTreeLabel::OnEnterEditingMode)
		.OnExitEditingMode(this, &SDataLayerTreeLabel::OnExitEditingMode)
		.IsSelected(FIsSelected::CreateSP(&InRow, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively))
		.IsReadOnly_Lambda([Item = DataLayerItem.AsShared(), this]()
		{
			return DataLayerPtr == nullptr || !DataLayerPtr->SupportRelabeling() || !CanExecuteRenameRequest(Item.Get());
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
			.Visibility_Lambda([this] { return (DataLayerPtr.IsValid() && DataLayerPtr->IsLocked() && !DataLayerPtr->IsReadOnly() && !DataLayerPtr->GetWorld()->IsPlayInEditor()) ? EVisibility::Visible : EVisibility::Collapsed; })
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FAppStyle::GetBrush(TEXT("PropertyWindow.Locked")))
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

bool SDataLayerTreeLabel::IsInActorEditorContext() const
{
	const UDataLayerInstance* DataLayer = DataLayerPtr.Get();
	return DataLayer && DataLayer->IsInActorEditorContext();
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
	const UDataLayerInstance* DataLayer = DataLayerPtr.Get();
	bool bIsDataLayerActive = false;
	FText SuffixText = FText::GetEmpty();
	if (!bInEditingMode)
	{
		if (DataLayer && DataLayer->IsRuntime() && DataLayer->GetWorld()->IsPlayInEditor())
		{
			const UDataLayerSubsystem* DataLayerSubsystem = DataLayer->GetWorld()->GetSubsystem<UDataLayerSubsystem>();
			SuffixText = FText::Format(LOCTEXT("DataLayerRuntimeState", " ({0})"), FTextStringHelper::CreateFromBuffer(GetDataLayerRuntimeStateName(DataLayerSubsystem->GetDataLayerEffectiveRuntimeState(DataLayer))));
		}
		else if (IsInActorEditorContext())
		{
			SuffixText = FText(LOCTEXT("IsCurrentSuffix", " (Current)"));
		}
	}

	static const FText DataLayerDeleted = LOCTEXT("DataLayerLabelForMissingDataLayer", "(Deleted Data Layer)");
	return DataLayer ? FText::Format(LOCTEXT("DataLayerDisplayText", "{0}{1}"), FText::FromString(DataLayer->GetDataLayerShortName()), SuffixText) : DataLayerDeleted;
}

FText SDataLayerTreeLabel::GetTooltipText() const
{
	if (const FSceneOutlinerTreeItemPtr TreeItem = TreeItemPtr.Pin())
	{
		FText Description = IsInActorEditorContext() ? LOCTEXT("DataLayerIsCurrentDescription", "This Data Layer is part of Current Data Layers. New actors will attempt to be added to this Data Layer.") : FText::GetEmpty();
		return FText::Format(LOCTEXT("DataLayerTooltipText", "{0}\n{1}"), FText::FromString(TreeItem->GetDisplayString()), Description);
	}

	return FText();
}

FText SDataLayerTreeLabel::GetTypeText() const
{
	const UDataLayerInstance* DataLayer = DataLayerPtr.Get();
	return DataLayer ? FText::FromName(DataLayer->GetClass()->GetFName()) : FText();
}

EVisibility SDataLayerTreeLabel::GetTypeTextVisibility() const
{
	return HighlightText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

const FSlateBrush* SDataLayerTreeLabel::GetIcon() const
{
	const UDataLayerInstance* DataLayer = DataLayerPtr.Get();
	if (DataLayer && WeakSceneOutliner.IsValid())
	{
		const TCHAR* IconName = DataLayer->GetDataLayerIconName();
		if (const FSlateBrush* CachedBrush = WeakSceneOutliner.Pin()->GetCachedIconForClass(IconName))
		{
			return CachedBrush;
		}

		const FSlateBrush* FoundSlateBrush = FAppStyle::GetBrush(IconName);
		WeakSceneOutliner.Pin()->CacheIconForClass(IconName, FoundSlateBrush);
		return FoundSlateBrush;
	}
	return nullptr;
}

FText SDataLayerTreeLabel::GetIconTooltip() const
{
	const UDataLayerInstance* DataLayer = DataLayerPtr.Get();
	return DataLayer ? (DataLayer->IsRuntime() ? FText(LOCTEXT("RuntimeDataLayer", "Runtime Data Layer")) : FText(LOCTEXT("EditorDataLayer", "Editor Data Layer"))) : FText();
}

FSlateColor SDataLayerTreeLabel::GetForegroundColor() const
{
	if (TOptional<FLinearColor> BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItemPtr.Pin()))
	{
		return BaseColor.GetValue();
	}

	const UDataLayerInstance* DataLayer = DataLayerPtr.Get();
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
	else if (IsInActorEditorContext())
	{
		return FAppStyle::Get().GetSlateColor("Colors.AccentGreen");
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

	UDataLayerInstance* FoundDataLayerInstance;
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (UDataLayerEditorSubsystem::Get()->TryGetDataLayerFromLabel(*InLabel.ToString(), FoundDataLayerInstance) && FoundDataLayerInstance != DataLayerPtr.Get())
	{
		OutErrorMessage = LOCTEXT("RenameFailed_AlreadyExists", "This Data Layer already exists");
		return false;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (FoundDataLayerInstance != nullptr && !FoundDataLayerInstance->SupportRelabeling())
	{
		OutErrorMessage = LOCTEXT("RenameFailed_NotPermittedOnDataLayer", "This Data Layer does not support renaming");
		return false;
	}

	return true;
}

void SDataLayerTreeLabel::OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
{
	UDataLayerInstance* DataLayerInstance = DataLayerPtr.Get();
	check(DataLayerInstance->SupportRelabeling());
	if (!InLabel.ToString().Equals(DataLayerInstance->GetDataLayerShortName(), ESearchCase::CaseSensitive))
	{
		const FScopedTransaction Transaction(LOCTEXT("SceneOutlinerRenameDataLayerTransaction", "Rename Data Layer"));

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UDataLayerEditorSubsystem::Get()->RenameDataLayer(DataLayerInstance, *InLabel.ToString());
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (WeakSceneOutliner.IsValid())
		{
			WeakSceneOutliner.Pin()->SetKeyboardFocus();
		}
	}
}

void SDataLayerTreeLabel::OnEnterEditingMode()
{
	bInEditingMode = true;
}

void SDataLayerTreeLabel::OnExitEditingMode()
{
	bInEditingMode = false;
}

#undef LOCTEXT_NAMESPACE 