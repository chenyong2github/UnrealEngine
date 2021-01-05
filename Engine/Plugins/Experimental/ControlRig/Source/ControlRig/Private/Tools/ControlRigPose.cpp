// Copyright Epic Games, Inc. All Rights Reserved.
#include "Tools/ControlRigPose.h"
#include "Tools/ControlRigPoseProjectSettings.h"
#include "IControlRigObjectBinding.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigPose"

void FControlRigControlPose::SavePose(UControlRig* ControlRig, bool bUseAll)
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

void FControlRigControlPose::PastePose(UControlRig* ControlRig, bool bDoKey, bool bDoMirror)
{
	PastePoseInternal(ControlRig, bDoKey, bDoMirror, CopyOfControls);
	ControlRig->Evaluate_AnyThread();
	PastePoseInternal(ControlRig, bDoKey, bDoMirror, CopyOfControls);

}

void FControlRigControlPose::SetControlMirrorTransform(UControlRig* ControlRig, const FName& Name, bool bIsMatched, const FVector& GlobalTranslation, const FQuat& GlobalRotation, const FQuat& LocalRotation, bool bNotify, const  FRigControlModifiedContext& Context)
{
	if (bIsMatched)
	{
		int32 Index = ControlRig->GetControlHierarchy().GetIndex(Name);
		FTransform ParentTransform = ControlRig->GetControlHierarchy().GetParentTransform(Index);
		FTransform CurrentTransform = ControlRig->GetControlHierarchy().GetLocalTransform(Index);
		FVector NewLocal = ParentTransform.InverseTransformPositionNoScale(GlobalTranslation);
		FTransform NewCurrentTransform(LocalRotation, NewLocal);
		ControlRig->SetControlLocalTransform(Name, NewCurrentTransform, bNotify, Context);
	}
	else
	{
		FTransform NewGlobalTransform(GlobalRotation, GlobalTranslation);
		ControlRig->SetControlGlobalTransform(Name, NewGlobalTransform, bNotify, Context);
	}
}

void FControlRigControlPose::PastePoseInternal(UControlRig* ControlRig, bool bDoKey, bool bDoMirror, const TArray<FRigControlCopy>& ControlsToPaste)
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
						ControlRig->SetControlGlobalTransform(RigControl.Name, CopyRigControl->GlobalTransform, true, Context);
					}
					else
					{
						FVector GlobalTranslation;
						FQuat GlobalRotation;
						FQuat LocalRotation;
						bool bIsMatched = MirrorTable.IsMatched(CopyRigControl->Name);
						MirrorTable.GetMirrorTransform(*CopyRigControl, bIsMatched, GlobalTranslation, GlobalRotation, LocalRotation);
						SetControlMirrorTransform(ControlRig, RigControl.Name, bIsMatched, GlobalTranslation, GlobalRotation, LocalRotation, true, Context);
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

void FControlRigControlPose::BlendWithInitialPoses(FControlRigControlPose& InitialPose, UControlRig* ControlRig, bool bDoKey, bool bDoMirror, float BlendValue)
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
							ControlRig->SetControlGlobalTransform(RigControl.Name, Val, bDoKey, Context);
						}
						else
						{
							FVector GlobalTranslation;
							FQuat GlobalRotation;
							FQuat LocalRotation;
							bool bIsMatched = MirrorTable.IsMatched(CopyRigControl->Name);
							MirrorTable.GetMirrorTransform(*CopyRigControl, bIsMatched, GlobalTranslation, GlobalRotation, LocalRotation);
							FVector InitialTranslation = InitialFound->GlobalTransform.GetTranslation();
							FQuat InitialGlobalRotation = InitialFound->GlobalTransform.GetRotation();
							FQuat InitialLocationRotation = InitialFound->LocalTransform.GetRotation();
							GlobalTranslation = FMath::Lerp(InitialTranslation, GlobalTranslation, BlendValue);
							GlobalRotation = FQuat::Slerp(InitialGlobalRotation, GlobalRotation, BlendValue); //doing slerp here not fast lerp, can be slow this is for content creation
							LocalRotation = FQuat::Slerp(InitialLocationRotation, LocalRotation, BlendValue); //doing slerp here not fast lerp, can be slow this is for content creation

							SetControlMirrorTransform(ControlRig, RigControl.Name, bIsMatched, GlobalTranslation, GlobalRotation, LocalRotation, bDoKey, Context);

						}
					}
				}
			}
		}
	}
}

bool FControlRigControlPose::ContainsName(const FName& Name) const
{
	const int32* Index = CopyOfControlsNameToIndex.Find(Name);
	return (Index && *Index >= 0);
}

void FControlRigControlPose::ReplaceControlName(const FName& Name, const FName& NewName)
{
	int32* Index = CopyOfControlsNameToIndex.Find(Name);
	if (Index && *Index >= 0)
	{
		FRigControlCopy& Control = CopyOfControls[*Index];
		Control.Name = NewName;
		CopyOfControlsNameToIndex.Remove(Name);
		CopyOfControlsNameToIndex.Add(Control.Name, *Index);
	}
}

TArray<FName> FControlRigControlPose::GetControlNames() const
{
	TArray<FName> Controls;
	Controls.Reserve(CopyOfControls.Num());
	for (const FRigControlCopy& Control : CopyOfControls)
	{
		Controls.Add(Control.Name);
	}
	return Controls;
}

void FControlRigControlPose::SetUpControlMap()
{
	CopyOfControlsNameToIndex.Reset();

	for (int32 Index = 0; Index < CopyOfControls.Num(); ++Index)
	{
		const FRigControlCopy& Control = CopyOfControls[Index];
		CopyOfControlsNameToIndex.Add(Control.Name, Index);
	}
}


UControlRigPoseAsset::UControlRigPoseAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
}

void UControlRigPoseAsset::PostLoad()
{
	Super::PostLoad();
	Pose.SetUpControlMap();
}

void UControlRigPoseAsset::SavePose(UControlRig* InControlRig, bool bUseAll)
{
	Pose.SavePose(InControlRig,bUseAll);
}

void UControlRigPoseAsset::PastePose(UControlRig* InControlRig, bool bDoKey, bool bDoMirror)
{
#if WITH_EDITOR
	FScopedTransaction ScopedTransaction(LOCTEXT("PastePoseTransaction", "Paste Pose"));
	InControlRig->Modify();
#endif
	Pose.PastePose(InControlRig,bDoKey, bDoMirror);
}

void UControlRigPoseAsset::SelectControls(UControlRig* InControlRig)
{
#if WITH_EDITOR
	FScopedTransaction ScopedTransaction(LOCTEXT("SelectControlTransaction", "Select Control"));
	InControlRig->Modify();
#endif
	InControlRig->ClearControlSelection();
	TArray<FName> Controls = Pose.GetControlNames();
	for (const FName& Name : Controls)
	{
		InControlRig->SelectControl(Name, true);
	}
}

void UControlRigPoseAsset::GetCurrentPose(UControlRig* InControlRig, FControlRigControlPose& OutPose)
{
	OutPose.SavePose(InControlRig, true);
}


TArray<FRigControlCopy> UControlRigPoseAsset::GetCurrentPose(UControlRig* InControlRig) 
{
	FControlRigControlPose TempPose;
	TempPose.SavePose(InControlRig,true);
	return TempPose.GetPoses();
}

void UControlRigPoseAsset::BlendWithInitialPoses(FControlRigControlPose& InitialPose, UControlRig* InControlRig, bool bDoKey, bool bDoMirror, float BlendValue)
{
	if (BlendValue > 0.0f)
	{
		Pose.BlendWithInitialPoses(InitialPose, InControlRig, bDoKey, bDoMirror, BlendValue);
	}
}

TArray<FName> UControlRigPoseAsset::GetControlNames() const
{
	return Pose.GetControlNames();
}

void UControlRigPoseAsset::ReplaceControlName(const FName& CurrentName, const FName& NewName)
{
	Pose.ReplaceControlName(CurrentName, NewName);
}

bool UControlRigPoseAsset::DoesMirrorMatch(UControlRig* ControlRig, const FName& ControlName) const
{
	FControlRigPoseMirrorTable MirrorTable;
	MirrorTable.SetUpMirrorTable(ControlRig);
	return (MirrorTable.IsMatched(ControlName));
}



#undef LOCTEXT_NAMESPACE

