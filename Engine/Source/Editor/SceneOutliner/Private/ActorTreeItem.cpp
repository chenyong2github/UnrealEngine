// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorTreeItem.h"
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
#include "LevelInstance/LevelInstanceActor.h"

#define LOCTEXT_NAMESPACE "SceneOutliner_ActorTreeItem"

const FSceneOutlinerTreeItemType FActorTreeItem::Type(&ISceneOutlinerTreeItem::Type);

struct SActorTreeLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SActorTreeLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FActorTreeItem& ActorItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());

		TreeItemPtr = StaticCastSharedRef<FActorTreeItem>(ActorItem.AsShared());
		ActorPtr = ActorItem.Actor;

		HighlightText = SceneOutliner.GetFilterHighlightText();

		TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;

		auto MainContent = SNew(SHorizontalBox)

			// Main actor label
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
				.Text(this, &SActorTreeLabel::GetDisplayText)
				.ToolTipText(this, &SActorTreeLabel::GetTooltipText)
				.HighlightText(HighlightText)
				.ColorAndOpacity(this, &SActorTreeLabel::GetForegroundColor)
				.OnTextCommitted(this, &SActorTreeLabel::OnLabelCommitted)
				.OnVerifyTextChanged(this, &SActorTreeLabel::OnVerifyItemLabelChanged)
				.IsSelected(FIsSelected::CreateSP(&InRow, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively))
				.IsReadOnly_Lambda([Item = ActorItem.AsShared(), this]()
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
				.Text(this, &SActorTreeLabel::GetTypeText)
				.Visibility(this, &SActorTreeLabel::GetTypeTextVisibility)
				.HighlightText(HighlightText)
			];

		if (WeakSceneOutliner.Pin()->GetMode()->IsInteractive())
		{
			ActorItem.RenameRequestEvent.BindSP(InlineTextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
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
						.Image(this, &SActorTreeLabel::GetIcon)
						.ToolTipText(this, &SActorTreeLabel::GetIconTooltip)
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
	TWeakPtr<FActorTreeItem> TreeItemPtr;
	TWeakObjectPtr<AActor> ActorPtr;
	TAttribute<FText> HighlightText;

	FText GetDisplayText() const
	{
		const AActor* Actor = ActorPtr.Get();
		if (const ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(Actor))
		{
			if (LevelInstanceActor->IsDirty())
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("ActorLabel"), FText::FromString(LevelInstanceActor->GetActorLabel()));
				Args.Add(TEXT("EditTag"), LOCTEXT("EditingLevelInstanceLabel", "*"));
				return FText::Format(LOCTEXT("LevelInstanceDisplay", "{ActorLabel}{EditTag}"), Args);
			}
		}

		return Actor ? FText::FromString(Actor->GetActorLabel()) : LOCTEXT("ActorLabelForMissingActor", "(Deleted Actor)");
	}

	FText GetTooltipText() const
	{
		if (const AActor* Actor = ActorPtr.Get())
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ID_Name"), LOCTEXT("CustomColumnMode_InternalName", "ID Name"));
			Args.Add(TEXT("Name"), FText::FromString(Actor->GetName()));
			return FText::Format(LOCTEXT("ActorNameTooltip", "{ID_Name}: {Name}"), Args);
		}

		return FText();
	}

	FText GetTypeText() const
	{
		if (const AActor* Actor = ActorPtr.Get())
		{
			return FText::FromName(Actor->GetClass()->GetFName());
		}

		return FText();
	}

	EVisibility GetTypeTextVisibility() const
	{
		return HighlightText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	}

	const FSlateBrush* GetIcon() const
	{
		if (const AActor* Actor = ActorPtr.Get())
		{
			if (WeakSceneOutliner.IsValid())
			{
				FName IconName = Actor->GetCustomIconName();
				if (IconName == NAME_None)
				{
					IconName = Actor->GetClass()->GetFName();
				}

				const FSlateBrush* CachedBrush = WeakSceneOutliner.Pin()->GetCachedIconForClass(IconName);
				if (CachedBrush != nullptr)
				{
					return CachedBrush;
				}
				else
				{

					const FSlateBrush* FoundSlateBrush = FClassIconFinder::FindIconForActor(Actor);
					WeakSceneOutliner.Pin()->CacheIconForClass(IconName, FoundSlateBrush);
					return FoundSlateBrush;
				}
			}
			else
			{
				return nullptr;
			}
		}
		else
		{
			return nullptr;
		}
	}

	const FSlateBrush* GetIconOverlay() const
	{
		static const FName SequencerActorTag(TEXT("SequencerActor"));

		if (const AActor* Actor = ActorPtr.Get())
		{
			if (Actor->ActorHasTag(SequencerActorTag))
			{
				return FEditorStyle::GetBrush("Sequencer.SpawnableIconOverlay");
			}
		}
		return nullptr;
	}

	FText GetIconTooltip() const
	{
		auto TreeItem = TreeItemPtr.Pin();
		if (!TreeItem.IsValid())
		{
			return FText();
		}

		FText ToolTipText;
		if (AActor* Actor = ActorPtr.Get())
		{
			ToolTipText = FText::FromString(Actor->GetClass()->GetName());
			if (WeakSceneOutliner.Pin()->GetMode()->IsInteractive())
			{
				USceneComponent* RootComponent = Actor->GetRootComponent();
				if (RootComponent)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("ActorClassName"), ToolTipText);

					if (RootComponent->Mobility == EComponentMobility::Static)
					{
						ToolTipText = FText::Format(LOCTEXT("ComponentMobility_Static", "{ActorClassName} with static mobility"), Args);
					}
					else if (RootComponent->Mobility == EComponentMobility::Stationary)
					{
						ToolTipText = FText::Format(LOCTEXT("ComponentMobility_Stationary", "{ActorClassName} with stationary mobility"), Args);
					}
					else if (RootComponent->Mobility == EComponentMobility::Movable)
					{
						ToolTipText = FText::Format(LOCTEXT("ComponentMobility_Movable", "{ActorClassName} with movable mobility"), Args);
					}
				}
			}
		}

		return ToolTipText;
	}

	FSlateColor GetForegroundColor() const
	{
		AActor* Actor = ActorPtr.Get();

		// Color LevelInstances differently if they are being edited
		if (const ALevelInstance* LevelInstanceActor = Cast<ALevelInstance>(Actor))
		{
			if (LevelInstanceActor->IsEditing() && !LevelInstanceActor->IsSelected())
			{
				return FAppStyle::Get().GetSlateColor("Colors.AccentGreen");
			}
			else
			{
				return FSlateColor::UseForeground();
			}
		}


		auto TreeItem = TreeItemPtr.Pin();
		if (auto BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItem))
		{
			return BaseColor.GetValue();
		}

		if (!Actor)
		{
			// Deleted actor!
			return FLinearColor(0.2f, 0.2f, 0.25f);
		}

		UWorld* OwningWorld = Actor->GetWorld();
		if (!OwningWorld)
		{
			// Deleted world!
			return FLinearColor(0.2f, 0.2f, 0.25f);
		}

		const bool bRepresentingPIEWorld = TreeItem->Actor->GetWorld()->IsPlayInEditor();
		if (bRepresentingPIEWorld && !TreeItem->bExistsInCurrentWorldAndPIE)
		{
			// Highlight actors that are exclusive to PlayWorld
			return FLinearColor(0.9f, 0.8f, 0.4f);
		}

		// also darken items that are non selectable in the active mode(s)
		const bool bInSelected = true;
		const bool bSelectEvenIfHidden = true;		// @todo outliner: Is this actually OK?
		if (!GEditor->CanSelectActor(Actor, bInSelected, bSelectEvenIfHidden))
		{
			return FSceneOutlinerCommonLabelData::DarkColor;
		}

		return FSlateColor::UseForeground();
	}

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
	{
		return FActorEditorUtils::ValidateActorName(InLabel, OutErrorMessage);
	}

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		auto* Actor = ActorPtr.Get();
		if (Actor && Actor->IsActorLabelEditable() && !InLabel.ToString().Equals(Actor->GetActorLabel(), ESearchCase::CaseSensitive))
		{
			const FScopedTransaction Transaction(LOCTEXT("SceneOutlinerRenameActorTransaction", "Rename Actor"));
			FActorLabelUtilities::RenameExistingActor(Actor, InLabel.ToString());

			auto Outliner = WeakSceneOutliner.Pin();
			if (Outliner.IsValid())
			{
				Outliner->SetKeyboardFocus();
			}
		}
	}
};

FActorTreeItem::FActorTreeItem(AActor* InActor)
	: ISceneOutlinerTreeItem(Type)
	, Actor(InActor)
	, ID(InActor)
{
	bExistsInCurrentWorldAndPIE = GEditor->ObjectsThatExistInEditorWorld.Get(InActor);
}

FSceneOutlinerTreeItemID FActorTreeItem::GetID() const
{
	return ID;
}

FString FActorTreeItem::GetDisplayString() const
{
	const AActor* ActorPtr = Actor.Get();
	return ActorPtr ? ActorPtr->GetActorLabel() : LOCTEXT("ActorLabelForMissingActor", "(Deleted Actor)").ToString();
}

bool FActorTreeItem::CanInteract() const
{
	AActor* ActorPtr = Actor.Get();
	if (!ActorPtr || !Flags.bInteractive)
	{
		return false;
	}

	const bool bInSelected = true;
	const bool bSelectEvenIfHidden = true;		// @todo outliner: Is this actually OK?
	if (!GEditor->CanSelectActor(ActorPtr, bInSelected, bSelectEvenIfHidden))
	{
		return false;
	}

	return true;
}

TSharedRef<SWidget> FActorTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SActorTreeLabel, *this, Outliner, InRow);
}

void FActorTreeItem::OnVisibilityChanged(const bool bNewVisibility)
{
	// Save the actor to the transaction buffer to support undo/redo, but do
	// not call Modify, as we do not want to dirty the actor's package and
	// we're only editing temporary, transient values
	SaveToTransactionBuffer(Actor.Get(), false);
	Actor->SetIsTemporarilyHiddenInEditor(!bNewVisibility);
}

bool FActorTreeItem::GetVisibility() const
{
	return Actor.IsValid() && !Actor->IsTemporarilyHiddenInEditor(true);
}

#undef LOCTEXT_NAMESPACE
