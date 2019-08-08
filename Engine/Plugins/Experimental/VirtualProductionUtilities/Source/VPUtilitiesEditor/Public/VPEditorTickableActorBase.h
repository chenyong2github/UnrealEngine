// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

#include "VPEditorTickableActorBase.generated.h"

/**
 * Actor that tick in the Editor viewport with the event EditorTick.
 */
UCLASS(Abstract)
class AVPEditorTickableActorBase : public AActor
{
	GENERATED_BODY()

public:
	AVPEditorTickableActorBase(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintNativeEvent, CallInEditor, BlueprintCallable, Category = "Tick")
	void EditorTick(float DeltaSeconds);

	UFUNCTION(BlueprintNativeEvent, CallInEditor, BlueprintCallable, Category = "Utilities")
	void EditorDestroyed();


	/** If true, actor is ticked even if TickType==LEVELTICK_ViewportsOnly */
	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Destroyed() override;

	/** Sets the LockLocation variable to disable movement from the translation gizmo */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Editor")
	void LockLocation(bool bSetLockLocation);
};
