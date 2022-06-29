// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlWidget.h"

#include "ActorTreeItem.h"
#include "ActorDescTreeItem.h"
#include "ActorFolderTreeItem.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

void SSourceControlWidget::Construct(const FArguments& InArgs, TWeakPtr<ISceneOutliner> InWeakOutliner, TWeakPtr<ISceneOutlinerTreeItem> InWeakTreeItem)
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
					ExternalPackage = Actor->GetExternalPackage();
				}

				ActorPackingModeChangedDelegateHandle = Actor->OnPackagingModeChanged.AddLambda([this](AActor* InActor, bool bExternal)
				{
					if (bExternal)
					{
						ExternalPackageName = USourceControlHelpers::PackageFilename(InActor->GetExternalPackage());
						ExternalPackage = InActor->GetExternalPackage();
						ConnectSourceControl();
					}
					else
					{
						ExternalPackageName = FString();
						ExternalPackage = nullptr;
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
					ExternalPackage = ActorFolder->GetExternalPackage();
				}
			}
		}
		else if (FActorDescTreeItem* ActorDescItem = TreeItemPtr->CastTo<FActorDescTreeItem>())
		{
			if (const FWorldPartitionActorDesc* ActorDesc = ActorDescItem->ActorDescHandle.Get())
			{
				ExternalPackageName = USourceControlHelpers::PackageFilename(ActorDesc->GetActorPackage().ToString());
				ExternalPackage = FindPackage(nullptr, *ActorDesc->GetActorPackage().ToString());
			}
		}
	}

	if (!ExternalPackageName.IsEmpty())
	{
		ConnectSourceControl();
	}
}

SSourceControlWidget::~SSourceControlWidget()
{
	DisconnectSourceControl();
}

FReply SSourceControlWidget::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::ForceUpdate);
	if (SourceControlState.IsValid())
	{
		UpdateSourceControlStateIcon(SourceControlState);
	}
	return FReply::Handled();
}

FSourceControlStatePtr SSourceControlWidget::GetSourceControlState()
{
	return ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, EStateCacheUsage::Use);
}

void SSourceControlWidget::ConnectSourceControl()
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

void SSourceControlWidget::DisconnectSourceControl()
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

void SSourceControlWidget::HandleSourceControlStateChanged(EStateCacheUsage::Type CacheUsage)
{
	FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(ExternalPackageName, CacheUsage);
	if (SourceControlState.IsValid())
	{
		UpdateSourceControlStateIcon(SourceControlState);
	}
}

void SSourceControlWidget::HandleSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
	SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlWidget::HandleSourceControlStateChanged, EStateCacheUsage::Use));
	
	UpdateSourceControlStateIcon(nullptr);

	ISourceControlModule::Get().QueueStatusUpdate(ExternalPackageName);
}

void SSourceControlWidget::UpdateSourceControlStateIcon(FSourceControlStatePtr SourceControlState)
{
	if(SourceControlState.IsValid())
	{
		FSlateIcon Icon = SourceControlState->GetIcon();
		SetFromSlateIcon(Icon);
		SetToolTipText(SourceControlState->GetDisplayTooltip());
	}
	else
	{
		SetImage(nullptr);
		SetToolTipText(FText::GetEmpty());
		RemoveAllLayers();
	}
}
