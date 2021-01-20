// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
/**
*  Data To Store and Apply the Control Rig Pose
*/

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TransformNoScale.h"
#include "ControlRig.h"
#include "Engine/SkeletalMesh.h"
#include "ControlRigToolAsset.h"
#include "Tools/ControlRigPoseMirrorTable.h"
#include "Rigs/RigControlHierarchy.h"
#include "ControlRigPose.generated.h"

/**
* The Data Stored For Each Control in A Pose.
*/
USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlCopy
{
	GENERATED_BODY()

		FRigControlCopy()
		: Name(NAME_None)
		, ControlType(ERigControlType::Transform)
		, Value()
		, ParentName(NAME_None)
		, SpaceName(NAME_None)
		, OffsetTransform(FTransform::Identity)
		, ParentTransform(FTransform::Identity)
		, LocalTransform(FTransform::Identity)
		, GlobalTransform(FTransform::Identity)

	{
	}

	FRigControlCopy(const FRigControl& InControl, const FRigControlHierarchy& Hierarchy)
	{
		Name = InControl.Name;
		ControlType = InControl.ControlType;
		Value = InControl.Value;
		ParentName = InControl.ParentName;
		SpaceName = InControl.SpaceName;
		OffsetTransform = InControl.OffsetTransform;

		int32 Index = Hierarchy.GetIndex(Name);
		ParentTransform = Index != INDEX_NONE ? Hierarchy.GetParentTransform(Index) : FTransform::Identity;
		LocalTransform = Hierarchy.GetLocalTransform(Name);
		GlobalTransform = Hierarchy.GetGlobalTransform(Name);
	}
	virtual ~FRigControlCopy() {}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Names")
	FName Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Type")
	ERigControlType ControlType;

	UPROPERTY()
	FRigControlValue Value;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Names")
	FName ParentName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Names")
	FName SpaceName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transforms")
	FTransform OffsetTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transforms")
	FTransform ParentTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transforms")
	FTransform LocalTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Transforms")
	FTransform GlobalTransform;

};

/**
* The Data Stored For Each Pose and associated Functions to Store and Paste It
*/
USTRUCT(BlueprintType)
struct CONTROLRIG_API FControlRigControlPose
{
	GENERATED_USTRUCT_BODY()

	FControlRigControlPose() {};
	FControlRigControlPose(UControlRig* InControlRig, bool bUseAll)
	{
		SavePose(InControlRig, bUseAll);
	}
	~FControlRigControlPose() {};

	void SavePose(UControlRig* ControlRig, bool bUseAll);
	void PastePose(UControlRig* ControlRig, bool bDoKey, bool bDoMirror);
	void SetControlMirrorTransform(bool bDoLocalSpace, UControlRig* ControlRig, const FName& Name, bool bIsMatched, const FVector& GlobalTranslation, const FQuat& GlobalRotation,const FVector& LocalTranslation, const FQuat& LocalRotation, bool bNotify, const  FRigControlModifiedContext& Context);
	void PastePoseInternal(UControlRig* ControlRig, bool bDoKey, bool bDoMirror, const TArray<FRigControlCopy>& ControlsToPaste);
	void BlendWithInitialPoses(FControlRigControlPose& InitialPose, UControlRig* ControlRig, bool bDoKey, bool bDoMirror, float BlendValue);

	bool ContainsName(const FName& Name) const;
	void ReplaceControlName(const FName& Name, const FName& NewName);
	TArray<FName> GetControlNames() const;

	void SetUpControlMap();
	TArray<FRigControlCopy> GetPoses() const {return CopyOfControls;};

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Controls")
	TArray<FRigControlCopy> CopyOfControls;

	//Cache of the Map, Used to make pasting faster.
	TMap<FName, int32>  CopyOfControlsNameToIndex;
};


/**
* An individual Pose made of Control Rig Controls
*/
UCLASS(BlueprintType)
class CONTROLRIG_API UControlRigPoseAsset : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	//UOBJECT
	virtual void PostLoad() override;

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SavePose(UControlRig* InControlRig, bool bUseAll);

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void PastePose(UControlRig* InControlRig, bool bDoKey = false, bool bDoMirror = false);
	
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SelectControls(UControlRig* InControlRig, bool bDoMirror = false);

	TArray<FRigControlCopy> GetCurrentPose(UControlRig* InControlRig);

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void GetCurrentPose(UControlRig* InControlRig, FControlRigControlPose& OutPose);

	UFUNCTION(BlueprintPure, Category = "Pose")
	TArray<FName> GetControlNames() const;

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void ReplaceControlName(const FName& CurrentName, const FName& NewName);

	UFUNCTION(BlueprintPure, Category = "Pose")
	bool DoesMirrorMatch(UControlRig* ControlRig, const FName& ControlName) const;

	void BlendWithInitialPoses(FControlRigControlPose& InitialPose, UControlRig* ControlRig, bool bDoKey, bool bdoMirror, float BlendValue);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Pose")
	FControlRigControlPose Pose;

};
