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
		TArray<FRigControl> CurrentControls; 
		ControlRig->GetControlsInOrder(CurrentControls);
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
		SetUpControlMap();
	}

	void PastePose(UControlRig* ControlRig, bool bDoKey, bool bDoMirror)
	{
		PastePoseInternal(ControlRig,bDoKey, bDoMirror, CopyOfControls);

	}

	void SetControlMirrorTransform(UControlRig* ControlRig, const FName& Name, const FVector& GlobalTranslation, const FQuat& GlobalRotation, bool bNotify, FRigControlModifiedContext Context)
	{
		FTransform NewGlobalTransform(GlobalRotation, GlobalTranslation);
		ControlRig->SetControlGlobalTransform(Name, NewGlobalTransform, bNotify, Context);
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
	
		TArray<FRigControl> SortedControls;
		ControlRig->GetControlsInOrder(SortedControls);
		for(const FRigControl& RigControl : SortedControls)
		{ 
			if (!ControlRig->IsControlSelected(RigControl.Name))
			{
				continue;
			}
			FRigControlCopy* CopyRigControl = MirrorTable.GetControl(*this,RigControl.Name);
			if (CopyRigControl)
			{
				if (CopyRigControl->ControlType == RigControl.ControlType)
				{
					switch (RigControl.ControlType)
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
							ControlRig->SetControlGlobalTransform(RigControl.Name, CopyRigControl->GlobalTransform,true, Context);
						}
						else
						{
							FVector GlobalTranslation;
							FQuat GlobalRotation;
							bool bIsMatched = MirrorTable.IsMatched(CopyRigControl->Name);
							MirrorTable.GetMirrorTransform(*CopyRigControl, bIsMatched,GlobalTranslation, GlobalRotation);
							SetControlMirrorTransform(ControlRig,RigControl.Name, GlobalTranslation, GlobalRotation,true, Context);
						}
						break;
					}		
					case ERigControlType::Float:
					{
						float Val = CopyRigControl->Value.Get<float>();
						ControlRig->SetControlValue<float>(RigControl.Name, Val, true, Context);
						break;
					}
					case ERigControlType::Bool:
					{
						bool Val = CopyRigControl->Value.Get<bool>();
						ControlRig->SetControlValue<bool>(RigControl.Name, Val, true, Context);
						break;
					}
					case ERigControlType::Integer:
					{
						int32 Val = CopyRigControl->Value.Get<int32>();
						ControlRig->SetControlValue<int32>(RigControl.Name, Val, true, Context);
						break;
					}
					case ERigControlType::Vector2D:
					{
						FVector2D Val = CopyRigControl->Value.Get<FVector2D>();
						ControlRig->SetControlValue<FVector2D>(RigControl.Name, Val, true, Context);
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

	void BlendWithInitialPoses(FControlRigControlPose& InitialPose, UControlRig *ControlRig, bool bDoKey, bool bDoMirror, float BlendValue)
	{
		if (InitialPose.CopyOfControls.Num() == 0)
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

		TArray<FRigControl> SortedControls;
		ControlRig->GetControlsInOrder(SortedControls);
		for (const FRigControl& RigControl : SortedControls)
		{
			if (!ControlRig->IsControlSelected(RigControl.Name))
			{
				continue;
			}
			FRigControlCopy* CopyRigControl = MirrorTable.GetControl(*this, RigControl.Name);
			if (CopyRigControl)
			{
				if (CopyRigControl->ControlType == RigControl.ControlType)
				{

					FRigControlCopy* InitialFound = nullptr;
					int32* Index = InitialPose.CopyOfControlsNameToIndex.Find(CopyRigControl->Name);
					if (Index)
					{
						InitialFound = &(InitialPose.CopyOfControls[*Index]);
					}
					if (InitialFound && InitialFound->ControlType == CopyRigControl->ControlType)
					{
						if ((CopyRigControl->ControlType == ERigControlType::Transform || CopyRigControl->ControlType == ERigControlType::EulerTransform ||
							CopyRigControl->ControlType == ERigControlType::TransformNoScale || CopyRigControl->ControlType == ERigControlType::Position ||
							CopyRigControl->ControlType == ERigControlType::Rotator || CopyRigControl->ControlType == ERigControlType::Scale
							))
						{
							if (bDoMirror == false)
							{
								FTransform Val = CopyRigControl->GlobalTransform;
								FTransform InitialVal = InitialFound->GlobalTransform;
								FVector Translation, Scale;
								FQuat Rotation;
								Translation = FMath::Lerp(InitialVal.GetTranslation(), Val.GetTranslation(), BlendValue);
								Rotation = FQuat::Slerp(InitialVal.GetRotation(), Val.GetRotation(), BlendValue); //doing slerp here not fast lerp, can be slow this is for content creation
								Scale = FMath::Lerp(InitialVal.GetScale3D(), Val.GetScale3D(), BlendValue);
								Val = FTransform(Rotation, Translation, Scale);
								ControlRig->SetControlGlobalTransform(RigControl.Name, Val, false, Context);
							}
							else
							{
								FVector GlobalTranslation;
								FQuat GlobalRotation;
								bool bIsMatched = MirrorTable.IsMatched(CopyRigControl->Name);
								MirrorTable.GetMirrorTransform(*CopyRigControl, bIsMatched, GlobalTranslation, GlobalRotation);
								FVector InitialTranslation = InitialFound->GlobalTransform.GetTranslation();
								FQuat InitialRotation = InitialFound->LocalTransform.GetRotation();
								GlobalTranslation = FMath::Lerp(InitialTranslation, GlobalTranslation, BlendValue);
								GlobalRotation = FQuat::Slerp(InitialRotation, GlobalRotation, BlendValue); //doing slerp here not fast lerp, can be slow this is for content creation
								SetControlMirrorTransform(ControlRig, RigControl.Name, GlobalTranslation, GlobalRotation, false, Context);
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

	void SetUpControlMap()
	{
		CopyOfControlsNameToIndex.Reset();
		
		for (int32 Index = 0;  Index <  CopyOfControls.Num(); ++ Index)
		{
			const FRigControlCopy& Control =  CopyOfControls[Index];
			CopyOfControlsNameToIndex.Add(Control.Name, Index);
		}
	}
	TArray<FRigControlCopy> GetPoses() const {return CopyOfControls;};

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Controls")
	TArray<FRigControlCopy> CopyOfControls;

	//Cache of the Map, Used to make pasting faster.
	TMap<FName, int32>  CopyOfControlsNameToIndex;
};


/**
* An indivual Pose made of Control Rig Controls
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
	void SelectControls(UControlRig* InControlRig);

	TArray<FRigControlCopy> GetCurrentPose(UControlRig* InControlRig);

	UFUNCTION(BlueprintCallable, Category = "Pose")
	void GetCurrentPose(UControlRig* InControlRig, FControlRigControlPose& OutPose);

	void BlendWithInitialPoses(FControlRigControlPose& InitialPose, UControlRig* ControlRig, bool bDoKey, bool bdoMirror, float BlendValue);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Pose")
	FControlRigControlPose Pose;

};
