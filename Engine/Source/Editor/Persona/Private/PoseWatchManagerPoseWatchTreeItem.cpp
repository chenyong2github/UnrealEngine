// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseWatchManagerPoseWatchTreeItem.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Framework/Commands/UIAction.h"
#include "ToolMenus.h"
#include "EditorStyleSet.h"
#include "PoseWatchManagerDragDrop.h"
#include "SPoseWatchManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Engine/PoseWatch.h"

#define LOCTEXT_NAMESPACE "PoseWatchManagerPoseWatchTreeItem"

struct PERSONA_API SPoseWatchManagerPoseWatchTreeLabel : FPoseWatchManagerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SPoseWatchManagerPoseWatchTreeLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FPoseWatchManagerPoseWatchTreeItem& PoseWatchTreeItem, IPoseWatchManager& PoseWatchManager, const STableRow<FPoseWatchManagerTreeItemPtr>& InRow)
	{
		WeakPoseWatchManager = StaticCastSharedRef<IPoseWatchManager>(PoseWatchManager.AsShared());

		TreeItemPtr = StaticCastSharedRef<FPoseWatchManagerPoseWatchTreeItem>(PoseWatchTreeItem.AsShared());
		WeakPoseWatchPtr = PoseWatchTreeItem.PoseWatch;

		HighlightText = PoseWatchManager.GetFilterHighlightText();

		TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;

		auto MainContent = SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
				.Text(this, &SPoseWatchManagerPoseWatchTreeLabel::GetDisplayText)
				.ToolTipText(this, &SPoseWatchManagerPoseWatchTreeLabel::GetTooltipText)
				.HighlightText(HighlightText)
				.ColorAndOpacity(this, &SPoseWatchManagerPoseWatchTreeLabel::GetForegroundColor)
				.OnTextCommitted(this, &SPoseWatchManagerPoseWatchTreeLabel::OnLabelCommitted)
				.OnVerifyTextChanged(this, &SPoseWatchManagerPoseWatchTreeLabel::OnVerifyItemLabelChanged)
				.OnEnterEditingMode(this, &SPoseWatchManagerPoseWatchTreeLabel::OnEnterEditingMode)
				.OnExitEditingMode(this, &SPoseWatchManagerPoseWatchTreeLabel::OnExitEditingMode)
				.IsSelected(FIsSelected::CreateSP(&InRow, &STableRow<FPoseWatchManagerTreeItemPtr>::IsSelectedExclusively))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(0.0f, 0.f, 3.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &SPoseWatchManagerPoseWatchTreeLabel::GetTypeText)
				.Visibility(this, &SPoseWatchManagerPoseWatchTreeLabel::GetTypeTextVisibility)
				.HighlightText(HighlightText)
			];

		if (WeakPoseWatchManager.Pin())
		{
			PoseWatchTreeItem.RenameRequestEvent.BindSP(InlineTextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		}

		ChildSlot
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FPoseWatchManagerDefaultTreeItemMetrics::IconPadding())
			[
				SNew(SBox)
				.WidthOverride(FPoseWatchManagerDefaultTreeItemMetrics::IconSize())
				.HeightOverride(FPoseWatchManagerDefaultTreeItemMetrics::IconSize())
				[
					SNew(SImage)
					.Image(this, &SPoseWatchManagerPoseWatchTreeLabel::GetIcon)
					.ToolTipText(this, &SPoseWatchManagerPoseWatchTreeLabel::GetIconTooltip)
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
	TWeakPtr<FPoseWatchManagerPoseWatchTreeItem> TreeItemPtr;
	TWeakObjectPtr<UPoseWatch> WeakPoseWatchPtr;
	TAttribute<FText> HighlightText;
	bool bInEditingMode = false;

	FText GetDisplayText() const
	{
		return WeakPoseWatchPtr.Get()->GetLabel();
	}

	FText GetTooltipText() const
	{
		if (!TreeItemPtr.Pin()->IsEnabled())
		{
			return LOCTEXT("PoseWatchDisabled", "This pose watch is disabled because it is not being evaluated");
		}
		return GetDisplayText();
	}

	FText GetTypeText() const
	{
		if (const UPoseWatch* PoseWatch = WeakPoseWatchPtr.Get())
		{
			return FText::FromName(PoseWatch->Node->GetFName());
		}
		return FText();
	}

	EVisibility GetTypeTextVisibility() const
	{
		return HighlightText.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
	}

	const FSlateBrush* GetIcon() const
	{
		return FEditorStyle::Get().GetBrush(TEXT("ClassIcon.PoseAsset"));
	}

	const FSlateBrush* GetIconOverlay() const
	{
		return nullptr;
	}

	FText GetIconTooltip() const
	{
		return LOCTEXT("PoseWatch", "Pose Watch");
	}

	FSlateColor GetForegroundColor() const
	{
		if (auto BaseColor = FPoseWatchManagerCommonLabelData::GetForegroundColor(*TreeItemPtr.Pin()))
		{
			return BaseColor.GetValue();
		}
		return FSlateColor::UseForeground();
	}

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
	{
		return WeakPoseWatchPtr->ValidateLabelRename(InLabel, OutErrorMessage);
	}

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		check(WeakPoseWatchPtr->SetLabel(InLabel));
		WeakPoseWatchManager.Pin()->FullRefresh();
		WeakPoseWatchManager.Pin()->SetKeyboardFocus();
	}

	void OnEnterEditingMode()
	{
		bInEditingMode = true;
	}

	void OnExitEditingMode()
	{
		bInEditingMode = false;
	}
};

const EPoseWatchTreeItemType FPoseWatchManagerPoseWatchTreeItem::Type(EPoseWatchTreeItemType::PoseWatch);

FPoseWatchManagerPoseWatchTreeItem::FPoseWatchManagerPoseWatchTreeItem(UPoseWatch* InPoseWatch)
	: IPoseWatchManagerTreeItem(Type)
	, ID(InPoseWatch)
	, PoseWatch(InPoseWatch)
{
	check(InPoseWatch);
}

FObjectKey FPoseWatchManagerPoseWatchTreeItem::GetID() const
{
	return ID;
}

FString FPoseWatchManagerPoseWatchTreeItem::GetDisplayString() const
{
	return PoseWatch->GetLabel().ToString();
}

bool FPoseWatchManagerPoseWatchTreeItem::IsAssignedFolder() const
{
	return PoseWatch->IsAssignedFolder();
}

TSharedRef<SWidget> FPoseWatchManagerPoseWatchTreeItem::GenerateLabelWidget(IPoseWatchManager& PoseWatchManager, const STableRow<FPoseWatchManagerTreeItemPtr>& InRow)
{
	return SNew(SPoseWatchManagerPoseWatchTreeLabel, *this, PoseWatchManager, InRow);
}

bool FPoseWatchManagerPoseWatchTreeItem::GetVisibility() const
{
	return PoseWatch->GetIsVisible();
}

void FPoseWatchManagerPoseWatchTreeItem::SetIsVisible(const bool bVisible)
{
	PoseWatch->SetIsVisible(bVisible);
}

TSharedPtr<SWidget> FPoseWatchManagerPoseWatchTreeItem::CreateContextMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	UPoseWatch* PoseWatchPtr = PoseWatch.Get();
	MenuBuilder.BeginSection(FName(), LOCTEXT("PoseWatch", "Pose Watch"));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeletePoseWatch", "Delete Pose Watch"),
		LOCTEXT("DeleteFolderDescription", "Delete the selected pose watch"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([PoseWatchPtr]() {
				PoseWatchPtr->OnRemoved();
			})
		)
	);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void FPoseWatchManagerPoseWatchTreeItem::OnRemoved()
{
	PoseWatch->OnRemoved();
}

bool FPoseWatchManagerPoseWatchTreeItem::IsEnabled() const
{
	return PoseWatch->GetIsEnabled();
}

#undef LOCTEXT_NAMESPACE
