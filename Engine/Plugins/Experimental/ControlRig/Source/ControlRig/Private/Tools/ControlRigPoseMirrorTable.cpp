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
				MatchedControls.Add(CurrentName, NewName);
			}
			else if (CurrentString.Contains(Settings->LeftSide))
			{
				FString NewString = CurrentString.Replace(*Settings->LeftSide, *Settings->RightSide);
				FName CurrentName(*CurrentString);
				FName NewName(*NewString);
				MatchedControls.Add(CurrentName, NewName);
			}
		}
	}
}

FRigControl* FControlRigPoseMirrorTable::GetControl(UControlRig* ControlRig, const FName& Name) const
{

	if (MatchedControls.Num() <= 0)
	{
		if (ControlRig->IsControlSelected(Name))
		{
			return ControlRig->FindControl(Name);
		}
	}
	if (MatchedControls.Num() > 0) 
	{
		if (const FName* MatchedName = MatchedControls.Find(Name))
		{
			if (ControlRig->IsControlSelected(*MatchedName))
			{
				if (FRigControl* RigControl = ControlRig->FindControl(*MatchedName))
				{
					return RigControl;
				}
			}
			else
			{
				return nullptr;
			}
		}
	}
	
	if (ControlRig->IsControlSelected(Name))
	{
		return ControlRig->FindControl(Name);
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
void FControlRigPoseMirrorTable::GetMirrorTransform(const FRigControlCopy& ControlCopy, bool bIsMatched, FVector& OutGlobalTranslation, FQuat& OutLocalRotation) const
{
	const UControlRigPoseMirrorSettings* Settings = GetDefault<UControlRigPoseMirrorSettings>();
	if (Settings)
	{
		FVector Axis(Settings->XAxis, Settings->YAxis, Settings->ZAxis);
		Axis.Normalize();
		FTransform GlobalTransform = ControlCopy.GlobalTransform;

		FVector Translation = ControlCopy.GlobalTransform.GetTranslation();
		FPlane Plane(FVector::ZeroVector,Axis);
		OutGlobalTranslation = Translation.MirrorByPlane(Plane);

		FTransform LocalTransform = ControlCopy.LocalTransform;
		FQuat Rotation = LocalTransform.GetRotation();
		//test FRotator OldRotator = Rotation.Rotator();
		
		if (bIsMatched)
		{
			OutLocalRotation = Rotation;
		}
		else
		{
			FQuat MirrorNormalQuat = FQuat(Axis.X, Axis.Y, Axis.Z, 0);
			Rotation.EnforceShortestArcWith(MirrorNormalQuat);
			OutLocalRotation = MirrorNormalQuat * Rotation * MirrorNormalQuat;
		}
		//test FRotator NewRotator = OutLocalRotation.Rotator();
		
		return;
	}
	OutGlobalTranslation = FVector::ZeroVector;
	OutLocalRotation = FQuat::Identity;
}
