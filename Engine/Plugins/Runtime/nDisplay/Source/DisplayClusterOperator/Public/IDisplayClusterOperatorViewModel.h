// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ADisplayClusterRootActor;
class FTabManager;

DECLARE_EVENT_OneParam(IDisplayClusterOperatorViewModel, FOnActiveRootActorChanged, ADisplayClusterRootActor*);

/** Interface for a view model object that stores any state from the operator panel that should be exposed externally */
class DISPLAYCLUSTEROPERATOR_API IDisplayClusterOperatorViewModel
{
public:
	/** Gets whether the view model has been populated with a valid root actor */
	virtual bool HasRootActor() const = 0;

	/** Gets the root actor that is actively being edited by the operator panel */
	virtual ADisplayClusterRootActor* GetRootActor() const = 0;

	/** Sets the root actor that is actively being edited by the operator panel */
	virtual void SetRootActor(ADisplayClusterRootActor* InRootActor) = 0;

	/** Gets the event handler that is raised when the operator panel changes the root actor being operated on */
	virtual FOnActiveRootActorChanged& OnActiveRootActorChanged() = 0;

	/** Gets the tab manager of the active operator panel, if there is an open operator panel */
	virtual TSharedPtr<FTabManager> GetTabManager() const = 0;
};