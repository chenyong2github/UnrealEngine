// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerActorSCCColumn.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Views/STreeView.h"
#include "ActorTreeItem.h"
#include "ActorDescTreeItem.h"
#include "ActorFolderTreeItem.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "Widgets/Images/SLayeredImage.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerSourceControlColumn"

class SSourceControlWidget : public SLayeredImage
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
			.Image(FStyleDefaults::GetNoBrush()));

		FSceneOutlinerTreeItemPtr TreeItemPtr = WeakTreeItem.Pin();
		if (TreeItemPtr.IsValid())
		{
			if (FActorTreeItem* ActorItem = TreeItemPtr->CastTo<FActorTreeItem>())
			{
				if (AActor* Actor = ActorItem->Actor.Get())
				{
					if (Actor->IsPackageExternal())
					{
						ExternalPackageName = USourceControlHelpers::PackageFilename(Actor->GetExternalPackage());
					}

					ActorPackingModeChangedDelegateHandle = Actor->OnPackagingModeChanged.AddLambda([this](AActor* InActor, bool bExternal)
						{
							if (bExternal)
							{
								ExternalPackageName = USourceControlHelpers::PackageFilename(InActor->GetExternalPackage());
								ConnectSourceControl();
							}
							else
							{
								ExternalPackageName = FString();
								DisconnectSourceControl();
							}
						});
				}
			}
			else if (FActorFolderTreeItem* ActorFolderItem = TreeItemPtr->CastTo<FActorFolderTreeItem>())
			{
				if (const UActorFolder* ActorFolder = ActorFolderItem->GetActorFolder())
				{
					if (ActorFolder->IsPackageExternal())
					{
						ExternalPackageName = USourceControlHelpers::PackageFilename(ActorFolder->GetExternalPackage());
					}
				}
			}
			else if (FActorDescTreeItem* ActorDescItem = TreeItemPtr->CastTo<FActorDescTreeItem>())
			{
				if (const FWorldPartitionActorDesc* ActorDesc = ActorDescItem->ActorDescHandle.Get())
				{
					ExternalPackageName =  USourceControlHelpers::PackageFilename(ActorDesc->GetActorPackage().ToString());
				}
			}
		}

		if (!ExternalPackageName.IsEmpty())
		{
			ConnectSourceControl();
		}
	}

	~SSourceControlWidget()
	{
		DisconnectSourceControl();
	}

private:

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::ForceUpdate);
		if (SourceControlState.IsValid())
		{
			UpdateSourceControlStateIcon(SourceControlState);

		}
		return FReply::Handled();
	}

	void ConnectSourceControl()
	{
		check(!ExternalPackageName.IsEmpty());

		ISourceControlModule& SCCModule = ISourceControlModule::Get();
		SourceControlProviderChangedDelegateHandle = SCCModule.RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlProviderChanged));
		SourceControlStateChangedDelegateHandle = SCCModule.GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlStateChanged, EStateCacheUsage::Use));

		// Check if there is already a cached state for this item
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::Use);
		if (SourceControlState.IsValid() && !SourceControlState->IsUnknown())
		{
			UpdateSourceControlStateIcon(SourceControlState);
		}
		else
		{
			SCCModule.QueueStatusUpdate(ExternalPackageName);
		}
	}

	void DisconnectSourceControl()
	{
		FSceneOutlinerTreeItemPtr TreeItemPtr = WeakTreeItem.Pin();
		if (TreeItemPtr.IsValid())
		{
			if (FActorTreeItem* ActorItem = TreeItemPtr->CastTo<FActorTreeItem>())
			{
				if (AActor* Actor = ActorItem->Actor.Get())
				{
					Actor->OnPackagingModeChanged.Remove(ActorPackingModeChangedDelegateHandle);
				}
			}
		}
		ISourceControlModule::Get().GetProvider().UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
		ISourceControlModule::Get().UnregisterProviderChanged(SourceControlProviderChangedDelegateHandle);
	}

	void HandleSourceControlStateChanged(EStateCacheUsage::Type CacheUsage)
	{
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, CacheUsage);
		if (SourceControlState.IsValid())
		{
			UpdateSourceControlStateIcon(SourceControlState);
		}
	}

	void HandleSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
	{
		OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
		SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlStateChanged, EStateCacheUsage::Use));
		
		UpdateSourceControlStateIcon(nullptr);

		ISourceControlModule::Get().QueueStatusUpdate(ExternalPackageName);
	}

	void UpdateSourceControlStateIcon(FSourceControlStatePtr SourceControlState)
	{
		if(SourceControlState.IsValid())
		{
			FSlateIcon Icon = SourceControlState->GetIcon();
			
			SetFromSlateIcon(Icon);
		}
		else
		{
			SetImage(nullptr);
			RemoveAllLayers();
		}
	}

	/** The tree item we relate to */
	TWeakPtr<ISceneOutlinerTreeItem> WeakTreeItem;

	/** Reference back to the outliner so we can set visibility of a whole selection */
	TWeakPtr<ISceneOutliner> WeakOutliner;

	/** Cache the items external package name */
	FString ExternalPackageName;

	/** Source control state changed delegate handle */
	FDelegateHandle SourceControlStateChangedDelegateHandle;

	/** Source control provider changed delegate handle */
	FDelegateHandle SourceControlProviderChangedDelegateHandle;

	/** Actor packaging mode changed delegate handle */
	FDelegateHandle ActorPackingModeChangedDelegateHandle;
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
		.DefaultTooltip(FText::FromName(GetColumnID()))
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(this, &FSceneOutlinerActorSCCColumn::GetHeaderIcon)
		];
}

const TSharedRef<SWidget> FSceneOutlinerActorSCCColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->IsA<FActorTreeItem>() || 
		TreeItem->IsA<FActorDescTreeItem>() || 
		(TreeItem->IsA<FActorFolderTreeItem>() && TreeItem->CastTo<FActorFolderTreeItem>()->GetActorFolder()))
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

#undef LOCTEXT_NAMESPACE