// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyOceanActor.h"
#include "WaterSplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "OceanCollisionComponent.h"
#include "WaterBooleanUtils.h"
#include "WaterSubsystem.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

AWaterBodyOcean::AWaterBodyOcean(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WaterBodyType = GetWaterBodyType();
	CollisionExtents = FVector(50000.f, 50000.f, 10000.f);

#if WITH_EDITOR
	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterBodyOceanSprite"));
#endif

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(IsFlatSurface());
	check(IsWaterSplineClosedLoop());
	check(IsHeightOffsetSupported());
}

void AWaterBodyOcean::InitializeBody()
{
	if (!OceanGenerator)
	{
		OceanGenerator = NewObject<UOceanGenerator>(this, TEXT("OceanGenerator"));
	}
}

TArray<UPrimitiveComponent*> AWaterBodyOcean::GetCollisionComponents() const
{
	if (OceanGenerator)
	{
		return OceanGenerator->GetCollisionComponents();
	}
	return Super::GetCollisionComponents();
}

void AWaterBodyOcean::SetHeightOffset(float InHeightOffset)
{
	// Currently only used by ocean
	const float ClampedHeightOffset = FMath::Max(0.0f, InHeightOffset);

	if (HeightOffset != ClampedHeightOffset)
	{
		HeightOffset = ClampedHeightOffset;

		// the physics volume needs to be adjusted : 
		OnWaterBodyChanged(true);
	}
}

void AWaterBodyOcean::BeginUpdateWaterBody()
{
	Super::BeginUpdateWaterBody();

	// Update WaterSubsustem's OceanActor
	if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
	{
		WaterSubsystem->SetOceanActor(this);
	}
}

void AWaterBodyOcean::UpdateWaterBody(bool bWithExclusionVolumes)
{
	if (OceanGenerator)
	{
		OceanGenerator->UpdateBody(bWithExclusionVolumes);
	}
}

#if WITH_EDITOR
void AWaterBodyOcean::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	Super::OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterBodyOcean, CollisionExtents))
	{
		// Affects the physics shape
		bShapeOrPositionChanged = true;
	}
}
#endif

// ----------------------------------------------------------------------------------

UOceanGenerator::UOceanGenerator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOceanGenerator::Reset()
{
	for (UBoxComponent* Component : CollisionBoxes)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	CollisionBoxes.Reset();
	for (UOceanCollisionComponent* Component : CollisionHullSets)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	CollisionHullSets.Reset();
}

void UOceanGenerator::OnUpdateBody(bool bWithExclusionVolumes)
{
	AWaterBody* OwnerBody = GetOuterAWaterBody();
	if (OwnerBody->bGenerateCollisions)
	{
		// Fake ocean depth for now
		FVector OceanCollisionExtents = OwnerBody->GetCollisionExtents();
		OceanCollisionExtents.Z = 10000.f;

		// The volume's top is located on the ocean actor's height + the additional ocean level : 
		// and the volume's bottom is deeper by a value == OceanCollisionExtents.Z :
		FVector OceanBoxLocation = FVector(0, 0, OwnerBody->GetHeightOffset() - OceanCollisionExtents.Z);
		// No matter the scale, OceanCollisionExtents is always specified in world-space : 
		FVector OceanBoxExtent = OceanCollisionExtents; 

		// get our box information and exclusion volumes
		FTransform ActorTransform = OwnerBody->GetActorTransform();
		FBoxSphereBounds WorldBounds;
		WorldBounds.Origin = ActorTransform.TransformPositionNoScale(OceanBoxLocation);
		WorldBounds.BoxExtent = OceanBoxExtent;
		TArray<AWaterBodyExclusionVolume*> Exclusions = bWithExclusionVolumes ? OwnerBody->GetExclusionVolumes() : TArray<AWaterBodyExclusionVolume*>();

		// Calculate a set of boxes and meshes that are Difference(Box, Union(ExclusionVolumes))
		// Output is calculated in World space and then transformed into Actor space, ie by inverse of ActorTransform
		TArray<FBoxSphereBounds> Boxes;
		TArray<TArray<FKConvexElem>> ConvexSets;
		double WorldMeshBufferWidth = 1000.0;		// extra space left around exclusion meshes
		double WorldBoxOverlap = 10.0;				// output boxes overlap each other and meshes by this amount
		UE::WaterUtils::BuildOceanCollisionComponents(WorldBounds, ActorTransform, Exclusions,
			Boxes, ConvexSets, WorldMeshBufferWidth, WorldBoxOverlap);

		// Don't delete components unless we have to : this generates determinism issues because UOceanCollisionComponent has a UBodySetup with a GUID :
		if ((CollisionBoxes.Num() != Boxes.Num()) || (CollisionHullSets.Num() != ConvexSets.Num()))
		{
			Reset();
		}

		// create the box components
		for (int32 i = 0; i < Boxes.Num(); ++i)
		{
			const FBoxSphereBounds& Box = Boxes[i];
			FString Name = FString::Printf(TEXT("OceanCollisionBoxComponent_%d"), i);
			UOceanBoxCollisionComponent* BoxComponent = nullptr;
			if (CollisionBoxes.IsValidIndex(i) && (CollisionBoxes[i] != nullptr))
			{
				BoxComponent = CollisionBoxes[i];
			}
			else
			{
				BoxComponent = NewObject<UOceanBoxCollisionComponent>(OwnerBody, *Name);
				BoxComponent->SetupAttachment(OwnerBody->GetRootComponent());
				CollisionBoxes.Add(BoxComponent);
			}

			if (!BoxComponent->IsRegistered())
			{
				BoxComponent->RegisterComponent();
			}
			BoxComponent->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			BoxComponent->bDrawOnlyIfSelected = true;
			BoxComponent->SetRelativeLocation(FVector::ZeroVector);

			BoxComponent->bFillCollisionUnderneathForNavmesh = OwnerBody->bFillCollisionUnderWaterBodiesForNavmesh;
			BoxComponent->SetCollisionProfileName(OwnerBody->GetCollisionProfileName());
			BoxComponent->SetGenerateOverlapEvents(true);

			FVector RelativePosition = Box.Origin;			// boxes are calculated in space of actor
			BoxComponent->SetRelativeLocation(RelativePosition);
			BoxComponent->SetBoxExtent(Box.BoxExtent);

		}

		// create the convex-hull components
		for (int32 i = 0; i < ConvexSets.Num(); ++i)
		{
			const TArray<FKConvexElem>& ConvexSet = ConvexSets[i];
			FString Name = FString::Printf(TEXT("OceanCollisionComponent_%d"), i);
			UOceanCollisionComponent* CollisionComponent = nullptr;
			if (CollisionHullSets.IsValidIndex(i) && (CollisionHullSets[i] != nullptr))
			{
				CollisionComponent = CollisionHullSets[i];
			}
			else
			{
				CollisionComponent = NewObject<UOceanCollisionComponent>(OwnerBody, *Name);
				CollisionComponent->SetupAttachment(OwnerBody->GetRootComponent());
				CollisionHullSets.Add(CollisionComponent);
			}

			if (!CollisionComponent->IsRegistered())
			{
				CollisionComponent->RegisterComponent();
			}
			CollisionComponent->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			CollisionComponent->SetRelativeLocation(FVector::ZeroVector);

			CollisionComponent->bFillCollisionUnderneathForNavmesh = OwnerBody->bFillCollisionUnderWaterBodiesForNavmesh;
			CollisionComponent->SetCollisionProfileName(OwnerBody->GetCollisionProfileName());
			CollisionComponent->SetGenerateOverlapEvents(true);

			CollisionComponent->InitializeFromConvexElements(ConvexSet);
		}
	}
	else
	{
		// clear existing
		Reset();
	}
}

TArray<UPrimitiveComponent*> UOceanGenerator::GetCollisionComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(CollisionBoxes.Num() + CollisionHullSets.Num());

	for (UOceanBoxCollisionComponent* Comp : CollisionBoxes)
	{
		Result.Add(Comp);
	}

	for (UOceanCollisionComponent* Comp : CollisionHullSets)
	{
		Result.Add(Comp);
	}
	return Result;
}