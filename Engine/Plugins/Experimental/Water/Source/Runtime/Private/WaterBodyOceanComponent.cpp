// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyOceanComponent.h"
#include "WaterSplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "OceanCollisionComponent.h"
#include "WaterBodyOceanActor.h"
#include "WaterBooleanUtils.h"
#include "WaterSubsystem.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

UWaterBodyOceanComponent::UWaterBodyOceanComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CollisionExtents = FVector(50000.f, 50000.f, 10000.f);

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(IsFlatSurface());
	check(IsWaterSplineClosedLoop());
	check(IsHeightOffsetSupported());
}

TArray<UPrimitiveComponent*> UWaterBodyOceanComponent::GetCollisionComponents() const
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

void UWaterBodyOceanComponent::SetHeightOffset(float InHeightOffset)
{
	const float ClampedHeightOffset = FMath::Max(0.0f, InHeightOffset);

	if (HeightOffset != ClampedHeightOffset)
	{
		HeightOffset = ClampedHeightOffset;

		// the physics volume needs to be adjusted : 
		OnWaterBodyChanged(true);
	}
}

void UWaterBodyOceanComponent::BeginUpdateWaterBody()
{
	Super::BeginUpdateWaterBody();

	// Update WaterSubsystem's OceanActor
	if (UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
	{
		WaterSubsystem->SetOceanBodyComponent(this);
	}
}

#if WITH_EDITOR
void UWaterBodyOceanComponent::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	Super::OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UWaterBodyOceanComponent, CollisionExtents))
	{
		// Affects the physics shape
		bShapeOrPositionChanged = true;
	}
}
#endif

void UWaterBodyOceanComponent::Reset()
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

void UWaterBodyOceanComponent::OnUpdateBody(bool bWithExclusionVolumes)
{
	AActor* OwnerActor = GetOwner();
	check(OwnerActor);

	if (bGenerateCollisions)
	{
		FVector OceanCollisionExtents = GetCollisionExtents();
		OceanCollisionExtents.Z += CollisionHeightOffset / 2;

		// The volume's top is located on the ocean actor's height + the additional ocean level + the collision height offset: 
		// and the volume's bottom is deeper by a value == OceanCollisionExtents.Z :
		FVector OceanBoxLocation = FVector(0, 0, GetHeightOffset() - OceanCollisionExtents.Z + CollisionHeightOffset);
		// No matter the scale, OceanCollisionExtents is always specified in world-space : 
		FVector OceanBoxExtent = OceanCollisionExtents; 

		// get our box information and exclusion volumes
		FTransform ComponentTransform = GetComponentTransform();
		FBoxSphereBounds WorldBounds;
		WorldBounds.Origin = ComponentTransform.TransformPositionNoScale(OceanBoxLocation);
		WorldBounds.BoxExtent = OceanBoxExtent;
		TArray<AWaterBodyExclusionVolume*> Exclusions = bWithExclusionVolumes ? GetExclusionVolumes() : TArray<AWaterBodyExclusionVolume*>();

		// Calculate a set of boxes and meshes that are Difference(Box, Union(ExclusionVolumes))
		// Output is calculated in World space and then transformed into Actor space, ie by inverse of ActorTransform
		TArray<FBoxSphereBounds> Boxes;
		TArray<TArray<FKConvexElem>> ConvexSets;
		double WorldMeshBufferWidth = 1000.0;		// extra space left around exclusion meshes
		double WorldBoxOverlap = 10.0;				// output boxes overlap each other and meshes by this amount
		FWaterBooleanUtils::BuildOceanCollisionComponents(WorldBounds, ComponentTransform, Exclusions,
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
			// We want a deterministic name within this water body component's outer to avoid non-deterministic cook issues but we also want to avoid reusing a component that might have been deleted
			//  prior to that (in order to avoid potentially stalls caused by the primitive component not having been FinishDestroy-ed) (because OnUpdateBody runs 2 times in a row, 
			//  once with bWithExclusionVolumes == false, once with bWithExclusionVolumes == true) so we use MakeUniqueObjectName for the name here :
			FName Name = MakeUniqueObjectName(OwnerActor, UOceanCollisionComponent::StaticClass(), *FString::Printf(TEXT("OceanCollisionBoxComponent_%d"), i));
			UOceanBoxCollisionComponent* BoxComponent = nullptr;
			if (CollisionBoxes.IsValidIndex(i) && (CollisionBoxes[i] != nullptr))
			{
				BoxComponent = CollisionBoxes[i];
			}
			else
			{
				BoxComponent = NewObject<UOceanBoxCollisionComponent>(OwnerActor, Name, RF_Transactional);
				BoxComponent->SetupAttachment(this);
				CollisionBoxes.Add(BoxComponent);
			}

			if (!BoxComponent->IsRegistered())
			{
				BoxComponent->RegisterComponent();
			}
			BoxComponent->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			BoxComponent->bDrawOnlyIfSelected = true;
			BoxComponent->SetRelativeLocation(FVector::ZeroVector);

			BoxComponent->bFillCollisionUnderneathForNavmesh = bFillCollisionUnderWaterBodiesForNavmesh;
			BoxComponent->SetCollisionProfileName(GetCollisionProfileName());
			BoxComponent->SetGenerateOverlapEvents(true);

			FVector RelativePosition = Box.Origin;			// boxes are calculated in space of actor
			BoxComponent->SetRelativeLocation(RelativePosition);
			BoxComponent->SetBoxExtent(Box.BoxExtent);

		}

		// create the convex-hull components
		for (int32 i = 0; i < ConvexSets.Num(); ++i)
		{
			const TArray<FKConvexElem>& ConvexSet = ConvexSets[i];
			// We want a deterministic name within this water body component's outer to avoid non-deterministic cook issues but we also want to avoid reusing a component that might have been deleted
			//  prior to that (in order to avoid potentially stalls caused by the primitive component not having been FinishDestroy-ed) (because OnUpdateBody runs 2 times in a row, 
			//  once with bWithExclusionVolumes == false, once with bWithExclusionVolumes == true) so we use MakeUniqueObjectName for the name here :
			FName Name = MakeUniqueObjectName(OwnerActor, UOceanCollisionComponent::StaticClass(), *FString::Printf(TEXT("OceanCollisionComponent_%d"), i));
			UOceanCollisionComponent* CollisionComponent = nullptr;
			if (CollisionHullSets.IsValidIndex(i) && (CollisionHullSets[i] != nullptr))
			{
				CollisionComponent = CollisionHullSets[i];
			}
			else
			{
				CollisionComponent = NewObject<UOceanCollisionComponent>(OwnerActor, Name, RF_Transactional);
				CollisionComponent->SetupAttachment(this);
				CollisionHullSets.Add(CollisionComponent);
			}

			if (!CollisionComponent->IsRegistered())
			{
				CollisionComponent->RegisterComponent();
			}
			CollisionComponent->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			CollisionComponent->SetRelativeLocation(FVector::ZeroVector);

			CollisionComponent->bFillCollisionUnderneathForNavmesh = bFillCollisionUnderWaterBodiesForNavmesh;
			CollisionComponent->SetCollisionProfileName(GetCollisionProfileName());
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