// Copyright Epic Games, Inc. All Rights Reserved.
#include "Tools/ControlRigPose.h"
#include "Tools/ControlRigPoseProjectSettings.h"
#include "IControlRigObjectBinding.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigPose"

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

#undef LOCTEXT_NAMESPACE

