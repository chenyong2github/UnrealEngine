// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VPViewportTickableActorBase.h"

#include "VPEditorTickableActorBase.generated.h"

/**
 * Actor that tick in the Editor viewport with the event EditorTick.
 */
UCLASS(Abstract)
class VPUTILITIESEDITOR_API AVPEditorTickableActorBase : public AVPViewportTickableActorBase
{
	GENERATED_BODY()

public:

	/** Sets the LockLocation variable to disable movement from the translation gizmo */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Editor")
	void LockLocation(bool bSetLockLocation);
};
