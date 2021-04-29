// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "GameFramework/RootMotionSource.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Chaos/SimCallbackObject.h"

struct FFindFloorResult;
struct FStepDownResult;

struct FUpdatedComponentAsyncOutput
{
	// TODO Overlaps: When overlapping, if UpdatedComponent enables overlaps, we cannot read from
	// overlapped component to determine if it needs overlap event, because we are not on game thread.
	// We cache overlap regardless if other component enables Overlap events,
	// and will have to cull them on game thread when applying output.
	// TODO see ShouldIngoreOverlapResult, check WorldSEttings and ActorInitialized condition.
	TArray<FOverlapInfo> SpeculativeOverlaps;

	// stolen from prim component TODO 
	int32 IndexOfOverlap(const FOverlapInfo& SearchItem)
	{
		return SpeculativeOverlaps.IndexOfByPredicate(FFastOverlapInfoCompare(SearchItem));
	}

	// stolen from prim component TODO dedupe
	// Helper for adding an FOverlapInfo uniquely to an Array, using IndexOfOverlapFast and knowing that at least one overlap is valid (non-null
	void AddUniqueSpeculativeOverlap(const FOverlapInfo& NewOverlap)
	{
		if (IndexOfOverlap(NewOverlap) == INDEX_NONE)
		{
			SpeculativeOverlaps.Add(NewOverlap);
		}
	}
};


struct FCharacterMovementAsyncOutput : public Chaos::FSimCallbackOutput
{
	void Reset() { }

	bool bWasSimulatingRootMotion;
	EMovementMode MovementMode;
	EMovementMode GroundMovementMode;
	uint8 CustomMovementMode;
	FVector Acceleration;
	float AnalogInputModifier;
	FVector LastUpdateLocation;
	FQuat LastUpdateRotation;
	FVector LastUpdateVelocity;
	bool bForceNextFloorCheck;
	FRootMotionSourceGroup CurrentRootMotion; //  TODO not threadsafe, has pointers in there...
	FVector Velocity;
	bool bDeferUpdateBasedMovement;
	EMoveComponentFlags MoveComponentFlags;
	FVector PendingForceToApply;
	FVector PendingImpulseToApply;
	FVector PendingLaunchVelocity;
	bool bCrouchMaintainsBaseLocation;
	bool bJustTeleported;
	float ScaledCapsuleRadius;
	float ScaledCapsuleHalfHeight;
	bool bIsCrouched;
	bool bWantsToCrouch;
	bool bMovementInProgress;
	FFindFloorResult CurrentFloor;
	bool bHasRequestedVelocity;
	bool bRequestedMoveWithMaxSpeed;
	FVector RequestedVelocity;
	int32 NumJumpApexAttempts;
	FRootMotionMovementParams RootMotionParams;
	bool bShouldApplyDeltaToMeshPhysicsTransforms; // See UpdateBasedMovement
	FVector DeltaPosition;
	FQuat DeltaQuat;
	float DeltaTime;
	FVector OldVelocity; // Cached for CallMovementUpdateDelegate
	FVector OldLocation;

	// See MaybeUpdateBasedMovement
	// TODO MovementBase, handle tick group changes properly
	bool bShouldDisablePostPhysicsTick;
	bool bShouldEnablePostPhysicsTick;
	bool bShouldAddMovementBaseTickDependency;
	bool bShouldRemoveMovementBaseTickDependency;
	UPrimitiveComponent* NewMovementBase; // call SetBase
	AActor* NewMovementBaseOwner; // make sure this is set whenever base component is. TODO

	// UpdatedComponent data
	FUpdatedComponentAsyncOutput UpdatedComponent;

	// Character Ownner Data
	FRotator CharacterOwnerRotation; 
	int32 JumpCurrentCountPreJump;
	int32 JumpCurrentCount;
	float JumpForceTimeRemaining;
	bool bWasJumping;
	bool bPressedJump;
	float JumpKeyHoldTime;
};

// Don't read into this part too much it needs to be changed.
struct FCachedMovementBaseAsyncData
{
	// data derived from movement base
	UPrimitiveComponent* CachedMovementBase; // Do not access, this was input movement base, only here so I could ensure when it changed.
	
	// Invalid if movement base changes.
	bool bMovementBaseUsesRelativeLocationCached;
	bool bMovementBaseIsSimulatedCached;
	bool bMovementBaseIsValidCached;
	bool bMovementBaseOwnerIsValidCached;
	bool bMovementBaseIsDynamicCached;

	bool bIsBaseTransformValid;
	FQuat BaseQuat;
	FVector BaseLocation;
	FQuat OldBaseQuat;
	FVector OldBaseLocation;

	// Calling this before reading movement base data, as if it changed during tick, we are using stale data,
	// can't read from game thread. Need to think about this more.
	void Validate(const FCharacterMovementAsyncOutput& Output) const
	{
		ensure(Output.NewMovementBase == CachedMovementBase);
	}
};

// Data and implementation that lives on movement component's character owner
struct FCharacterAsyncInput
{
	virtual ~FCharacterAsyncInput() {}

	// Character owner input
	float JumpMaxHoldTime;
	int32 JumpMaxCount;
	ENetRole  LocalRole;
	ENetRole RemoteRole;
	bool bIsLocallyControlled;
	bool bIsPlayingNetworkedRootMontage;
	bool bUseControllerRotationPitch;
	bool bUseControllerRotationYaw;
	bool bUseControllerRotationRoll;
	FRotator ControllerDesiredRotation;

	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime, const FCharacterMovementAsyncInput& Input, FCharacterMovementAsyncOutput& Output) const;
	virtual void CheckJumpInput(float DeltaSeconds, const FCharacterMovementAsyncInput& Input, FCharacterMovementAsyncOutput& Output) const;
	virtual void ClearJumpInput(float DeltaSeconds, const FCharacterMovementAsyncInput& Input, FCharacterMovementAsyncOutput& Output) const;
	virtual bool CanJump(const FCharacterMovementAsyncInput& Input, FCharacterMovementAsyncOutput& Output) const;
	virtual void ResetJumpState(const FCharacterMovementAsyncInput& Input, FCharacterMovementAsyncOutput& Output) const;
};

// Represents the UpdatedComponent's state and implementation
struct FUpdatedComponentAsyncInput
{
	virtual ~FUpdatedComponentAsyncInput() {}

	// base Implementation from PrimitiveComponent, this will be wrong if UpdatedComponent is SceneComponent.
	virtual bool MoveComponent(const FVector& Delta, const FQuat& NewRotationQuat, bool bSweep, FHitResult* OutHit, EMoveComponentFlags MoveFlags, ETeleportType Teleport, const FCharacterMovementAsyncInput& Input, FCharacterMovementAsyncOutput& Output) const;
	virtual bool AreSymmetricRotations(const FQuat& A, const FQuat& B, const FVector& Scale3D) const;

	// TODO Dedupe with PrimitiveComponent where possible
	static void PullBackHit(FHitResult& Hit, const FVector& Start, const FVector& End, const float Dist);
	static bool ShouldCheckOverlapFlagToQueueOverlaps(const UPrimitiveComponent& ThisComponent);
	static bool ShouldIgnoreHitResult(const UWorld* InWorld, FHitResult const& TestHit, FVector const& MovementDirDenormalized, const AActor* MovingActor, EMoveComponentFlags MoveFlags);
	static bool ShouldIgnoreOverlapResult(const UWorld* World, const AActor* ThisActor, const UPrimitiveComponent& ThisComponent, const AActor* OtherActor, const UPrimitiveComponent& OtherComponent);

	FVector GetForwardVector() const
	{
		return GetRotation().GetAxisX();
	}

	// Async API, physics thread only.
	void SetPosition(const FVector& Position) const;
	FVector GetPosition() const;
	void SetRotation(const FQuat& Rotation) const;
	FQuat GetRotation() const;

	bool bIsQueryCollisionEnabled;
	bool bIsSimulatingPhysics;

	// PrimComponent->InitSweepCollisionParams + modified IgnoreTouches and trace tag.
	FComponentQueryParams MoveComponentQueryParams;
	FCollisionResponseParams MoveComponentCollisionResponseParams;

	UPrimitiveComponent* UpdatedComponent; // TODO Threadsafe make sure we aren't accessing this anywhere.
	FPhysicsActorHandle PhysicsHandle;

	FCollisionShape CollisionShape;
	bool bForceGatherOverlaps; // !ShouldCheckOverlapFlagToQueueOverlaps(*PrimitiveComponent);
	bool bGatherOverlaps; // GetGenerateOverlapEvents() || bForceGatherOverlaps
	FVector Scale;
};

/*
* Contains all input and implementation required to run async character movement.
* Base implementation is from CharacterMovementComponent.
* Contains 'CharacterInput' and 'UpdatedComponentInput' represent data/impl of Character and our UpdatedComponent.
* All input is const, non-const data goes in output. 'InitialOutput' member is copied to output before sim to initialize non-const data.
*/
struct FCharacterMovementAsyncInput : public Chaos::FSimCallbackInput
{
	// Has this been filled out?
	bool bInitialized = false;

	FVector InputVector;
	ENetworkSmoothingMode NetworkSmoothingMode;
	bool bIsNetModeClient; // shared state, TODO remove.
	bool bWasSimulatingRootMotion;
	bool bRunPhysicsWithNoController;
	bool bForceMaxAccel;
	float MaxAcceleration;
	float MinAnalogWalkSpeed;
	bool bIgnoreBaseRotation;
	bool bOrientRotationToMovement;
	bool bUseControllerDesiredRotation;
	bool bConstrainToPlane;
	FVector PlaneConstraintOrigin;
	FVector PlaneConstraintNormal;
	bool bHasValidData; // TODO look into if this can become invalid during sim
	float MaxStepHeight;
	bool bAlwaysCheckFloor;
	float WalkableFloorZ;
	bool bUseFlatBaseForFloorChecks;
	float GravityZ;
	bool bCanEverCrouch;
	int32 MaxSimulationIterations;
	float MaxSimulationTimeStep;
	bool bMaintainHorizontalGroundVelocity;
	bool bUseSeparateBrakingFriction;
	float GroundFriction;
	float BrakingFrictionFactor;
	float BrakingFriction;
	float BrakingSubStepTime;
	float BrakingDecelerationWalking;
	float BrakingDecelerationFalling;
	float BrakingDecelerationSwimming;
	float BrakingDecelerationFlying;
	float MaxDepenetrationWithGeometryAsProxy;
	float MaxDepenetrationWithGeometry;
	float MaxDepenetrationWithPawn;
	float MaxDepenetrationWithPawnAsProxy;
	bool bCanWalkOffLedgesWhenCrouching;
	bool bCanWalkOffLedges;
	float LedgeCheckThreshold;
	float PerchRadiusThreshold;
	float AirControl;
	float AirControlBoostMultiplier;
	float AirControlBoostVelocityThreshold;
	bool bApplyGravityWhileJumping;
	float PhysicsVolumeTerminalVelocity;
	int32 MaxJumpApexAttemptsPerSimulation;
	EMovementMode DefaultLandMovementMode;
	float FallingLateralFriction;
	float JumpZVelocity;
	bool bAllowPhysicsRotationDuringAnimRootMotion;
	FRotator RotationRate;
	bool bDeferUpdateMoveComponent;
	bool bRequestedMoveUseAcceleration;
	float PerchAdditionalHeight;
	bool bNavAgentPropsCanJump; // from NavAgentProps.bCanJump
	bool bMovementStateCanJump; // from MovementState.bCanJump
	float MaxWalkSpeedCrouched;
	float MaxWalkSpeed;
	float MaxSwimSpeed;
	float MaxFlySpeed;
	float MaxCustomMovementSpeed;


	FCachedMovementBaseAsyncData MovementBaseAsyncData;
	TUniquePtr<FUpdatedComponentAsyncInput> UpdatedComponentInput;
	TUniquePtr<FCharacterAsyncInput> CharacterInput;
	

	UWorld* World; // Remove once we have physics thread scene query API
	//AActor* Owner; // TODO Threadsafe make sure this isn't accessed.

	// primitive component InitSweepCollisionParams
	FComponentQueryParams QueryParams;
	FCollisionResponseParams CollisionResponseParams;
	ECollisionChannel CollisionChannel;
	FCollisionQueryParams CapsuleParams;
	FRandomStream RandomStream;

	// When filling inputs we put initial value of outputs in here.
	// This initializes callback output at beginning of sim,
	// and callback output is copied back onto this at end of sim,
	// so it may be used to initialize subsequent steps using this same input
	TUniquePtr<FCharacterMovementAsyncOutput> InitialOutput;

	// Entry point of async tick
	void Simulate(const float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	void Reset() { bInitialized = false; /* TODO, currently writing everything each frame. Should actually implement this.*/ }
	virtual ~FCharacterMovementAsyncInput() {}

	// TODO organize these
	virtual void ControlledCharacterMove(const float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual void PerformMovement(float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual void MaybeUpdateBasedMovement(float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual void UpdateBasedMovement(float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual void StartNewPhysics(float deltaTime, int32 Iterations, FCharacterMovementAsyncOutput& Output) const;
	virtual void PhysWalking(float deltaTime, int32 Iterations, FCharacterMovementAsyncOutput& Output) const;
	virtual void PhysFalling(float deltaTime, int32 Iterations, FCharacterMovementAsyncOutput& Output) const;
	virtual void PhysicsRotation(float DeltaTime, FCharacterMovementAsyncOutput& Output) const;
	virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult, FCharacterMovementAsyncOutput& Output) const;
	virtual FVector ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace, FCharacterMovementAsyncOutput& Output) const;
	virtual bool CanCrouchInCurrentState(FCharacterMovementAsyncOutput& Output) const;
	virtual FVector ConstrainInputAcceleration(FVector InputAcceleration, const FCharacterMovementAsyncOutput& Output) const;
	virtual FVector ScaleInputAcceleration(FVector InputAcceleration) const;
	virtual float ComputeAnalogInputModifier(FVector Acceleration) const;
	virtual FVector ConstrainLocationToPlane(FVector Location) const;
	virtual FVector ConstrainDirectionToPlane(FVector Direction) const;
	virtual FVector ConstrainNormalToPlane(FVector Normal) const;
	virtual void MaintainHorizontalGroundVelocity(FCharacterMovementAsyncOutput& Output) const;
	virtual bool MoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FCharacterMovementAsyncOutput& Output, FHitResult* OutHitResult = nullptr, ETeleportType TeleportType = ETeleportType::None) const;
	virtual bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, FCharacterMovementAsyncOutput& Output, ETeleportType Teleport = ETeleportType::None) const;
	virtual void ApplyAccumulatedForces(float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual void ClearAccumulatedForces(FCharacterMovementAsyncOutput& Output) const;
	virtual void SetMovementMode(EMovementMode NewMovementMode, FCharacterMovementAsyncOutput& Output, uint8 NewCustomMode = 0) const;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode, FCharacterMovementAsyncOutput& Output) const;
	virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, FCharacterMovementAsyncOutput& Output, const FHitResult* DownwardSweepResult = nullptr) const;
	virtual void ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, FCharacterMovementAsyncOutput& Output, const FHitResult* DownwardSweepResult = nullptr) const;
	virtual bool FloorSweepTest(struct FHitResult& OutHit, const FVector& Start, const FVector& End, ECollisionChannel TraceChannel, const struct FCollisionShape& CollisionShape, const struct FCollisionQueryParams& Params, const struct FCollisionResponseParams& ResponseParam, FCharacterMovementAsyncOutput& Output) const;
	virtual bool IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const;
	virtual bool IsWalkable(const FHitResult& Hit) const;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds, FCharacterMovementAsyncOutput& Output) const;
	virtual float GetSimulationTimeStep(float RemainingTime, int32 Iterations) const;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration, FCharacterMovementAsyncOutput& Output) const;
	virtual bool ApplyRequestedMove(float DeltaTime, float MaxAccel, float MaxSpeed, float Friction, float BrakingDeceleration, FVector& OutAcceleration, float& OutRequestedSpeed, FCharacterMovementAsyncOutput& Output) const;
	virtual bool ShouldComputeAccelerationToReachRequestedVelocity(const float RequestedSpeed, FCharacterMovementAsyncOutput& Output) const;
	virtual float GetMinAnalogSpeed(FCharacterMovementAsyncOutput& Output) const;
	virtual float GetMaxBrakingDeceleration(FCharacterMovementAsyncOutput& Output) const;
	virtual void ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration, FCharacterMovementAsyncOutput& Output) const;
	virtual FVector GetPenetrationAdjustment(FHitResult& HitResult) const;
	virtual bool ResolvePenetration(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotation, FCharacterMovementAsyncOutput& Output) const;
	virtual void HandleImpact(const FHitResult& Impact, FCharacterMovementAsyncOutput& Output, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector) const;
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact, FCharacterMovementAsyncOutput& Output) const;
	virtual FVector ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual FVector HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual void OnCharacterStuckInGeometry(const FHitResult* Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual bool CanStepUp(const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult& Hit, FCharacterMovementAsyncOutput& Output, FStepDownResult* OutStepDownResult = nullptr) const;
	virtual bool CanWalkOffLedges(FCharacterMovementAsyncOutput& Output) const;
	virtual FVector GetLedgeMove(const FVector& OldLocation, const FVector& Delta, const FVector& GravDir, FCharacterMovementAsyncOutput& Output) const;
	virtual bool CheckLedgeDirection(const FVector& OldLocation, const FVector& SideStep, const FVector& GravDir, FCharacterMovementAsyncOutput& Output) const;
	FVector GetPawnCapsuleExtent(const EShrinkCapsuleExtent ShrinkMode, const float CustomShrinkAmount, FCharacterMovementAsyncOutput& Output) const;
	FCollisionShape GetPawnCapsuleCollisionShape(const EShrinkCapsuleExtent ShrinkMode, FCharacterMovementAsyncOutput& Output, const float CustomShrinkAmount = 0.0f) const;
	void TwoWallAdjust(FVector& OutDelta, const FHitResult& Hit, const FVector& OldHitNormal, FCharacterMovementAsyncOutput& Output) const;
	void RevertMove(const FVector& OldLocation, UPrimitiveComponent* OldBase, const FVector& PreviousBaseLocation, const FFindFloorResult& OldFloor, bool bFailMove, FCharacterMovementAsyncOutput& Output) const;
	ETeleportType GetTeleportType(FCharacterMovementAsyncOutput& Output) const;
	virtual void HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta) const;
	virtual bool ShouldCatchAir(const FFindFloorResult& OldFloor, const FFindFloorResult& NewFloor) const;
	virtual void StartFalling(int32 Iterations, float remainingTime, float timeTick, const FVector& Delta, const FVector& subLoc, FCharacterMovementAsyncOutput& Output) const;
	virtual void AdjustFloorHeight(FCharacterMovementAsyncOutput& Output) const;
	void SetBaseFromFloor(const FFindFloorResult& FloorResult, FCharacterMovementAsyncOutput& Output) const;
	virtual bool ShouldComputePerchResult(const FHitResult& InHit, FCharacterMovementAsyncOutput& Output, bool bCheckRadius = true) const;
	virtual bool ComputePerchResult(const float TestRadius, const FHitResult& InHit, const float InMaxFloorDist, FFindFloorResult& OutPerchFloorResult, FCharacterMovementAsyncOutput& Output) const;
	float GetPerchRadiusThreshold() const;
	virtual float GetValidPerchRadius(const FCharacterMovementAsyncOutput& Output) const;
	virtual bool CheckFall(const FFindFloorResult& OldFloor, const FHitResult& Hit, const FVector& Delta, const FVector& OldLocation, float remainingTime, float timeTick, int32 Iterations, bool bMustJump, FCharacterMovementAsyncOutput& Output) const;
	virtual FVector GetFallingLateralAcceleration(float DeltaTime, FCharacterMovementAsyncOutput& Output) const;
	virtual FVector GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration, FCharacterMovementAsyncOutput& Output) const;
	virtual float BoostAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration, FCharacterMovementAsyncOutput& Output) const;
	virtual bool ShouldLimitAirControl(float DeltaTime, const FVector& FallAcceleration) const;
	virtual FVector LimitAirControl(float DeltaTime, const FVector& FallAcceleration, const FHitResult& HitResult, bool bCheckForValidLandingSpot, FCharacterMovementAsyncOutput& Output) const;
	void RestorePreAdditiveRootMotionVelocity(FCharacterMovementAsyncOutput& Output) const;
	virtual FVector NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime, FCharacterMovementAsyncOutput& Output) const;
	virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations, FCharacterMovementAsyncOutput& Output) const;
	virtual void SetPostLandedPhysics(const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual void SetDefaultMovementMode(FCharacterMovementAsyncOutput& Output) const;
	virtual bool ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
	virtual FRotator GetDeltaRotation(float DeltaTime) const;
	virtual float GetAxisDeltaRotation(float InAxisRotationRate, float DeltaTime) const;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation, FCharacterMovementAsyncOutput& Output) const;
	virtual bool ShouldRemainVertical(FCharacterMovementAsyncOutput& Output) const;
	virtual bool CanAttemptJump(FCharacterMovementAsyncOutput& Output) const;
	virtual bool DoJump(bool bReplayingMoves, FCharacterMovementAsyncOutput& Output) const;

	// UNavMovementComponent (super class) impl
	bool IsJumpAllowed() const;

	virtual float GetMaxSpeed(FCharacterMovementAsyncOutput& Output) const;
	virtual bool IsCrouching(const FCharacterMovementAsyncOutput& Output) const;
	virtual bool IsFalling(const FCharacterMovementAsyncOutput& Output) const;
	virtual bool IsMovingOnGround(const FCharacterMovementAsyncOutput& Output) const;
	virtual bool IsExceedingMaxSpeed(float MaxSpeed, const FCharacterMovementAsyncOutput& Output) const;

	// More from UMovementComponent, this is not impl ported from CharacterMovementComponent, but super class, so we can call Super:: in CMC impl.
	// TODO rename or re-organize this.
	FVector MoveComponent_GetPenetrationAdjustment(FHitResult& HitResult) const;
	float MoveComponent_SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, FCharacterMovementAsyncOutput& Output, bool bHandleImpact = false) const;
	FVector MoveComponent_ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit, FCharacterMovementAsyncOutput& Output) const;
};

class FCharacterMovementAsyncCallback : public Chaos::TSimCallbackObject<FCharacterMovementAsyncInput, FCharacterMovementAsyncOutput>
{
private:
	virtual void OnPreSimulate_Internal() override;
};