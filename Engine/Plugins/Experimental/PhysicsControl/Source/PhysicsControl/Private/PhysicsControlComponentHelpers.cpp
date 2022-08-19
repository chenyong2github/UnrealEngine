// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsControlComponentHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

//======================================================================================================================
void ConvertSpringParams(
	double& OutSpring, double& OutDamping, 
	double InStrength, double InDampingRatio, double InExtraDamping)
{
	double AngularFrequency = InStrength * 2.0 * PI;
	double Stiffness = AngularFrequency * AngularFrequency;

	OutSpring = Stiffness;
	OutDamping = 2.0 * InDampingRatio * AngularFrequency;
	OutDamping += InExtraDamping;
}

//======================================================================================================================
void ConvertSpringParams(
	FVector& OutSpring, FVector& OutDamping, 
	const FVector& InStrength, float InDampingRatio, const FVector& InExtraDamping)
{
	ConvertSpringParams(OutSpring.X, OutDamping.X, InStrength.X, InDampingRatio, InExtraDamping.X);
	ConvertSpringParams(OutSpring.Y, OutDamping.Y, InStrength.Y, InDampingRatio, InExtraDamping.Y);
	ConvertSpringParams(OutSpring.Z, OutDamping.Z, InStrength.Z, InDampingRatio, InExtraDamping.Z);
}

//======================================================================================================================
FBodyInstance* GetBodyInstance(UMeshComponent* MeshComponent, const FName BoneName)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent);
	if (StaticMeshComponent)
	{
		return StaticMeshComponent->GetBodyInstance();
	}
	USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent);
	if (SkeletalMeshComponent)
	{
		return SkeletalMeshComponent->GetBodyInstance(BoneName);
	}
	return nullptr;
}

//======================================================================================================================
FName GetPhysicalParentBone(USkeletalMeshComponent* SkeletalMeshComponent, FName BoneName)
{
	while (true)
	{
		FName ParentBoneName = SkeletalMeshComponent->GetParentBone(BoneName);
		if (ParentBoneName.IsNone() || ParentBoneName == BoneName)
		{
			return FName();
		}
		const FBodyInstance* ParentBodyInstance = GetBodyInstance(SkeletalMeshComponent, ParentBoneName);
		if (ParentBodyInstance)
		{
			return ParentBoneName;
		}
		BoneName = ParentBoneName;
	}
	return FName();
}

