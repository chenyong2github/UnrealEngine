// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/ImmediatePhysics/ImmediatePhysicsDeclares.h"
#include "AnimNode_RigidBody.generated.h"

struct FBodyInstance;
struct FConstraintInstance;

extern ANIMGRAPHRUNTIME_API TAutoConsoleVariable<int32> CVarEnableRigidBodyNode;
extern ANIMGRAPHRUNTIME_API TAutoConsoleVariable<int32> CVarEnableRigidBodyNodeSimulation;
extern ANIMGRAPHRUNTIME_API TAutoConsoleVariable<int32> CVarRigidBodyLODThreshold;

/** Determines in what space the simulation should run */
UENUM()
enum class ESimulationSpace : uint8
{
	/** Simulate in component space. Moving the entire skeletal mesh will have no affect on velocities */
	ComponentSpace,
	/** Simulate in world space. Moving the skeletal mesh will generate velocity changes */
	WorldSpace,
	/** Simulate in another bone space. Moving the entire skeletal mesh and individually modifying the base bone will have no affect on velocities */
	BaseBoneSpace,
};


USTRUCT(BlueprintType)
struct ANIMGRAPHRUNTIME_API FSimSpaceSettings
{
	GENERATED_USTRUCT_BODY()

	FSimSpaceSettings();

	// Global multipler on the effects of simulation space movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterAlpha;

	// Multiplier on the Z-component of velocity and acceleration that is passed to the solver
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float VelocityScaleZ;

	// A clamp on the effective simulation-space velocity that is passed to the solver
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float MaxLinearVelocity;

	// A clamp on the effective simulation-space angular velocity that is passed to the solver
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float MaxAngularVelocity;
	
	// A clamp on the effective simulation-space acceleration that is passed to the solver
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float MaxLinearAcceleration;
	
	// A clamp on the effective simulation-space angular accleration that is passed to the solver
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float MaxAngularAcceleration;

	// Can be used to simulate freefall
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float Freefall;

	// Additional linear drag applied to every body (in addition to linear drag in the physics asset).
	// Useful when combined with ExternalLinearVelocity to add a temporart wind-blown effect without
	// having to set up the physics asset with linear drag.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings, meta = (ClampMin = "0.0"))
	float ExternalLinearDrag;

	// Additional velocity to pass into the solver - for wind etc
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FVector ExternalLinearVelocity;

	// Additional angular velocity to pass into the solver
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	FVector ExternalAngularVelocity;
};

/**
 *	Controller that simulates physics based on the physics asset of the skeletal mesh component
 */
USTRUCT()
struct ANIMGRAPHRUNTIME_API FAnimNode_RigidBody : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	FAnimNode_RigidBody();
	~FAnimNode_RigidBody();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual bool NeedsOnInitializeAnimInstance() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual bool NeedsDynamicReset() const override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual int32 GetLODThreshold() const override;
	// End of FAnimNode_SkeletalControlBase interface

	virtual void AddImpulseAtLocation(FVector Impulse, FVector Location, FName BoneName = NAME_None);

	// TEMP: Exposed for use in PhAt as a quick way to get drag handles working with Chaos
	virtual ImmediatePhysics::FSimulation* GetSimulation() { return PhysicsSimulation; }

public:
	/** Physics asset to use. If empty use the skeletal mesh's default physics asset */
	UPROPERTY(EditAnywhere, Category = Settings)
	UPhysicsAsset* OverridePhysicsAsset;

private:
	FTransform PreviousCompWorldSpaceTM;
	FTransform CurrentTransform;
	FTransform PreviousTransform;

	UPhysicsAsset* UsePhysicsAsset;
public:
	/** Override gravity*/
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, editcondition = "bOverrideWorldGravity"))
	FVector OverrideWorldGravity;

	/** Applies a uniform external force in world space. This allows for easily faking inertia of movement while still simulating in component space for example */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinShownByDefault))
	FVector ExternalForce;

	/** When using non-world-space sim, this controls how much of the components world-space acceleration is passed on to the local-space simulation. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	FVector ComponentLinearAccScale;

	/** When using non-world-space sim, this applies a 'drag' to the bodies in the local space simulation, based on the components world-space velocity. */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	FVector ComponentLinearVelScale;

	/** When using non-world-space sim, this is an overall clamp on acceleration derived from ComponentLinearAccScale and ComponentLinearVelScale, to ensure it is not too large. */
	UPROPERTY(EditAnywhere, Category = Settings)
	FVector	ComponentAppliedLinearAccClamp;

	/** 
	 * Settings for the system which passes motion of the simulations space
	 * into the simulation. This allows the simulation to pass some fraction
	 * of the world space motion onto the bodies.
	 * This system is a superset of the functionality provided by ComponentLinearAccScale,
	 * ComponentLinearVelScale, and ComponentAppliedLinearAccClamp. In general
	 * you would not have both systems enabled.
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault))
	FSimSpaceSettings SimSpaceSettings;


	/**
	 * Scale of cached bounds (vs. actual bounds).
	 * Increasing this may improve performance, but overlaps may not work as well.
	 * (A value of 1.0 effectively disables cached bounds).
	 */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (ClampMin="1.0", ClampMax="2.0"))
	float CachedBoundsScale;

	/** Matters if SimulationSpace is BaseBone */
	UPROPERTY(EditAnywhere, Category = Settings)
	FBoneReference BaseBoneRef;

	/** The channel we use to find static geometry to collide with */
	UPROPERTY(EditAnywhere, Category = Settings, meta = (editcondition = "bEnableWorldGeometry"))
	TEnumAsByte<ECollisionChannel> OverlapChannel;

	/** What space to simulate the bodies in. This affects how velocities are generated */
	UPROPERTY(EditAnywhere, Category = Settings)
	ESimulationSpace SimulationSpace;

	/** Whether to allow collisions between two bodies joined by a constraint  */
	UPROPERTY(EditAnywhere, Category = Settings)
	bool bForceDisableCollisionBetweenConstraintBodies;


private:
	ETeleportType ResetSimulatedTeleportType;

public:
	UPROPERTY(EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	uint8 bEnableWorldGeometry : 1;

	UPROPERTY(EditAnywhere, Category = Settings, meta = (InlineEditConditionToggle))
	uint8 bOverrideWorldGravity : 1;

	/** 
		When simulation starts, transfer previous bone velocities (from animation)
		to make transition into simulation seamless.
	*/
	UPROPERTY(EditAnywhere, Category = Settings, meta=(PinHiddenByDefault))
	uint8 bTransferBoneVelocities : 1;

	/**
		When simulation starts, freeze incoming pose.
		This is useful for ragdolls, when we want the simulation to take over.
		It prevents non simulated bones from animating.
	*/
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bFreezeIncomingPoseOnStart : 1;

	/**
		Correct for linear tearing on bodies with all axes Locked.
		This only works if all axes linear translation are locked
	*/
	UPROPERTY(EditAnywhere, Category = Settings)
	uint8 bClampLinearTranslationLimitToRefPose : 1;

	/**
		For world-space simulations, if the magnitude of the component's 3D scale is less than WorldSpaceMinimumScale, do not update the node.
	*/
	UPROPERTY(EditAnywhere, Category = Settings)
	float WorldSpaceMinimumScale;

private:
	uint8 bEnabled : 1;
	uint8 bSimulationStarted : 1;
	uint8 bCheckForBodyTransformInit : 1;

public:
	void PostSerialize(const FArchive& Ar);

private:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bComponentSpaceSimulation_DEPRECATED;	//use SimulationSpace
#endif

	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	void InitPhysics(const UAnimInstance* InAnimInstance);
	void UpdateWorldGeometry(const UWorld& World, const USkeletalMeshComponent& SKC);
	void UpdateWorldForces(const FTransform& ComponentToWorld, const FTransform& RootBoneTM);

	void InitializeNewBodyTransformsDuringSimulation(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, const FTransform& BaseBoneTM);

	void InitSimulationSpace(
		const FTransform& ComponentToWorld,
		const FTransform& BoneToComponent);

	// Calculate simulation space transform, velocity etc to pass into the solver
	void CalculateSimulationSpace(
		ESimulationSpace Space,
		const FTransform& ComponentToWorld,
		const FTransform& BoneToComponent,
		const float Dt,
		const FSimSpaceSettings& Settings,
		FTransform& SpaceTransform,
		FVector& SpaceLinearVel,
		FVector& SpaceAngularVel,
		FVector& SpaceLinearAcc,
		FVector& SpaceAngularAcc);

	// Gather nearby world objects and add them to the sim
	void CollectWorldObjects();

	// Flag invalid world objects to be removed from the sim
	void ExpireWorldObjects();

	// Remove simulation objects that are flagged as expired
	void PurgeExpiredWorldObjects();

	// Update sim-space transforms of world objects
	void UpdateWorldObjects(const FTransform& SpaceTransform);

private:

	float AccumulatedDeltaTime;
	float AnimPhysicsMinDeltaTime;
	bool bSimulateAnimPhysicsAfterReset;
	/** This should only be used for removing the delegate during termination. Do NOT use this for any per frame work */
	TWeakObjectPtr<USkeletalMeshComponent> SkelMeshCompWeakPtr;

	ImmediatePhysics::FSimulation* PhysicsSimulation;
	FSolverIterations SolverIterations;

	struct FOutputBoneData
	{
		FOutputBoneData()
			: CompactPoseBoneIndex(INDEX_NONE)
		{}

		TArray<FCompactPoseBoneIndex> BoneIndicesToParentBody;
		FCompactPoseBoneIndex CompactPoseBoneIndex;
		int32 BodyIndex;
		int32 ParentBodyIndex;
	};

	struct FBodyAnimData
	{
		FBodyAnimData()
			: TransferedBoneAngularVelocity(ForceInit)
			, TransferedBoneLinearVelocity(ForceInitToZero)
			, LinearXMotion(ELinearConstraintMotion::LCM_Locked)
			, LinearYMotion(ELinearConstraintMotion::LCM_Locked)
			, LinearZMotion(ELinearConstraintMotion::LCM_Locked)
			, LinearLimit(0.0f)
			, RefPoseLength (0.f)
			, bIsSimulated(false)
			, bBodyTransformInitialized(false)
		{}

		FQuat TransferedBoneAngularVelocity;
		FVector TransferedBoneLinearVelocity;

		ELinearConstraintMotion LinearXMotion;
		ELinearConstraintMotion LinearYMotion;
		ELinearConstraintMotion LinearZMotion;
		float LinearLimit;
		// we don't use linear limit but use default length to limit the bodies
		// linear limits are defined per constraint - it can be any two joints that can limit
		// this is just default length of the local space from parent, and we use that info to limit
		// the translation
		float RefPoseLength;

		bool bIsSimulated : 1;
		bool bBodyTransformInitialized : 1;
	};

	struct FWorldObject
	{
		FWorldObject() : ActorHandle(nullptr), LastSeenTick(0), bExpired(false) {}
		FWorldObject(ImmediatePhysics::FActorHandle* InActorHandle, int32 InLastSeenTick) : ActorHandle(InActorHandle), LastSeenTick(InLastSeenTick), bExpired(false) {}

		ImmediatePhysics::FActorHandle* ActorHandle;
		int32 LastSeenTick;
		bool bExpired;
	};

	TArray<FOutputBoneData> OutputBoneData;
	TArray<ImmediatePhysics::FActorHandle*> Bodies;
	TArray<int32> SkeletonBoneIndexToBodyIndex;
	TArray<FBodyAnimData> BodyAnimData;

	TArray<FPhysicsConstraintHandle*> Constraints;
	TArray<USkeletalMeshComponent::FPendingRadialForces> PendingRadialForces;

	TMap<const UPrimitiveComponent*, FWorldObject> ComponentsInSim;
	int32 ComponentsInSimTick;

	FVector WorldSpaceGravity;

	float TotalMass;

	// Bounds used to gather world objects copied into the simulation
	FSphere CachedBounds;

	FCollisionQueryParams QueryParams;

	FPhysScene* PhysScene;

	// Evaluation counter, to detect when we haven't be evaluated in a while.
	FGraphTraversalCounter EvalCounter;

	// Used by CollectWorldObjects and UpdateWorldGeometry in Task Thread
	// Typically, World should never be accessed off the Game Thread.
	// However, since we're just doing overlaps this should be OK.
	const UWorld* UnsafeWorld;

	// Used by CollectWorldObjects and UpdateWorldGeometry in Task Thread
	// Only used for a pointer comparison.
	const AActor* UnsafeOwner;

	FBoneContainer CapturedBoneVelocityBoneContainer;
	FCSPose<FCompactHeapPose> CapturedBoneVelocityPose;
	FCSPose<FCompactHeapPose> CapturedFrozenPose;
	FBlendedHeapCurve CapturedFrozenCurves;

	// Used by the world-space to simulation-space motion transfer system in Component- or Bone-Space sims
	FTransform PreviousComponentToWorld;
	FTransform PreviousBoneToComponent;
	FVector PreviousComponentLinearVelocity;
	FVector PreviousComponentAngularVelocity;
	FVector PreviousBoneLinearVelocity;
	FVector PreviousBoneAngularVelocity;
};

#if WITH_EDITORONLY_DATA
template<>
struct TStructOpsTypeTraits<FAnimNode_RigidBody> : public TStructOpsTypeTraitsBase2<FAnimNode_RigidBody>
{
	enum
	{
		WithPostSerialize = true
	};
};
#endif
