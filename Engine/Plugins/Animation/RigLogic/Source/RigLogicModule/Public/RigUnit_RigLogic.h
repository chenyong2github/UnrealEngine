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

/* A helper struct used inside of the RigUnit_RigLogic to store arrays of arrays of integers. */
USTRUCT(meta = (DocumentationPolicy = "Strict"))
struct FRigUnit_RigLogic_IntArray
{
	GENERATED_BODY()

	// The values stored within this array.
	UPROPERTY(transient)
	TArray<int32> Values;
};

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
 	FRigUnit_RigLogic_Data& operator = (const FRigUnit_RigLogic_Data& Other);

	/** Cached Skeletal Mesh Component **/
	UPROPERTY(transient)
	TWeakObjectPtr<USkeletalMeshComponent> SkelMeshComponent;

	/** LOD for which the model is rendered **/
	UPROPERTY(transient)
	uint32 CurrentLOD;

	// Using TSharedPtr so the struct itself remains copy-able, but still cleans
	// up these members properly when needed
	FRigLogic* RigLogic;
	FRigInstance* RigInstance;

	/** Mapping RL indices to UE indices
	  * Note: we use int32 instead of uint32 to allow storing INDEX_NONE for missing elements
	  * if value is valid, it is cast to appropriate uint type
    **/

    /** RL input index to ControlRig's input curve index for each LOD **/
	UPROPERTY(transient)
	TArray<int32> InputCurveIndices;  

	/** RL joint index to ControlRig's hierarchy bone index **/
	UPROPERTY(transient)
	TArray<int32> HierarchyBoneIndices;

	/** RL mesh blend shape index to ControlRig's output blendshape curve index for each LOD **/
	UPROPERTY(transient)
	TArray<FRigUnit_RigLogic_IntArray> MorphTargetCurveIndices;  

	/** RL mesh+blend shape array index to RL blend shape index for each LOD **/
	UPROPERTY(transient)
	TArray<FRigUnit_RigLogic_IntArray> BlendShapeIndices;

	/** RL animated map index to ControlRig's output anim map curve index for each LOD **/
	UPROPERTY(transient)
	TArray<FRigUnit_RigLogic_IntArray> CurveContainerIndicesForAnimMaps;

	/** RL animated map index to RL anim map curve index for each LOD **/
	UPROPERTY(transient)
	TArray<FRigUnit_RigLogic_IntArray> RigLogicIndicesForAnimMaps;

	/** Stores joints already updated, to avoid iterating through them multiple times **/
	UPROPERTY(transient)
	TArray<bool> UpdatedJoints;

	/** Size of the blendshape map, cached from BehaviorReader **/
	UPROPERTY(transient)
	uint32 BlendShapeMappingCount = 0;

	/** The index of RL input control for turning on the corrective expression for neck on average female body */
	UPROPERTY(transient)
	int32 NeckFemaleAverageCorCurveIndex = -1;

	/** The index of RL input control for turning on the corrective expression for neck on mascular male body */
	UPROPERTY(transient)
	int32 NeckMaleMuscularCorExpCurveIndex = -1;

	static const uint8 MAX_ATTRS_PER_JOINT;

	void InitializeRigLogic(IBehaviorReader* DNABehavior);
	bool IsRigLogicInitialized();
	void ChangeRigLogicLODIfNeeded();

	/** Constructs curve name from nameToSplit using formatString of form x<obj>y<attr>z **/
	FString ConstructCurveName(const FString& nameToSplit, const FString& formatString);

	/** Makes a map of input curve indices from DNA file to the control rig curves **/
	void MapInputCurveIndices(const IBehaviorReader* DNABehavior, const FRigCurveContainer* CurveContainer);
	/** Uses names to map joint indices from DNA file to the indices of bones in control rig hierarchy **/
	void MapJoints(const IBehaviorReader* DNABehavior, const FRigBoneHierarchy* Hierarchy);
	/** Uses names of blend shapes and meshes in DNA file, for all LODs, to map their indices to the indices of
	  * morph target curves in the curve container; curve name format is <mesh>__<blendshape> **/
	void MapMorphTargets(const IBehaviorReader* DNABehavior, const FRigCurveContainer* CurveContainer );
	/** Uses names to map mask multiplier indices from DNA file, for all LODs, to the indices of curves in the
	  * control rig's curve container **/
	void MapMaskMultipliers(const IBehaviorReader* DNABehavior, const FRigCurveContainer* CurveContainer );

	/** Calculates joint positions, orientation and scale based on inputs curves of the control rig **/
	void CalculateRigLogic(FControlRigExecuteContext& ExecuteContext);
	/** Updates joint positions in the hierarchy based on inputs curves of the control rig **/
	void UpdateJoints(FRigHierarchyContainer* Hierarchy, const FRigUnit_RigLogic_JointUpdateParams& JointUpdateParams);
	/** Updates morph target curve values based on values of input curves of the control rig **/
	void UpdateBlendShapeCurves(FRigCurveContainer* CurveContainer, TArrayView<const float>& BlendShapeValues);
	/** Updates anim map curve values based on values of input curves of the control rig **/
	void UpdateAnimMapCurves(FRigCurveContainer* CurveContainer, TArrayView<const float>& AnimMapOutputs);

	IBehaviorReader* FetchBehaviorReaderFromOwner();
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

	FRigUnit_RigLogic()
	{
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

private:

	// internal work data for the unit
	UPROPERTY(transient)
	FRigUnit_RigLogic_Data Data;
};
