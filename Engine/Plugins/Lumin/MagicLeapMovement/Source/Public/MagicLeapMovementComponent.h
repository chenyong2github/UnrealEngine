// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "GameFramework/MovementComponent.h"
#include "MagicLeapMovementTypes.h"
#include "Lumin/CAPIShims/LuminAPIMovement.h"
#include "MagicLeapMovementComponent.generated.h"

/**
	LuminOS designed spatial movement for objects to feel like they have mass and inertia by tilting and swaying during
	movement, but still respecting the user's desired placement. The component also offers two options for handling collision during movement (hard and soft).
	Hard collisions are basically the blocking collisions from the Engine.
	Soft collisions allow a degree of interpenetration before hitting an impenetrable core as defined by movement settings.
	Soft collisions are possible only with components that have an "Overlap" response for the moving component.
	By default the root SceneComponent of the owning Actor of this component is updated. Component to move can be changed by
	calling SetUpdatedComponent().
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPMOVEMENT_API UMagicLeapMovementComponent : public UMovementComponent
{
	GENERATED_BODY()

public:

	UMagicLeapMovementComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
		Sets the SceneComponent whose transform will control the movement according to the provided movement settings.
		@param InMovementController The SceneComponent to be used for movement. MotionControllerComponent is a common input here.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movement | MagicLeap")
	void AttachObjectToMovementController(const USceneComponent* InMovementController);

	/**
		Detaches the currently selected movement controller.
		@param bResolvePendingSoftCollisions If true, soft collisions will continue to resolve after detaching.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movement | MagicLeap")
	void DetachObject(bool bResolvePendingSoftCollisions = true);

	/**
		Gets the default settings.
		@param OutSettings The default settings.
		@return True if the default settings were retrieved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movement | MagicLeap")
	bool GetDefaultSettings(FMagicLeapMovementSettings& OutSettings) const;

	/** 
		Changes the depth offset of the object from the user's headpose (3Dof) or pointing device (6Dof).
		@param DeltaDepth The change in the depth offset in cm (can be negative).
		@return True if the depth was changed, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movement | MagicLeap")
	bool ChangeDepth(float DeltaDepth);

	/** 
		Changes the rotation about the up-axis of the object being moved.
		@param DeltaDegrees The change (in degrees) of the rotation about the up axis.
		@return True if the rotation was changed, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movement | MagicLeap")
	bool ChangeRotation(float DeltaDegrees);

	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	/** The settings to be used when transforming the attached object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	FMagicLeapMovementSettings MovementSettings;

	/** Additional settings to be used when transforming the attached object in 3DOF mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	FMagicLeapMovement3DofSettings Settings3Dof;

	/** Additional settings to be used when transforming the attached object in 6DOF mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	FMagicLeapMovement6DofSettings Settings6Dof;

	/** In case of a blocking hit, slide the attached object along the hit surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	bool bSlideAlongSurfaceOnBlockingHit;

	/** If true, attached object will have a degree of interpenetration with the overlapping objects.
	 *  This changes the regular interaction of the attached object with overlapping components and gradually
	 *  springs out of the overlapping volume instead of smoothly going through it. 
	 * "Generate Overlap Event" should be enabled to use this feature.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|MagicLeap")
	bool bSoftCollideWithOverlappingActors;

private:
	void EndMovementSession(bool bResolvePendingSoftCollisions, float DeltaTime);

	UFUNCTION()
	void OnUpdatedComponentOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	UFUNCTION()
	void OnUpdatedComponentOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	const USceneComponent* MovementController;
	EMagicLeapMovementType CurrentMovementType;
	bool bSoftCollisionsPending;

#if WITH_MLSDK
	void Update3DofMovement(float DeltaTime, MLMovementObject& MovementObject);
	void Update6DofMovement(float DeltaTime, MLMovementObject& MovementObject);

	void UpdateHardCollision(const FVector& ImpactNormal);

	void SetMovementTransform(const MLMovementObject& MovementObject, bool bRegisterHardCollision = true);
	void GetMovementTransform(MLMovementObject& MovementObject) const;

	void GetMovement3DofControls(MLMovement3DofControls& Movement3DofControls) const;
	void GetMovement6DofControls(MLMovement6DofControls& Movement6DofControls) const;

	void EndCollision(MLHandle& CollisionHandle);
	void EndAllSoftCollisions();

	MLHandle SessionHandle;
	MLHandle HardCollisionHandle;

	TMap<UPrimitiveComponent*, MLHandle> SoftCollisions;
	TArray<MLHandle> SoftCollisionsPendingDestroy;
#endif // WITH_MLSDK
};

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapMovement, Verbose, All);
