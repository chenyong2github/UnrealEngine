// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsControlComponentImpl.h"
#include "PhysicsControlComponentLog.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlComponentHelpers.h"

#include "Components/SkeletalMeshComponent.h"

//======================================================================================================================
bool FPhysicsControlComponentImpl::GetBoneData(
	FCachedSkeletalMeshData::FBoneData& OutBoneData,
	const USkeletalMeshComponent*       InSkeletalMeshComponent,
	const FName                         InBoneName) const
{
	check(InSkeletalMeshComponent);
	const FReferenceSkeleton& RefSkeleton = InSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(InBoneName);

	if (BoneIndex == INDEX_NONE)
	{
		UE_LOG(LogPhysicsControlComponent, Warning, TEXT("Failed to find BoneIndex for %s"), *InBoneName.ToString());
		return false;
	}

	const FCachedSkeletalMeshData* CachedSkeletalMeshData = CachedSkeletalMeshDatas.Find(InSkeletalMeshComponent);
	if (CachedSkeletalMeshData &&
		CachedSkeletalMeshData->ReferenceCount > 0 &&
		!CachedSkeletalMeshData->BoneData.IsEmpty())
	{
		if (BoneIndex < CachedSkeletalMeshData->BoneData.Num())
		{
			OutBoneData = CachedSkeletalMeshData->BoneData[BoneIndex];
			return true;
		}
		UE_LOG(LogPhysicsControlComponent, Warning, TEXT("BoneIndex is out of range"));

	}
	UE_LOG(LogPhysicsControlComponent, Warning, TEXT("Failed to find bone data for %s"), *InBoneName.ToString());
	return false;
}

//======================================================================================================================
FPhysicsControlRecord* FPhysicsControlComponentImpl::FindControlRecord(const FName Name)
{
	if (PhysicsControlRecords.IsEmpty())
	{
		return nullptr;
	}
	if (Name.IsNone())
	{
		return &PhysicsControlRecords.CreateIterator().Value();
	}
	if (FPhysicsControlRecord* Record = PhysicsControlRecords.Find(Name))
	{
		return Record;
	}
	return nullptr;
}

//======================================================================================================================
FPhysicsControl* FPhysicsControlComponentImpl::FindControl(const FName Name)
{
	if (FPhysicsControlRecord* ControlRecord = FindControlRecord(Name))
	{
		return &ControlRecord->PhysicsControl;
	}
	return nullptr;
}

//======================================================================================================================
bool FPhysicsControlComponentImpl::DetectTeleport(
	const FTransform& OldComponentTM, const FTransform& NewComponentTM) const
{
	if (Owner->TeleportDistanceThreshold > 0)
	{
		double Distance = FVector::Distance(OldComponentTM.GetTranslation(), NewComponentTM.GetTranslation());
		if (Distance > Owner->TeleportDistanceThreshold)
		{
			return true;
		}
	}
	if (Owner->TeleportRotationThreshold > 0)
	{
		double Radians = OldComponentTM.GetRotation().AngularDistance(NewComponentTM.GetRotation());
		if (FMath::RadiansToDegrees(Radians) > Owner->TeleportRotationThreshold)
		{
			return true;
		}
	}
	return false;
}


//======================================================================================================================
void FPhysicsControlComponentImpl::UpdateCachedSkeletalBoneData(float Dt)
{
	for (TPair<TObjectPtr<USkeletalMeshComponent>, FCachedSkeletalMeshData>& CachedSkeletalMeshDataPair :
		CachedSkeletalMeshDatas)
	{
		FCachedSkeletalMeshData& CachedSkeletalMeshData = CachedSkeletalMeshDataPair.Value;
		if (!CachedSkeletalMeshData.ReferenceCount)
		{
			continue;
		}

		TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = CachedSkeletalMeshDataPair.Key;
		if (USkeletalMeshComponent* SkeletalMesh = SkeletalMeshComponent.Get())
		{
			FTransform ComponentTM = SkeletalMesh->GetComponentToWorld();
			const TArray<FTransform>& TMs = SkeletalMesh->GetEditableComponentSpaceTransforms();
			if (TMs.Num() == CachedSkeletalMeshData.BoneData.Num() &&
				!DetectTeleport(CachedSkeletalMeshData.ComponentTM, ComponentTM))
			{
				for (int32 Index = 0; Index != TMs.Num(); ++Index)
				{
					FTransform TM = TMs[Index] * ComponentTM;
					CachedSkeletalMeshData.BoneData[Index].Update(TM.GetTranslation(), TM.GetRotation(), Dt);
				}
			}
			else
			{
				CachedSkeletalMeshData.BoneData.Empty(TMs.Num());
				for (const FTransform& BoneTM : TMs)
				{
					FTransform TM = BoneTM * ComponentTM;
					CachedSkeletalMeshData.BoneData.Emplace(TM.GetTranslation(), TM.GetRotation());
				}
			}
			CachedSkeletalMeshData.ComponentTM = ComponentTM;
		}
		else
		{
			CachedSkeletalMeshData.BoneData.Empty();
		}
	}
}

//======================================================================================================================
void FPhysicsControlComponentImpl::ResetControls(bool bKeepControlRecords)
{
	for (TPair<FName, FPhysicsControlRecord>& PhysicsControlRecordPair : PhysicsControlRecords)
	{
		FPhysicsControlRecord& Record = PhysicsControlRecordPair.Value;
		Record.PhysicsControlState.Reset();
	}

	if (!bKeepControlRecords)
	{
		PhysicsControlRecords.Empty();
	}
}

//======================================================================================================================
// Currently this looks for world-space targets from the controls, and forms a strength-weighted
// average of them if there are multiple targets. However, it would probably be better to replace
// this with an explicit kinematic target on each body modifier, as it is a little unintuitive to
// make zero strength physical controls. UE-159655
void FPhysicsControlComponentImpl::ApplyKinematicTarget(const FPhysicsBodyModifier& BodyModifier) const
{
	FBodyInstance* BodyInstance = GetBodyInstance(BodyModifier.MeshComponent, BodyModifier.BoneName);
	if (!BodyInstance)
	{
		return;
	}

	// First find any controls that are (a) acting in world space (b) driving the modified object
	float PositionWeight = 0.0f;
	float OrientationWeight = 0.0f;
	FVector WeightedPosition(ForceInitToZero);
	FVector4 WeightedOrientation(ForceInitToZero);
	for (const TPair<FName, FPhysicsControlRecord>& PhysicsControlRecordPair : PhysicsControlRecords)
	{
		const FPhysicsControlRecord& Record = PhysicsControlRecordPair.Value;
		FBodyInstance* ParentBodyInstance = GetBodyInstance(
			Record.PhysicsControl.ParentMeshComponent, Record.PhysicsControl.ParentBoneName);

		if (!ParentBodyInstance)
		{
			FBodyInstance* ChildBodyInstance = GetBodyInstance(
				Record.PhysicsControl.ChildMeshComponent, Record.PhysicsControl.ChildBoneName);
			if (BodyInstance == ChildBodyInstance)
			{
				FTransform TargetTM;
				FVector TargetVelocity;
				FVector TargetAngularVelocity;
				CalculateControlTargetData(TargetTM, TargetVelocity, TargetAngularVelocity, Record, false);

				// TargetTM is actually the target for the control point, but we will be setting the body TM
				// so we need to remove the offset
				TargetTM.AddToTranslation(TargetTM.GetRotation() * -Record.PhysicsControl.ControlSettings.ControlPoint);

				// TODO note that this isn't using the multipliers, or the force limits etc. Using an explicit
				// kinematic target will solve this
				float LinearWeight = Record.PhysicsControl.ControlData.LinearStrength + UE_SMALL_NUMBER;
				float AngularWeight = Record.PhysicsControl.ControlData.AngularStrength + UE_SMALL_NUMBER;

				WeightedPosition += TargetTM.GetTranslation() * LinearWeight;
				PositionWeight += LinearWeight;

				FQuat Q = TargetTM.GetRotation();
				Q.EnforceShortestArcWith(FQuat::Identity);
				WeightedOrientation += FVector4(Q.X, Q.Y, Q.Z, Q.W) * AngularWeight;
				OrientationWeight += AngularWeight;
			}
		}
	}

	if (PositionWeight <= 0 && OrientationWeight <= 0)
	{
		return;
	}

	// Seems like static and skeletal meshes need to be handled differently
	if (BodyModifier.MeshComponent.IsA<USkeletalMeshComponent>())
	{
		FTransform NewTM = BodyInstance->GetUnrealWorldTransform();
		if (PositionWeight)
		{
			FVector NewPosition = WeightedPosition / PositionWeight;
			NewTM.SetLocation(NewPosition);
		}
		if (OrientationWeight)
		{
			WeightedOrientation /= OrientationWeight;
			FQuat TargetOrientation = FQuat(
				WeightedOrientation.X, WeightedOrientation.Y, WeightedOrientation.Z, WeightedOrientation.W);
			TargetOrientation.Normalize();
			NewTM.SetRotation(TargetOrientation);
		}

		BodyInstance->SetBodyTransform(NewTM, ETeleportType::None);
	}
	else
	{
		if (PositionWeight)
		{
			FVector TargetPosition = WeightedPosition / PositionWeight;
			BodyModifier.MeshComponent->SetWorldLocation(TargetPosition, false, nullptr, ETeleportType::None);
		}

		if (OrientationWeight)
		{
			WeightedOrientation /= OrientationWeight;
			FQuat TargetOrientation = FQuat(
				WeightedOrientation.X, WeightedOrientation.Y, WeightedOrientation.Z, WeightedOrientation.W);
			TargetOrientation.Normalize();
			BodyModifier.MeshComponent->SetWorldRotation(TargetOrientation, false, nullptr, ETeleportType::None);
		}
	}
}

//======================================================================================================================
void FPhysicsControlComponentImpl::AddSkeletalMeshReference(USkeletalMeshComponent* InSkeletalMeshComponent)
{
	check(InSkeletalMeshComponent);
	for (TPair<TObjectPtr<USkeletalMeshComponent>, FCachedSkeletalMeshData>& CachedSkeletalMeshDataPair :
		CachedSkeletalMeshDatas)
	{
		TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = CachedSkeletalMeshDataPair.Key;
		if (SkeletalMeshComponent == InSkeletalMeshComponent)
		{
			FCachedSkeletalMeshData& CachedSkeletalMeshData = CachedSkeletalMeshDataPair.Value;
			++CachedSkeletalMeshData.ReferenceCount;
			return;
		}
	}
	FCachedSkeletalMeshData& Data = CachedSkeletalMeshDatas.Add(InSkeletalMeshComponent);
	Data.ReferenceCount = 1;
	Owner->PrimaryComponentTick.AddPrerequisite(InSkeletalMeshComponent, InSkeletalMeshComponent->PrimaryComponentTick);
}

//======================================================================================================================
void FPhysicsControlComponentImpl::RemoveSkeletalMeshReference(USkeletalMeshComponent* InSkeletalMeshComponent)
{
	check(InSkeletalMeshComponent);
	if (!InSkeletalMeshComponent)
	{
		UE_LOG(LogPhysicsControlComponent, Warning, TEXT("Invalid skeletal mesh component"));
		return;
	}

	for (auto It = CachedSkeletalMeshDatas.CreateIterator(); It; ++It)
	{
		TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = It.Key();
		FCachedSkeletalMeshData& CachedSkeletalMeshData = It.Value();
		if (SkeletalMeshComponent == InSkeletalMeshComponent)
		{
			if (--CachedSkeletalMeshData.ReferenceCount == 0)
			{
				Owner->PrimaryComponentTick.RemovePrerequisite(
					InSkeletalMeshComponent, InSkeletalMeshComponent->PrimaryComponentTick);
				It.RemoveCurrent();
			}
			return;
		}
	}
	UE_LOG(LogPhysicsControlComponent, Warning, TEXT("Failed to remove skeletal mesh component dependency"));
}

//======================================================================================================================
FName FPhysicsControlComponentImpl::GetUniqueControlName(const FName ParentBoneName, const FName ChildBoneName) const
{
	FString NameBase = TEXT("");
	if (!ParentBoneName.IsNone())
	{
		NameBase += ParentBoneName.ToString() + TEXT("_");
	}
	if (!ChildBoneName.IsNone())
	{
		NameBase += ChildBoneName.ToString() + TEXT("_");
	}

	TSet<FName> Keys;
	PhysicsControlRecords.GetKeys(Keys);
	// If the number gets too large, almost certainly we're in some nasty situation where this is
	// getting called in a loop. Better to quit and fail, rather than allow the constraint set to
	// increase without bound. 
	for (int32 Index = 0; Index < Owner->MaxNumControlsOrModifiersPerName; ++Index)
	{
		FString NameStr = FString::Format(TEXT("{0}{1}"), { NameBase, Index });
		FName Name(NameStr);
		if (!Keys.Find(Name))
		{
			return Name;
		}
	}
	UE_LOG(LogPhysicsControlComponent, Warning,
		TEXT("Unable to find a suitable Control name - the limit of MaxNumControlsOrModifiersPerName (%d) has been exceeded"),
		Owner->MaxNumControlsOrModifiersPerName);
	return FName();
}

//======================================================================================================================
FName FPhysicsControlComponentImpl::GetUniqueBodyModifierName(const FName BoneName) const
{
	FString NameBase = TEXT("");
	if (!BoneName.IsNone())
	{
		NameBase += BoneName.ToString() + TEXT("_");
	}
	else
	{
		NameBase = TEXT("Body_");
	}

	TSet<FName> Keys;
	PhysicsBodyModifiers.GetKeys(Keys);
	// If the number gets too large, almost certainly we're in some nasty situation where this is
	// getting called in a loop. Better to quit and fail, rather than allow the modifier set to
	// increase without bound. 
	for (int32 Index = 0; Index != Owner->MaxNumControlsOrModifiersPerName; ++Index)
	{
		FString NameStr = FString::Format(TEXT("{0}{1}"), { NameBase, Index });
		FName Name(NameStr);
		if (!Keys.Find(Name))
		{
			return Name;
		}
	}
	UE_LOG(LogPhysicsControlComponent, Warning,
		TEXT("Unable to find a suitable Body Modifier name - the limit of MaxNumControlsOrModifiersPerName (%d) has been exceeded"),
		Owner->MaxNumControlsOrModifiersPerName);
	return FName();
}

//======================================================================================================================
void FPhysicsControlComponentImpl::CalculateControlTargetData(
	FTransform&                  OutTargetTM,
	FVector&                     OutTargetVelocity,
	FVector&                     OutTargetAngularVelocity,
	const FPhysicsControlRecord& Record,
	bool                         bCalculateVelocity) const
{
	const FPhysicsControlTarget& Target = Record.PhysicsControl.ControlTarget;

	// Calculate the authored target position/orientation - i.e. not using the skeletal animation
	FQuat TargetOrientationQ = Target.TargetOrientation.Quaternion();

	// Incorporate the offset from the control point
	FVector ExtraTargetPosition =
		Record.PhysicsControl.ControlTarget.bApplyControlPointToTarget
		? Record.PhysicsControl.ControlSettings.ControlPoint
		: FVector::ZeroVector;
	FVector ExtraTargetPositionWorld = TargetOrientationQ * ExtraTargetPosition;
	FVector TargetPosition = Target.TargetPosition + ExtraTargetPositionWorld;

	if (bCalculateVelocity)
	{
		// Note that Target.TargetAngularVelocity is in revs per second (as it's user-facing)
		OutTargetAngularVelocity = Target.TargetAngularVelocity * UE_TWO_PI;
		FVector ExtraVelocity = OutTargetAngularVelocity.Cross(ExtraTargetPositionWorld);
		OutTargetVelocity = Target.TargetVelocity + ExtraVelocity;
	}
	else
	{
		OutTargetVelocity.Set(0, 0, 0);
		OutTargetAngularVelocity.Set(0, 0, 0);
	}

	// OutTargetTM is the target transform of the constraint's child frame relative to the
	// constraint's parent frame
	OutTargetTM = FTransform(TargetOrientationQ, TargetPosition);

	// Adjust based on any skeletal action
	if (Record.PhysicsControl.ControlSettings.bUseSkeletalAnimation)
	{
		FCachedSkeletalMeshData::FBoneData ChildBoneData, ParentBoneData;
		bool bHaveChildBoneData = false;
		bool bHaveParentBoneData = false;

		if (USkeletalMeshComponent* ChildSkeletalMeshComponent =
			Cast<USkeletalMeshComponent>(Record.PhysicsControl.ChildMeshComponent.Get()))
		{
			bHaveChildBoneData = GetBoneData(
				ChildBoneData, ChildSkeletalMeshComponent, Record.PhysicsControl.ChildBoneName);
		}

		if (USkeletalMeshComponent* ParentSkeletalMeshComponent =
			Cast<USkeletalMeshComponent>(Record.PhysicsControl.ParentMeshComponent.Get()))
		{
			bHaveParentBoneData = GetBoneData(
				ParentBoneData, ParentSkeletalMeshComponent, Record.PhysicsControl.ParentBoneName);
		}

		// Note that the TargetTM/velocity calculated so far are supposed to be interpreted as
		// expressed relative to the skeletal animation pose.
		//
		// Also note that the velocities calculated in the bone data are the strict rates of change
		// of the transform position/orientation - not of the center of mass (which is what physics
		// bodies often use for velocity).
		if (bHaveChildBoneData)
		{
			FTransform ChildBoneTM = ChildBoneData.GetTM();
			if (bHaveParentBoneData)
			{
				FTransform ParentBoneTM = ParentBoneData.GetTM();
				FTransform SkeletalDeltaTM = ChildBoneTM * ParentBoneTM.Inverse();
				// This puts TargetTM in the space of the ParentBone
				OutTargetTM = OutTargetTM * SkeletalDeltaTM;

				FQuat ParentBoneQ = ParentBoneTM.GetRotation();
				FQuat ParentBoneInvQ = ParentBoneQ.Inverse();

				if (bCalculateVelocity)
				{
					OutTargetVelocity = SkeletalDeltaTM.GetRotation() * OutTargetVelocity;
					OutTargetAngularVelocity = SkeletalDeltaTM.GetRotation() * OutTargetAngularVelocity;

					if (Record.PhysicsControl.ControlSettings.SkeletalAnimationVelocityMultiplier)
					{
						// Offset of the control point from the target child bone TM, in world space.
						FVector WorldControlPointOffset =
							ChildBoneTM.GetRotation() * Record.PhysicsControl.ControlSettings.ControlPoint;
						// World space position of the target control point
						FVector WorldChildControlPointPosition = ChildBoneTM.GetTranslation() + WorldControlPointOffset;

						// World-space velocity of the control point due to the motion of the parent
						// linear and angular velocity.
						FVector ChildTargetVelocityDueToParent =
							ParentBoneData.Velocity + ParentBoneData.AngularVelocity.Cross(
								WorldChildControlPointPosition - ParentBoneTM.GetTranslation());
						// World-space velocity of the control point due to the motion of the child
						// linear and angular velocity
						FVector ChildTargetVelocity =
							ChildBoneData.Velocity + ChildBoneData.AngularVelocity.Cross(WorldControlPointOffset);

						// Pull out just the motion in the child that isn't due to the parent
						FVector SkeletalTargetVelocity =
							ParentBoneInvQ * (ChildTargetVelocity - ChildTargetVelocityDueToParent);
						OutTargetVelocity += SkeletalTargetVelocity *
							Record.PhysicsControl.ControlSettings.SkeletalAnimationVelocityMultiplier;

						FVector SkeletalTargetAngularVelocity =
							ParentBoneInvQ * (ChildBoneData.AngularVelocity - ParentBoneData.AngularVelocity);
						OutTargetAngularVelocity += SkeletalTargetAngularVelocity *
							Record.PhysicsControl.ControlSettings.SkeletalAnimationVelocityMultiplier;
					}
				}
			}
			else
			{
				OutTargetTM = OutTargetTM * ChildBoneTM;

				if (bCalculateVelocity)
				{
					OutTargetVelocity = ChildBoneTM.GetRotation() * OutTargetVelocity;
					OutTargetAngularVelocity = ChildBoneTM.GetRotation() * OutTargetAngularVelocity;

					if (Record.PhysicsControl.ControlSettings.SkeletalAnimationVelocityMultiplier)
					{
						FVector WorldControlPointOffset =
							ChildBoneTM.GetRotation() * Record.PhysicsControl.ControlSettings.ControlPoint;
						FVector WorldChildControlPointPosition = ChildBoneTM.GetTranslation() + WorldControlPointOffset;

						// World-space velocity of the control point due to the motion of the child
						FVector ChildTargetVelocity =
							ChildBoneData.Velocity + ChildBoneData.AngularVelocity.Cross(WorldControlPointOffset);

						OutTargetVelocity += ChildTargetVelocity *
							Record.PhysicsControl.ControlSettings.SkeletalAnimationVelocityMultiplier;

						OutTargetAngularVelocity += ChildBoneData.AngularVelocity *
							Record.PhysicsControl.ControlSettings.SkeletalAnimationVelocityMultiplier;
					}
				}
			}
		}
	}
}

//======================================================================================================================
bool FPhysicsControlComponentImpl::ApplyControlStrengths(
	FPhysicsControlRecord& Record, FConstraintInstance* ConstraintInstance)
{
	const FPhysicsControlData& Data = Record.PhysicsControl.ControlData;
	const FPhysicsControlMultipliers& Multipliers = Record.PhysicsControl.ControlMultipliers;

	double AngularSpring;
	double AngularDamping;
	double MaxTorque = Data.MaxTorque * Multipliers.MaxTorqueMultiplier;

	FVector LinearSpring;
	FVector LinearDamping;
	FVector MaxForce = Data.MaxForce * Multipliers.MaxForceMultiplier;

	ConvertSpringParams(
		AngularSpring, AngularDamping,
		Data.AngularStrength * Multipliers.AngularStrengthMultiplier,
		Data.AngularDampingRatio,
		Data.AngularExtraDamping * Multipliers.AngularExtraDampingMultiplier);
	ConvertSpringParams(
		LinearSpring, LinearDamping,
		Data.LinearStrength * Multipliers.LinearStrengthMultiplier,
		Data.LinearDampingRatio,
		Data.LinearExtraDamping * Multipliers.LinearExtraDampingMultiplier);

	if (Multipliers.MaxTorqueMultiplier <= 0.0)
	{
		AngularSpring = 0.0;
		AngularDamping = 0.0;
	}
	if (Multipliers.MaxForceMultiplier.X <= 0.0)
	{
		LinearSpring.X = 0.0;
		LinearDamping.X = 0.0;
	}
	if (Multipliers.MaxForceMultiplier.Y <= 0.0)
	{
		LinearSpring.Y = 0.0;
		LinearDamping.Y = 0.0;
	}
	if (Multipliers.MaxForceMultiplier.Z <= 0.0)
	{
		LinearSpring.Z = 0.0;
		LinearDamping.Z = 0.0;
	}

	ConstraintInstance->SetAngularDriveParams(AngularSpring, AngularDamping, MaxTorque);
	ConstraintInstance->SetLinearDriveParams(LinearSpring, LinearDamping, MaxForce);

	double TestAngular = (AngularSpring + AngularDamping) * FMath::Max(UE_SMALL_NUMBER, MaxTorque);
	FVector TestLinear = (LinearSpring + LinearDamping) * FVector(
		FMath::Max(UE_SMALL_NUMBER, MaxForce.X),
		FMath::Max(UE_SMALL_NUMBER, MaxForce.Y),
		FMath::Max(UE_SMALL_NUMBER, MaxForce.Z));
	double Test = TestAngular + TestLinear.GetMax();
	return Test > 0.0;
}

//======================================================================================================================
void FPhysicsControlComponentImpl::ApplyControl(FPhysicsControlRecord& Record)
{
	FConstraintInstance* ConstraintInstance = Record.PhysicsControlState.ConstraintInstance.Get();

	if (!ConstraintInstance || !Record.PhysicsControlState.bEnabled)
	{
		return;
	}

	FBodyInstance* ParentBodyInstance = GetBodyInstance(
		Record.PhysicsControl.ParentMeshComponent, Record.PhysicsControl.ParentBoneName);

	FBodyInstance* ChildBodyInstance = GetBodyInstance(
		Record.PhysicsControl.ChildMeshComponent, Record.PhysicsControl.ChildBoneName);

	if (!ParentBodyInstance && !ChildBodyInstance)
	{
		return;
	}

	// Set strengths etc
	if (ApplyControlStrengths(Record, ConstraintInstance))
	{
		FTransform TargetTM;
		FVector TargetVelocity;
		FVector TargetAngularVelocity;
		CalculateControlTargetData(TargetTM, TargetVelocity, TargetAngularVelocity, Record, true);

		ConstraintInstance->SetLinearPositionTarget(TargetTM.GetTranslation());
		ConstraintInstance->SetAngularOrientationTarget(TargetTM.GetRotation());
		ConstraintInstance->SetLinearVelocityTarget(TargetVelocity);
		ConstraintInstance->SetAngularVelocityTarget(TargetAngularVelocity / UE_TWO_PI); // In rev/sec

		if (ParentBodyInstance)
		{
			ParentBodyInstance->WakeInstance();
		}
		if (ChildBodyInstance)
		{
			ChildBodyInstance->WakeInstance();
		}
	}
}

//======================================================================================================================
FPhysicsBodyModifier* FPhysicsControlComponentImpl::FindBodyModifier(const FName Name)
{
	if (PhysicsBodyModifiers.IsEmpty())
	{
		return nullptr;
	}
	if (Name.IsNone())
	{
		return &PhysicsBodyModifiers.CreateIterator().Value();
	}
	if (FPhysicsBodyModifier* Record = PhysicsBodyModifiers.Find(Name))
	{
		return Record;
	}
	return nullptr;
}

