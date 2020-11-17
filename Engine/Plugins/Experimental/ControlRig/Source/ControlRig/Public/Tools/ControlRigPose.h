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
USTRUCT()
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
		, InitialValue()
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
		InitialValue = InControl.InitialValue;

		int32 Index = Hierarchy.GetIndex(Name);
		ParentTransform = Index != INDEX_NONE ? Hierarchy.GetParentTransform(Index) : FTransform::Identity;
		LocalTransform = Hierarchy.GetLocalTransform(Name);
		GlobalTransform = Hierarchy.GetGlobalTransform(Name);
	}
	virtual ~FRigControlCopy() {}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	ERigControlType ControlType;

	UPROPERTY()
	FRigControlValue Value;

	UPROPERTY()
	FName ParentName;

	UPROPERTY()
	FName SpaceName;

	UPROPERTY()
	FTransform OffsetTransform;

	UPROPERTY()
	FRigControlValue InitialValue;

	UPROPERTY()
	FTransform ParentTransform;

	UPROPERTY()
	FTransform LocalTransform;

	UPROPERTY()
	FTransform GlobalTransform;

};

/**
* The Data Stored For Each Pose and associated Functions to Store and Paste It
*/
USTRUCT()
struct FControlRigControlPose
{
	GENERATED_USTRUCT_BODY()

	FControlRigControlPose() {};
	FControlRigControlPose(UControlRig* InControlRig, bool bUseAll)
	{
		SavePose(InControlRig, bUseAll);
	}
	~FControlRigControlPose() {};

	void SavePose(UControlRig* ControlRig,  bool bUseAll)
	{
		const TArray<FRigControl>& CurrentControls = ControlRig->AvailableControls();
		CopyOfControls.SetNum(0);
		FRigControlHierarchy& Hierarchy = ControlRig->GetControlHierarchy();
		for (const FRigControl& RigControl : CurrentControls)
		{
			if (RigControl.bAnimatable && (bUseAll || ControlRig->IsControlSelected(RigControl.Name)))
			{
				FRigControlCopy Copy(RigControl, Hierarchy);
				CopyOfControls.Add(Copy);
			}
		}
	}

	void PastePose(UControlRig* ControlRig, bool bDoKey, bool bDoMirror)
	{
		PastePoseInternal(ControlRig,bDoKey, bDoMirror, CopyOfControls);
		//do it twice do to issues we have seen with the ordering of certain controls,due to how spaces and offsets work.
		PastePoseInternal(ControlRig, bDoKey, bDoMirror, CopyOfControls);
	}

	void SetControlMirrorTransform(UControlRig* ControlRig, const FName& Name, const FVector& GlobalTranslation, const FQuat& LocalRotation, bool bNotify, FRigControlModifiedContext Context)
	{
		int32 Index = ControlRig->GetControlHierarchy().GetIndex(Name);
		FTransform ParentTransform = ControlRig->GetControlHierarchy().GetParentTransform(Index);
		FTransform CurrentTransform = ControlRig->GetControlHierarchy().GetLocalTransform(Index);
		FVector NewLocal = ParentTransform.InverseTransformPositionNoScale(GlobalTranslation);
		FTransform NewCurrentTransform(LocalRotation, NewLocal);
		ControlRig->SetControlLocalTransform(Name, NewCurrentTransform, bNotify, Context);
	}

	void PastePoseInternal(UControlRig* ControlRig, bool bDoKey,bool bDoMirror, const TArray<FRigControlCopy>& ControlsToPaste)
	{
		FRigControlModifiedContext Context;
		Context.SetKey = bDoKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		FControlRigPoseMirrorTable MirrorTable;
		if (bDoMirror)
		{
			MirrorTable.SetUpMirrorTable(ControlRig);
		}
	
		for (const FRigControlCopy& CopyRigControl : ControlsToPaste)
		{
			FRigControl* RigControl = MirrorTable.GetControl(ControlRig,CopyRigControl.Name);
			if (RigControl)
			{
				if (CopyRigControl.ControlType == RigControl->ControlType)
				{
					switch (RigControl->ControlType)
					{
					case ERigControlType::Transform:
					case ERigControlType::TransformNoScale:
					case ERigControlType::EulerTransform:
					case ERigControlType::Position:
					case ERigControlType::Scale:
					case ERigControlType::Rotator:
					{
						if (bDoMirror == false)
						{
							ControlRig->SetControlGlobalTransform(RigControl->Name, CopyRigControl.GlobalTransform,Context);
						}
						else
						{
							FVector GlobalTranslation;
							FQuat LocalRotation;
							bool bIsMatched = MirrorTable.IsMatched(RigControl->Name);
							MirrorTable.GetMirrorTransform(CopyRigControl, bIsMatched,GlobalTranslation, LocalRotation);
							SetControlMirrorTransform(ControlRig,RigControl->Name, GlobalTranslation, LocalRotation,true, Context);
						}
						break;
					}		
					case ERigControlType::Float:
					{
						float Val = CopyRigControl.Value.Get<float>();
						ControlRig->SetControlValue<float>(RigControl->Name, Val, true, Context);
						break;
					}
					case ERigControlType::Bool:
					{
						bool Val = CopyRigControl.Value.Get<bool>();
						ControlRig->SetControlValue<bool>(RigControl->Name, Val, true, Context);
						break;
					}
					case ERigControlType::Integer:
					{
						int32 Val = CopyRigControl.Value.Get<int32>();
						ControlRig->SetControlValue<int32>(RigControl->Name, Val, true, Context);
						break;
					}
					case ERigControlType::Vector2D:
					{
						FVector2D Val = CopyRigControl.Value.Get<FVector2D>();
						ControlRig->SetControlValue<FVector2D>(RigControl->Name, Val, true, Context);
						break;
					}	
					default:
						//TODO add log
						break;
					};
				}
			}
		}
	}

	void BlendWithInitialPoses(TArray<FRigControlCopy>& InitialControls, UControlRig *ControlRig, bool bDoKey, bool bDoMirror, float BlendValue)
	{
		if (InitialControls.Num() == 0)
		{
			return;
		}

		//though can be n^2 should be okay, we search from current Index which in most cases will be the same
		//not run often anyway
		FRigControlModifiedContext Context;
		Context.SetKey = bDoKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		FControlRigPoseMirrorTable MirrorTable;
		if (bDoMirror)
		{
			MirrorTable.SetUpMirrorTable(ControlRig);
		}

		for (int32 Index = 0; Index <CopyOfControls.Num(); ++Index)
		{
			FRigControlCopy& CopyRigControl = CopyOfControls[Index];
			FRigControl* RigControl = MirrorTable.GetControl(ControlRig, CopyRigControl.Name);
			if (RigControl)
			{
				if (RigControl->ControlType == CopyRigControl.ControlType)
				{
					FRigControlCopy* InitialFound = nullptr;
					for (int32 NewIncIndex = Index; NewIncIndex < InitialControls.Num(); ++NewIncIndex)
					{
						if (CopyOfControls[Index].Name == InitialControls[NewIncIndex].Name)
						{
							InitialFound = &InitialControls[NewIncIndex];
							break;
						}
					}
					if (!InitialFound && Index >= 1)
					{
						//now search backwards to zero
						for (int32 NewDecIndex = Index - 1; NewDecIndex >= 0; --NewDecIndex)
						{
							if (CopyOfControls[Index].Name == InitialControls[NewDecIndex].Name)
							{
								InitialFound = &InitialControls[NewDecIndex];
								break;
							}
						}
					}
					if (InitialFound && InitialFound->ControlType == CopyOfControls[Index].ControlType)
					{
						if ((CopyRigControl.ControlType == ERigControlType::Transform || CopyRigControl.ControlType == ERigControlType::EulerTransform ||
							CopyRigControl.ControlType == ERigControlType::TransformNoScale || CopyRigControl.ControlType == ERigControlType::Position ||
							CopyRigControl.ControlType == ERigControlType::Rotator || CopyRigControl.ControlType == ERigControlType::Scale
							))
						{
							if (bDoMirror == false)
							{
								FTransform Val = CopyRigControl.GlobalTransform;
								FTransform InitialVal = InitialFound->GlobalTransform;
								FVector Translation, Scale;
								FQuat Rotation;
								Translation = FMath::Lerp(InitialVal.GetTranslation(), Val.GetTranslation(), BlendValue);
								Rotation = FQuat::Slerp(InitialVal.GetRotation(), Val.GetRotation(), BlendValue); //doing slerp here not fast lerp, can be slow this is for content creation
								Scale = FMath::Lerp(InitialVal.GetScale3D(), Val.GetScale3D(), BlendValue);
								Val = FTransform(Rotation, Translation, Scale);
								ControlRig->SetControlGlobalTransform(RigControl->Name, Val, Context);
							}
							else
							{
								FVector GlobalTranslation;
								FQuat LocalRotation;
								bool bIsMatched = MirrorTable.IsMatched(CopyRigControl.Name);
								MirrorTable.GetMirrorTransform(CopyRigControl, bIsMatched, GlobalTranslation, LocalRotation);
								FVector InitialTranslation = InitialFound->GlobalTransform.GetTranslation();
								FQuat InitialRotation = InitialFound->LocalTransform.GetRotation();
								GlobalTranslation = FMath::Lerp(InitialTranslation, GlobalTranslation, BlendValue);
								LocalRotation = FQuat::Slerp(InitialRotation, LocalRotation, BlendValue); //doing slerp here not fast lerp, can be slow this is for content creation
								SetControlMirrorTransform(ControlRig, RigControl->Name, GlobalTranslation, LocalRotation, false, Context);
							}
						}
					}
				}
			}			
		}
	}

	TArray<FName> GetControlNames() const
	{
		TArray<FName> Controls;
		Controls.Reserve(CopyOfControls.Num());
		for (const FRigControlCopy& Control : CopyOfControls)
		{
			Controls.Add(Control.Name);
		}
		return Controls;
	}

	TArray<FRigControlCopy> GetPoses() const {return CopyOfControls;};

	UPROPERTY()
	TArray<FRigControlCopy> CopyOfControls;
};


/**
* The Actual Pose Asset that holds the Pose.
*/
UCLASS(BlueprintType)
class CONTROLRIG_API UControlRigPoseAsset : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SavePose(UControlRig* InControlRig, bool bUseAll);

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void PastePose(UControlRig* InControlRig, bool bDoKey = false, bool bDoMirror = false);
	
	UFUNCTION(BlueprintCallable, Category = "Pose")
	void SelectControls(UControlRig* InControlRig);
	
	UFUNCTION(BlueprintCallable, Category = "Pose")
	const TArray<FName>& GetControlNames() const { return Controls; }

	TArray<FRigControlCopy> GetCurrentPose(UControlRig* InControlRig);
	void BlendWithInitialPoses(TArray<FRigControlCopy>& InitialControls, UControlRig* ControlRig, bool bDoKey, bool bdoMirror, float BlendValue);

private:

	UPROPERTY()
	FControlRigControlPose Pose;

	/** Controls we are made of */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FName> Controls;

};
