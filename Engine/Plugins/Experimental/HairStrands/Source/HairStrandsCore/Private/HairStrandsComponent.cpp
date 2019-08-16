// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#include "HairStrandsComponent.h"
#include "Engine/CollisionProfile.h"
#include "PrimitiveSceneProxy.h"

UHairStrandsComponent::UHairStrandsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	bAutoActivate = true;
	HairDensity = 1;
	MergeThreshold = 0.1f;
	bUsedForReference = false;

	SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
}

FPrimitiveSceneProxy* UHairStrandsComponent::CreateSceneProxy()
{
	return nullptr;
}

FBoxSphereBounds UHairStrandsComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox HairBox(ForceInit);
	if (HairStrandsAsset)
	{
		for (FVector& PointPosition : HairStrandsAsset->StrandsDatas.StrandsPoints.PointsPosition)
		{
			HairBox += PointPosition;
		}
		HairBox = HairBox.TransformBy(LocalToWorld);
		return FBoxSphereBounds(HairBox);
	}
	else
	{
		return FBoxSphereBounds();
	}
}

int32 UHairStrandsComponent::GetNumMaterials() const
{
	return 1;
}

void UHairStrandsComponent::PostLoad()
{
	Super::PostLoad();
}

void UHairStrandsComponent::OnRegister()
{
	Super::OnRegister();
}

void UHairStrandsComponent::OnUnregister()
{
	Super::OnUnregister();
}

void UHairStrandsComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}