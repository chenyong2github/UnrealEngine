// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigInstance.h"
#include "RigLogic.h"

#include "ControlRig.h"
#include "DNAAsset.h"
#include "Materials/Material.h"
#include "Units/RigUnit.h"

#include "RigUnit_RigLogic.generated.h"

class IBehaviorReader;
class FTransformArrayView;

DECLARE_LOG_CATEGORY_EXTERN(LogRigLogicUnit, Log, All);

/**
 * A helper struct used inside of the RigUnit_RigLogic holding params for updating joints.
 * Note that these params are array views - they don't own the memory they're pointing to.
 */
struct FRigUnit_RigLogic_JointUpdateParams
{
	FRigUnit_RigLogic_JointUpdateParams(
		TArrayView<const uint16> InVariableAttributes,
		FTransformArrayView InNeutralJointTransforms,
		FTransformArrayView InDeltaTransforms
	)
		: VariableAttributes(InVariableAttributes)
		, NeutralJointTransforms(InNeutralJointTransforms)
		, DeltaTransforms(InDeltaTransforms)
	{ }

public:
	TArrayView<const uint16> VariableAttributes;
	FTransformArrayView NeutralJointTransforms;
	FTransformArrayView DeltaTransforms; //the result of rig logic calculations
};

/* The work data used by the FRigUnit_RigLogic */
USTRUCT(meta = (DocumentationPolicy = "Strict"))
struct FRigUnit_RigLogic_Data
{
	GENERATED_BODY()

	FRigUnit_RigLogic_Data();
 	~FRigUnit_RigLogic_Data();
 	FRigUnit_RigLogic_Data(const FRigUnit_RigLogic_Data& Other);
 	FRigUnit_RigLogic_Data& operator=(const FRigUnit_RigLogic_Data& Other);

	/** Cached Skeletal Mesh Component **/
	UPROPERTY(transient)
	TWeakObjectPtr<USkeletalMeshComponent> SkelMeshComponent;

	/** LOD for which the model is rendered **/
	UPROPERTY(transient)
	uint32 CurrentLOD;

	/** The rig runtime context is shared between multiple rig instances and is not owned by this class **/
	TWeakPtr<FSharedRigRuntimeContext> SharedRigRuntimeContext;

	/** RigInstance is a thin class, containing only character instance specific data
	  * i.e. the output buffers where RigLogic writes the results of computations, specific
	  * to each instance of a rig.
	**/
	TUniquePtr<FRigInstance> RigInstance;

	/** Stores joints already updated, to avoid iterating through them multiple times **/
	UPROPERTY(transient)
	TArray<bool> UpdatedJoints;

	static const uint8 MAX_ATTRS_PER_JOINT;

	bool IsRigLogicInitialized(FSharedRigRuntimeContext* Context);
	void InitializeRigLogic(FSharedRigRuntimeContext* Context, const FRigBoneHierarchy* BoneHierarchy, const FRigCurveContainer* CurveContainer);
	void ChangeRigLogicLODIfNeeded();

	/** Makes a map of input curve indices from DNA file to the control rig curves **/
	void MapInputCurveIndices(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer);
	/** Uses names to map joint indices from DNA file to the indices of bones in control rig hierarchy **/
	void MapJoints(FSharedRigRuntimeContext* Context, const FRigBoneHierarchy* Hierarchy);
	/** Uses names of blend shapes and meshes in DNA file, for all LODs, to map their indices to the indices of
	  * morph target curves in the curve container; curve name format is <mesh>__<blendshape> **/
	void MapMorphTargets(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer);
	/** Uses names to map mask multiplier indices from DNA file, for all LODs, to the indices of curves in the
	  * control rig's curve container **/
	void MapMaskMultipliers(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer);

	/** Calculates joint positions, orientation and scale based on inputs curves of the control rig **/
	void CalculateRigLogic(FSharedRigRuntimeContext* Context, const FRigCurveContainer* CurveContainer);
	/** Updates joint positions in the hierarchy based on inputs curves of the control rig **/
	void UpdateJoints(FSharedRigRuntimeContext* Context, FRigHierarchyContainer* Hierarchy, const FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams);
	/** Updates morph target curve values based on values of input curves of the control rig **/
	void UpdateBlendShapeCurves(FSharedRigRuntimeContext* Context, FRigCurveContainer* CurveContainer, TArrayView<const float>& BlendShapeValues);
	/** Updates anim map curve values based on values of input curves of the control rig **/
	void UpdateAnimMapCurves(FSharedRigRuntimeContext* Context, FRigCurveContainer* CurveContainer, TArrayView<const float>& AnimMapOutputs);
};


/** RigLogic is used to translate control input curves into bone transforms and values for blend shape and
  *  animated map multiplier curves */

USTRUCT(meta = (DisplayName = "RigLogic", Category = "RigLogic", DocumentationPolicy = "Strict", Keywords = "Rig,RigLogic"))
struct FRigUnit_RigLogic: public FRigUnitMutable
{
	GENERATED_BODY()

public:
#if WITH_DEV_AUTOMATION_TESTS
	/** Allows accessing private Data property from unit tests **/
	struct TestAccessor;
	friend TestAccessor;
#endif

	RIGVM_METHOD()
	void Execute(const FRigUnitContext& Context) override;

private:

	// internal work data for the unit
	UPROPERTY(transient)
	FRigUnit_RigLogic_Data Data;
};
