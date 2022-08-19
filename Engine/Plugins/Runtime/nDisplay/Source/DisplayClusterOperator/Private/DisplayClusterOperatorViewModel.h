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
	virtual bool HasRootActor() const override { return RootActor.IsValid(); }
	virtual ADisplayClusterRootActor* GetRootActor() const override { return RootActor.IsValid() ? RootActor.Get() : nullptr; }
	virtual void SetRootActor(ADisplayClusterRootActor* InRootActor) override;
	virtual FOnActiveRootActorChanged& OnActiveRootActorChanged() override { return RootActorChanged; }

	virtual TSharedPtr<FTabManager> GetTabManager() const override { return TabManager; }
	//~ End IDisplayClusterOperatorViewModel interface

	TSharedRef<FTabManager> CreateTabManager(const TSharedRef<SDockTab>& MajorTabOwner);
	void ResetTabManager();

private:
	TWeakObjectPtr<ADisplayClusterRootActor> RootActor;
	TSharedPtr<FTabManager> TabManager;

	FOnActiveRootActorChanged RootActorChanged;
};