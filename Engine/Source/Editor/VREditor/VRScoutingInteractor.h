// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "InputCoreTypes.h"
#include "HeadMountedDisplayTypes.h"
#include "VREditorInteractor.h"
#include "VRScoutingInteractor.generated.h"

class AActor;
class UStaticMesh;
class UStaticMeshSocket;

/**
 * Represents the interactor in the world
 */
UCLASS(Abstract)
class VREDITOR_API UVRScoutingInteractor: public UVREditorInteractor
{
	GENERATED_BODY()

public:

	/** Default constructor */
	UVRScoutingInteractor();

	/** Gets the trackpad slide delta */
	virtual float GetSlideDelta_Implementation() const override;

	/** Sets up all components */
	virtual void SetupComponent_Implementation( AActor* OwningActor ) override;

	// IViewportInteractorInterface overrides
	virtual void Shutdown_Implementation() override;

	/** Sets the gizmo mode for selected object */
	UFUNCTION(BlueprintCallable, Category = "Scouting")
	void SetGizmoMode(EGizmoHandleTypes InGizmoMode);

	/** Gets the gizmo mode for selected object */
	UFUNCTION(BlueprintCallable, Category = "Scouting")
	EGizmoHandleTypes GetGizmoMode() const;

	/** Gets all actors that are selected in the world editor */
	UFUNCTION(BlueprintCallable, Category = "Scouting")
	static TArray<AActor*> GetSelectedActors();

	/** Shown in Navigation mode */
	UPROPERTY(Category = Interactor, EditAnywhere, BlueprintReadOnly)
	class UStaticMeshComponent* FlyingIndicatorComponent;
};
