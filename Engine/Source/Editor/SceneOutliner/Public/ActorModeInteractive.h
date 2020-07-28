// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorMode.h"

class FActorModeInteractive : public FActorMode
{
public:
	FActorModeInteractive(SSceneOutliner* InSceneOutliner, bool bHideComponents, TWeakObjectPtr<UWorld> InSpecifiedWorldToDisplay = nullptr);
	virtual ~FActorModeInteractive();

	virtual bool IsInteractive() const override { return true; }
private:
	/* Events */

	void OnMapChange(uint32 MapFlags);
	void OnNewCurrentLevel();

	void OnLevelSelectionChanged(UObject* Obj);
	void OnActorLabelChanged(AActor* ChangedActor);
	void OnLevelActorRequestsRename(const AActor* Actor);
};
