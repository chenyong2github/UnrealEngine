// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerActorSCCColumn.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/STreeView.h"
#include "ActorTreeItem.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerSourceControlColumn"

class SSourceControlWidget : public SImage
{
public:
	SLATE_BEGIN_ARGS(SSourceControlWidget) {}
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs, TWeakPtr<ISceneOutliner> InWeakOutliner, TWeakPtr<ISceneOutlinerTreeItem> InWeakTreeItem)
	{
		WeakTreeItem = InWeakTreeItem;
		WeakOutliner = InWeakOutliner;

		SImage::Construct(
			SImage::FArguments()
			.ColorAndOpacity(this, &SSourceControlWidget::GetForegroundColor)
			.Image(this, &SSourceControlWidget::GetBrush));

		SCCStateBrush = nullptr;

		FSceneOutlinerTreeItemPtr TreeItemPtr = WeakTreeItem.Pin();
		if (TreeItemPtr.IsValid())
		{
			if (FActorTreeItem* ActorItem = TreeItemPtr->CastTo<FActorTreeItem>())
			{
				AActor* Actor = ActorItem->Actor.Get();
				if (Actor && Actor->GetLevel() && Actor->GetLevel()->bUseExternalActors)
				{
					ExternalPackageName = USourceControlHelpers::PackageFilename(Actor->GetExternalPackage());
				}
			}
		}

		if (!ExternalPackageName.IsEmpty())
		{
			ISourceControlModule& SCCModule = ISourceControlModule::Get();
			SourceControlProviderChangedDelegateHandle = SCCModule.RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlProviderChanged));
			SourceControlStateChangedDelegateHandle = SCCModule.GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlStateChanged));

			// Check if there is already a cached state for this item
			FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::Use);
			if (SourceControlState.IsValid() && SourceControlState->GetSmallIconName() != NAME_None)
			{
				SCCStateBrush = FEditorStyle::GetBrush(SourceControlState->GetSmallIconName());
			}
			else
			{
				SCCModule.QueueStatusUpdate(ExternalPackageName);
			}
		}
	}

	~SSourceControlWidget()
	{
		ISourceControlModule::Get().GetProvider().UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
		ISourceControlModule::Get().UnregisterProviderChanged(SourceControlProviderChangedDelegateHandle);
	}

private:

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::ForceUpdate);
		if (SourceControlState.IsValid())
		{
			SCCStateBrush = FEditorStyle::GetBrush(SourceControlState->GetSmallIconName());
		}
		return FReply::Handled();
	}

	void HandleSourceControlStateChanged()
	{
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::Use);
		if (SourceControlState.IsValid())
		{
			SCCStateBrush = FEditorStyle::GetBrush(SourceControlState->GetSmallIconName());
		}
	}

	void HandleSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
	{
		OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
		SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlStateChanged));
		
		SCCStateBrush = nullptr;
		ISourceControlModule::Get().QueueStatusUpdate(ExternalPackageName);
	}

	/** Get the brush for this widget */
	const FSlateBrush* GetBrush() const
	{
		return SCCStateBrush;
	}

	/** The tree item we relate to */
	TWeakPtr<ISceneOutlinerTreeItem> WeakTreeItem;

	/** Reference back to the outliner so we can set visibility of a whole selection */
	TWeakPtr<ISceneOutliner> WeakOutliner;

	/** Cached brush for the source control state */
	const FSlateBrush* SCCStateBrush;

	/** Cache the items external package name */
	FString ExternalPackageName;

	/** Source control state changed delegate handle */
	FDelegateHandle SourceControlStateChangedDelegateHandle;

	/** Source control provider changed delegate handle */
	FDelegateHandle SourceControlProviderChangedDelegateHandle;
};

FName FSceneOutlinerActorSCCColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FSceneOutlinerActorSCCColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(this, &FSceneOutlinerActorSCCColumn::GetHeaderIcon)
		];
}

const TSharedRef<SWidget> FSceneOutlinerActorSCCColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->IsA<FActorTreeItem>())
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SSourceControlWidget, WeakSceneOutliner, TreeItem)
			];
	}
	return SNullWidget::NullWidget;
}

const FSlateBrush* FSceneOutlinerActorSCCColumn::GetHeaderIcon() const
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		return FEditorStyle::GetBrush("SourceControl.StatusIcon.On");
	}
	else
	{
		return FEditorStyle::GetBrush("SourceControl.StatusIcon.Off");
	}
}