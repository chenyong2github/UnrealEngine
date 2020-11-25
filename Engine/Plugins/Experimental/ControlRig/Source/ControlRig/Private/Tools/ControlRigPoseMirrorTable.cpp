// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/ControlRigPoseMirrorTable.h"
#include "Tools/ControlRigPoseMirrorSettings.h"
#include "ControlRig.h"
#include "Tools/ControlRigPose.h"

void FControlRigPoseMirrorTable::SetUpMirrorTable(const UControlRig* ControlRig)
{
	const UControlRigPoseMirrorSettings* Settings = GetDefault<UControlRigPoseMirrorSettings>();
	MatchedControls.Reset();
	if (Settings && ControlRig)
	{
		const TArray<FRigControl>& CurrentControls = ControlRig->AvailableControls();
		for (const FRigControl& RigControl : CurrentControls)
		{
			FString CurrentString = RigControl.Name.ToString();
			if (CurrentString.Contains(Settings->RightSide))
			{
				FString NewString = CurrentString.Replace(*Settings->RightSide, *Settings->LeftSide);
				FName CurrentName(*CurrentString);
				FName NewName(*NewString);
				MatchedControls.Add(NewName, CurrentName);
			}
			else if (CurrentString.Contains(Settings->LeftSide))
			{
				FString NewString = CurrentString.Replace(*Settings->LeftSide, *Settings->RightSide);
				FName CurrentName(*CurrentString);
				FName NewName(*NewString);
				MatchedControls.Add(NewName, CurrentName);
			}
		}
	}
}


FRigControlCopy* FControlRigPoseMirrorTable::GetControl(FControlRigControlPose& Pose, FName Name)
{

	TArray<FRigControlCopy> CopyOfControls;

	//Cache of the Map, Used to make pasting faster.
	TMap<FName, int32>  CopyOfControlsNameToIndex;

	if (MatchedControls.Num() <= 0)
	{
		int32* Index = Pose.CopyOfControlsNameToIndex.Find(Name);
		if (Index != nullptr && (*Index) >= 0 && (*Index) < Pose.CopyOfControls.Num())
		{
			return &(Pose.CopyOfControls[*Index]);
		}
	
	}
	if (MatchedControls.Num() > 0) 
	{
		if (const FName* MatchedName = MatchedControls.Find(Name))
		{

			int32* Index = Pose.CopyOfControlsNameToIndex.Find(*MatchedName);
			if (Index != nullptr && (*Index) >= 0 && (*Index) < Pose.CopyOfControls.Num())
			{
				return &(Pose.CopyOfControls[*Index]);
			}
		}
	}

	return nullptr;
}

bool FControlRigPoseMirrorTable::IsMatched(const FName& Name) const
{
	if (MatchedControls.Num() > 0)
	{
		if (const FName* MatchedName = MatchedControls.Find(Name))
		{
			return true;
		}
	}
	return false;
}

// Mirrors Translation as Global(Component)
// Mirrors Rotation as Local , only if NOT matched, if matched we just use it
void FControlRigPoseMirrorTable::GetMirrorTransform(const FRigControlCopy& ControlCopy, bool bIsMatched, FVector& OutGlobalTranslation, FQuat& OutGlobalRotation) const
{
	const UControlRigPoseMirrorSettings* Settings = GetDefault<UControlRigPoseMirrorSettings>();
	if (Settings)
	{
		FTransform GlobalTransform = ControlCopy.GlobalTransform;
		FTransform LocalTransform = ControlCopy.LocalTransform;
		//FQuat Rotation = LocalTransform.GetRotation();
		FQuat Rotation = GlobalTransform.GetRotation();

		FVector Axis(Settings->XAxis, Settings->YAxis, Settings->ZAxis);
		//if axis nearly zero just return
		if (Axis.IsNearlyZero())
		{
			OutGlobalTranslation = ControlCopy.GlobalTransform.GetTranslation();
			OutGlobalRotation = Rotation;
			return;
		}
		Axis.Normalize();

		FVector Translation = ControlCopy.GlobalTransform.GetTranslation();
		FPlane Plane(FVector::ZeroVector,Axis);
		OutGlobalTranslation = Translation.MirrorByPlane(Plane);

		//test FRotator OldRotator = Rotation.Rotator();
		
		if (bIsMatched)
		{
			OutGlobalRotation = Rotation;
		}
		else
		{
			FQuat MirrorNormalQuat = FQuat(Axis.X, Axis.Y, Axis.Z, 0);
			Rotation.EnforceShortestArcWith(MirrorNormalQuat);
			OutGlobalRotation = MirrorNormalQuat * Rotation * MirrorNormalQuat;
		}
		//test FRotator NewRotator = OutGlobalRotation.Rotator();
		
		return;
	}
	OutGlobalTranslation = FVector::ZeroVector;
	OutGlobalRotation = FQuat::Identity;
}
