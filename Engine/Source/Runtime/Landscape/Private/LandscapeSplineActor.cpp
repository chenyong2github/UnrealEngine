// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeSplineActor.h"
#include "LandscapeInfo.h"
#include "LandscapeInfoMap.h"
#include "Landscape.h"
#include "LandscapeSplinesComponent.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/Landscape/LandscapeSplineActorDesc.h"
#endif

ALandscapeSplineActor::ALandscapeSplineActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ULandscapeSplinesComponent* SplineComponent = CreateDefaultSubobject<ULandscapeSplinesComponent>(TEXT("RootComponent0"));
	
	RootComponent = SplineComponent;	
	RootComponent->Mobility = EComponentMobility::Static;
}

ULandscapeSplinesComponent* ALandscapeSplineActor::GetSplinesComponent() const
{
	return Cast<ULandscapeSplinesComponent>(RootComponent);
}

FTransform ALandscapeSplineActor::LandscapeActorToWorld() const
{
	return GetLandscapeInfo()->LandscapeActor->LandscapeActorToWorld();
}

ULandscapeInfo* ALandscapeSplineActor::GetLandscapeInfo() const
{
	return ULandscapeInfo::Find(GetWorld(), LandscapeGuid);
}

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc> ALandscapeSplineActor::CreateClassActorDesc() const
{
	return TUniquePtr<FWorldPartitionActorDesc>(new FLandscapeSplineActorDesc());
}

void ALandscapeSplineActor::GetSharedProperties(ULandscapeInfo* InLandscapeInfo)
{
	Modify();
	LandscapeGuid = InLandscapeInfo->LandscapeGuid;
}

void ALandscapeSplineActor::Destroyed()
{
	Super::Destroyed();

	UWorld* World = GetWorld();

	if (GIsEditor && !World->IsGameWorld())
	{
		// Modify Splines Objects to support Undo/Redo
		GetSplinesComponent()->ModifySplines();
	}
}

void ALandscapeSplineActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!IsPendingKillPending())
	{
		if (LandscapeGuid.IsValid())
		{
			ULandscapeInfo* LandscapeInfo = ULandscapeInfo::FindOrCreate(GetWorld(), LandscapeGuid);
			LandscapeInfo->RegisterSplineActor(this);
		}
	}
}

void ALandscapeSplineActor::UnregisterAllComponents(bool bForReregister)
{
	if (GetWorld() && !GetWorld()->IsPendingKillOrUnreachable() && LandscapeGuid.IsValid())
	{
		if (ULandscapeInfo* LandscapeInfo = GetLandscapeInfo())
		{
			LandscapeInfo->UnregisterSplineActor(this);
		}
	}

	Super::UnregisterAllComponents(bForReregister);
}

void ALandscapeSplineActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (bFinished)
	{
		GetLandscapeInfo()->RequestSplineLayerUpdate();
	}
}

#endif