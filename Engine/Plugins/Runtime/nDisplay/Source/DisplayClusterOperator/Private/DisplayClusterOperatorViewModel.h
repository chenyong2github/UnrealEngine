// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDisplayClusterOperatorViewModel.h"
#include "UObject/WeakObjectPtr.h"

class ADisplayClusterRootActor;
class SDockTab;

/** A view model object that stores any state from the operator panel that should be exposed externally */
class FDisplayClusterOperatorViewModel : public TSharedFromThis<FDisplayClusterOperatorViewModel>, public IDisplayClusterOperatorViewModel
{
public:
	virtual ~FDisplayClusterOperatorViewModel() = default;

	//~ IDisplayClusterOperatorViewModel interface
	virtual bool HasRootActor(bool bEvenIfPendingKill = false) const override { return RootActor.IsValid(bEvenIfPendingKill); }
	virtual ADisplayClusterRootActor* GetRootActor(bool bEvenIfPendingKill = false) const override;
	virtual void SetRootActor(ADisplayClusterRootActor* InRootActor) override;
	virtual FOnActiveRootActorChanged& OnActiveRootActorChanged() override { return RootActorChanged; }

	virtual TSharedPtr<FTabManager> GetTabManager() const override { return TabManager; }

	virtual TSharedPtr<FWorkspaceItem> GetWorkspaceMenuGroup() const override { return WorkspaceItem; }
	//~ End IDisplayClusterOperatorViewModel interface

	TSharedRef<FTabManager> CreateTabManager(const TSharedRef<SDockTab>& MajorTabOwner);
	void ResetTabManager();

private:
	TWeakObjectPtr<ADisplayClusterRootActor> RootActor;
	TSharedPtr<FTabManager> TabManager;
	TSharedPtr<FWorkspaceItem> WorkspaceItem;

	FOnActiveRootActorChanged RootActorChanged;
};