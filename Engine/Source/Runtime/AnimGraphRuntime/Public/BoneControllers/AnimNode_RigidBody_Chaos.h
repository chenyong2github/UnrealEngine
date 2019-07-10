// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneControllers/AnimNode_RigidBody.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "GeometryCollection/PhysicsAssetSimulation.h"
#include "PhysicalMaterials/Experimental/ChaosPhysicalMaterial.h"

#include "AnimNode_RigidBody_Chaos.generated.h"


#if INCLUDE_CHAOS
namespace Chaos
{
	class FPBDRigidsSolver;
}
class FBoneHierarchy;
class FSkeletalMeshPhysicsObject;
struct FSkeletalMeshPhysicsObjectInputs;
struct FSkeletalMeshPhysicsObjectOutputs;
#endif

extern ANIMGRAPHRUNTIME_API TAutoConsoleVariable<int32> CVarEnableChaosRigidBodyNode;

/**
 *	Controller that simulates physics based on the physics asset of the skeletal mesh component
 */
USTRUCT()
struct ANIMGRAPHRUNTIME_API FAnimNode_RigidBody_Chaos : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	FAnimNode_RigidBody_Chaos();
	~FAnimNode_RigidBody_Chaos();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void UpdateComponentPose_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override { return true; }
	virtual bool NeedsDynamicReset() const override;
	virtual void ResetDynamics(ETeleportType InTeleportType) override;
	virtual int32 GetLODThreshold() const override;
	// End of FAnimNode_SkeletalControlBase interface

public:
	//
	// ChaosPhysics
	//

	/** Physics asset to use. If empty use the skeletal mesh's default physics asset */
	UPROPERTY(EditAnywhere, Category = Settings)
		UPhysicsAsset* OverridePhysicsAsset;

	/** Physical Properties */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics")
		const UChaosPhysicalMaterial* PhysicalMaterial;

	//
	// ChaosPhysics | General
	//

	/** When Simulating is enabled the Component will initialize its rigid bodies within the solver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		bool bSimulating;

	/** Number of solver interations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		int32 NumIterations;

	/** If true, this component will get collision notification events (@see IChaosNotifyHandlerInterface) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		bool bNotifyCollisions;

	/** ObjectType defines how to initialize the rigid collision structures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		EObjectStateTypeEnum ObjectType;

	/** Density / mass.
	 *
	 * Common densities in g/cm^3:
	 *     gold: 19.3
	 *     lead: 11.3
	 *     copper: 8.3 - 9.0
	 *     steel: 8.03
	 *     iron: 7.8
	 *     aluminum: 2.7
	 *     glass: 2.4 - 2.8
	 *     brick: 1.4 - 2.4
	 *     concrete: 0.45 - 2.4
	 *     bone: 1.7 - 2.0
	 *     muscle: 1.06
	 *     water: 1.0
	 *     fat: 0.9196
	 *     gasoline: 0.7
	 *     wood: 0.67
	 *     tree bark: 0.24
	 *     air: 0.001293
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		float Density;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		float MinMass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|General")
		float MaxMass;

	//
	// ChaosPhysics | Collisions
	//

	/** CollisionType defines how to initialize the rigid collision structures.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
		ECollisionTypeEnum CollisionType;

	/** Number of particles to generate per unit area (square cm). 0.1 would generate 1 collision particle per 10 cm^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
		float ImplicitShapeParticlesPerUnitArea;
	/** Minimum number of particles for each implicit shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
		int ImplicitShapeMinNumParticles;
	/** Maximum number of particles for each implicit shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
		int ImplicitShapeMaxNumParticles;

	/** Resolution on the smallest axes for the level set. (def: 5) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|Collisions")
		int32 MinLevelSetResolution;
	/** Resolution on the smallest axes for the level set. (def: 10) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ChaosPhysics|Collisions")
		int32 MaxLevelSetResolution;

	/** Collision group - 0 = collides with all, -1 = none */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Collisions")
		int32 CollisionGroup;

	//
	// ChaosPhysics | Initial Velocity
	//

	/** Where to pull initial velocity from - user defined or animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
		EInitialVelocityTypeEnum InitialVelocityType;
	/** Initial linear velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
		FVector InitialLinearVelocity;
	/** Initial angular velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaosPhysics|Initial Velocity")
		FVector InitialAngularVelocity;

private:
	FTransform PreviousCompWorldSpaceTM;
	FTransform CurrentTransform;
	FTransform PreviousTransform;

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
	UPROPERTY(EditAnywhere, Category = Settings, meta=(InlineEditConditionToggle))
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

private:
	uint8 bSimulationStarted : 1;
	uint8 bCheckForBodyTransformInit : 1;

public:
	void PostSerialize(const FArchive& Ar);

private:

	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	void InitPhysics(const UAnimInstance* InAnimInstance);
	void UpdateWorldGeometry(const UWorld& World, const USkeletalMeshComponent& SKC);
	void UpdateWorldForces(const FTransform& ComponentToWorld, const FTransform& RootBoneTM);

	void InitializeNewBodyTransformsDuringSimulation(FComponentSpacePoseContext& Output, const FTransform& ComponentTransform, const FTransform& BaseBoneTM);

private:

	float AccumulatedDeltaTime;

	/** This should only be used for removing the delegate during termination. Do NOT use this for any per frame work */
	TWeakObjectPtr<USkeletalMeshComponent> SkelMeshCompWeakPtr;

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

	TArray<FOutputBoneData> OutputBoneData;
	//TArray<int32> SkeletonBoneIndexToBodyIndex;
	TArray<FBodyAnimData> BodyAnimData;

#if INCLUDE_CHAOS
	/** Called by the Physics Object to get its set up parameters. */
	void PhysicsObjectInitCallback(const USkeletalMeshComponent* InSkelMeshComponent, const UAnimInstance* InAnimInstance, FSkeletalMeshPhysicsObjectParams& OutPhysicsParams);

	/** Called by the Physics Object to get the lastest pose from the animation */
	bool UpdatePhysicsInputs(FComponentSpacePoseContext& PoseContext, const float Dt, FBoneHierarchy& OutBoneHierarchy);

	void UpdateAnimNodeOutputs(const FBoneHierarchy& InBoneHierarchy, const FSkeletalMeshPhysicsObjectOutputs& InPhysicsOutputs, FComponentSpacePoseContext& PoseContext, TArray<FBoneTransform>& OutBoneTransforms);

	Chaos::FPBDRigidsSolver* Solver;
	FSkeletalMeshPhysicsObject* PhysicsObject;
	//TUniquePtr<Chaos::TChaosPhysicsMaterial<float>> PhysicsMaterial;	//@todo(mlentine): Don't have one per static mesh

	//ImmediatePhysics::FSimulation* PhysicsSimulation;
	//TArray<ImmediatePhysics::FActorHandle*> Bodies;
	//TArray<FPhysicsConstraintHandle*> Constraints;
	//TArray<USkeletalMeshComponent::FPendingRadialForces> PendingRadialForces;
	//FPhysScene* PhysScene;
	//FCollisionQueryParams QueryParams;
#endif

	TSet<UPrimitiveComponent*> ComponentsInSim;

	FVector WorldSpaceGravity;

	FSphere Bounds;

	float TotalMass;

	FSphere CachedBounds;


	// Evaluation counter, to detect when we haven't be evaluated in a while.
	FGraphTraversalCounter EvalCounter;

	// Typically, World should never be accessed off the Game Thread.
	// However, since we're just doing overlaps this should be OK.
	const UWorld* UnsafeWorld;

	FBoneContainer CapturedBoneVelocityBoneContainer;
	FCSPose<FCompactHeapPose> CapturedBoneVelocityPose;
	FCSPose<FCompactHeapPose> CapturedFrozenPose;
	FBlendedHeapCurve CapturedFrozenCurves;

	FVector PreviousComponentLinearVelocity;
};

#if WITH_EDITORONLY_DATA
template<>
struct TStructOpsTypeTraits<FAnimNode_RigidBody_Chaos> : public TStructOpsTypeTraitsBase2<FAnimNode_RigidBody_Chaos>
{
	enum
	{
		WithPostSerialize = true
	};
};
#endif
