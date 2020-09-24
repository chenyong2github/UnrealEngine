// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ControlRig.h"
#include "Units/Hierarchy/RigUnit_SetTransform.h"
#include "Units/Hierarchy/RigUnit_SetCurveValue.h"
#include "FKControlRig.generated.h"

class USkeletalMesh;
struct FReferenceSkeleton;
struct FSmartNameMapping;
/** Structs used to specify which bones/curves/controls we should have active, since if all controls or active we can't passthrough some previous bone transform*/
struct FFKBoneCheckInfo
{
	FName BoneName;
	int32 BoneID;
	bool  bActive;
};

UENUM()
enum class EControlRigFKRigExecuteMode: uint8
{
	/** Replaces the current pose */
	Replace,

	/** Applies the authored pose as an additive layer */
	Additive,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

/** Rig that allows override editing per joint */
UCLASS(NotBlueprintable, Meta = (DisplayName = "FK Control Rig"))
class CONTROLRIG_API UFKControlRig : public UControlRig
{
	GENERATED_UCLASS_BODY()

public: 

	// BEGIN ControlRig
	virtual void Initialize(bool bInitRigUnits = true) override;
	virtual void ExecuteUnits(FRigUnitContext& InOutContext, const FName& InEventName) override;
	// END ControlRig

	// utility function to 
	static FName GetControlName(const FName& InBoneName);
	static FName GetSpaceName(const FName& InBoneName);


	TArray<FName> GetControlNames();
	bool GetControlActive(int32 Index) const;
	void SetControlActive(int32 Index, bool bActive);
	void SetControlActive(const TArray<FFKBoneCheckInfo>& InBoneChecks);

	void ToggleApplyMode();
	bool CanToggleApplyMode() const { return true; }
	bool IsApplyModeAdditive() const { return ApplyMode == EControlRigFKRigExecuteMode::Additive; }

private:

	/** Create RigElements - bone hierarchy and curves - from incoming skeleton */
	void CreateRigElements(const USkeletalMesh* InReferenceMesh);
	void CreateRigElements(const FReferenceSkeleton& InReferenceSkeleton, const FSmartNameMapping* InSmartNameMapping);

	UPROPERTY()
	TArray<bool> IsControlActive;

	UPROPERTY()
	EControlRigFKRigExecuteMode ApplyMode;

	friend class FControlRigInteractionTest;
};