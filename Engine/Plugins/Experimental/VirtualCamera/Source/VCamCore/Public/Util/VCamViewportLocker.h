// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EVCamTargetViewportID.h"
#include "VCamViewportLocker.generated.h"

USTRUCT()
struct FVCamViewportLockState
{
	GENERATED_BODY()
	
	/** Whether the user wants the viewport to be locked */
	UPROPERTY(EditAnywhere, Category = "Viewport", meta = (EditCondition = "!bIsForceLocked"))
	bool bLockViewportToCamera = false;
	
	/** Whether this viewport is currently locked */
	UPROPERTY(Transient)
	bool bIsLockedToViewport = false;

#if WITH_EDITORONLY_DATA
	// This property is editor-only because we use it for EditCondition only
	UPROPERTY(Transient)
	bool bIsForceLocked = false;
#endif
	
	/** Used for editor */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> Backup_ActorLock;
	
	/** Used for gameplay */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> Backup_ViewTarget;
};

/**
 * Keeps track of which viewports are locked
 */
USTRUCT()
struct FVCamViewportLocker
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Viewport")
	TMap<EVCamTargetViewportID, FVCamViewportLockState> Locks {
		{ EVCamTargetViewportID::Viewport1, {} },
		{ EVCamTargetViewportID::Viewport2, {} },
		{ EVCamTargetViewportID::Viewport3, {} },
		{ EVCamTargetViewportID::Viewport4, {} }
	};
};
