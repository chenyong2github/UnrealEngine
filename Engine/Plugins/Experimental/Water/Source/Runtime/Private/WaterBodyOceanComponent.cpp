// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyOceanComponent.h"
#include "WaterModule.h"
#include "WaterSplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "OceanCollisionComponent.h"
#include "WaterBodyOceanActor.h"
#include "WaterBooleanUtils.h"
#include "WaterSubsystem.h"
#include "WaterZoneActor.h"
#include "UObject/UObjectIterator.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "Polygon2.h"
#include "ConstrainedDelaunay2.h"
#include "Operations/InsetMeshRegion.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "Algo/Transform.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

UWaterBodyOceanComponent::UWaterBodyOceanComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CollisionExtents = FVector(50000.f, 50000.f, 10000.f);
	VisualExtents = FVector2D(150000.f, 150000.f);

	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(IsFlatSurface());
	check(IsWaterSplineClosedLoop());
	check(IsHeightOffsetSupported());
}

TArray<UPrimitiveComponent*> UWaterBodyOceanComponent::GetCollisionComponents(bool bInOnlyEnabledComponents) const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(CollisionBoxes.Num() + CollisionHullSets.Num());

	Algo::TransformIf(CollisionBoxes, Result,
		[bInOnlyEnabledComponents](UOceanBoxCollisionComponent* Comp) { return ((Comp != nullptr) && (!bInOnlyEnabledComponents || (Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision))); },
		[](UOceanBoxCollisionComponent* Comp) { return Comp; });

	Algo::TransformIf(CollisionHullSets, Result,
		[bInOnlyEnabledComponents](UOceanCollisionComponent* Comp) { return ((Comp != nullptr) && (!bInOnlyEnabledComponents || (Comp->GetCollisionEnabled() != ECollisionEnabled::NoCollision))); },
		[](UOceanCollisionComponent* Comp) { return Comp; });

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

void UWaterBodyOceanComponent::SetVisualExtents(FVector2D NewExtents)
{
	if (VisualExtents != NewExtents)
	{
		VisualExtents = NewExtents;
		UpdateWaterBodyRenderData();
		Modify();
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
	else if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UWaterBodyOceanComponent, VisualExtents))
	{
		bShapeOrPositionChanged = true;
	}
}
const TCHAR* UWaterBodyOceanComponent::GetWaterSpriteTextureName() const
{
	return TEXT("/Water/Icons/WaterBodyOceanSprite");
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

void UWaterBodyOceanComponent::GenerateWaterBodyMesh()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateOceanMesh);

	using namespace UE::Geometry;

	WaterBodyMeshVertices.Empty();
	WaterBodyMeshIndices.Empty();

	const UWaterSplineComponent* SplineComp = GetWaterSpline();
	
	if (SplineComp->GetNumberOfSplineSegments() < 3)
	{
		return;
	}

	FPolygon2d Island;
	TArray<FVector> PolyLineVertices;
	SplineComp->ConvertSplineToPolyLine(ESplineCoordinateSpace::Local, FMath::Square(10.f), PolyLineVertices);
	
	// Construct a 2D polygon describing the central island
	for (int32 i = PolyLineVertices.Num() - 2; i >= 0; --i) // skip the last vertex since it's the same as the first vertex
	{
		Island.AppendVertex(FVector2D(PolyLineVertices[i]));
	}

	FVector OceanLocation = GetComponentLocation();
	FPolygon2d OceanBoundingPolygon = FPolygon2d::MakeRectangle(FVector2d(0, 0), VisualExtents.X, VisualExtents.Y);

	FConstrainedDelaunay2d Triangulation;
	Triangulation.FillRule = FConstrainedDelaunay2d::EFillRule::Positive;
	Triangulation.Add(OceanBoundingPolygon);
	if (!Island.IsClockwise())
	{
		Island.Reverse();
	}
	Triangulation.Add(Island);
	Triangulation.Triangulate();

	if (Triangulation.Triangles.Num() == 0)
	{
		return;
	}

	// This FDynamicMesh3 will only be used to compute the inset region for shape dilation
	FDynamicMesh3 OceanMesh(EMeshComponents::None);
	for (const FVector2d& Vertex : Triangulation.Vertices)
	{
		// push the set of undilated vertices to the persistent mesh
		FDynamicMeshVertex MeshVertex(FVector3f(Vertex.X, Vertex.Y, 0.f));
		MeshVertex.Color = FColor::Black;
		MeshVertex.TextureCoordinate[0].X = WaterBodyIndex;
		WaterBodyMeshVertices.Add(MeshVertex);

		OceanMesh.AppendVertex(FVector3d(Vertex, 0.0));
	}

	for (const FIndex3i& Triangle : Triangulation.Triangles)
	{
		WaterBodyMeshIndices.Append({ (uint32)Triangle.A, (uint32)Triangle.B, (uint32)Triangle.C });
		OceanMesh.AppendTriangle(Triangle);
	}


	if (ShapeDilation > 0.f)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DilateOceanMesh);

		// Inset the mesh by -ShapeDilation to effectively expand the mesh
		FInsetMeshRegion Inset(&OceanMesh);
		Inset.InsetDistance = -1 * ShapeDilation / 2.f;

		Inset.Triangles.Reserve(OceanMesh.TriangleCount());
		for (int32 Idx : OceanMesh.TriangleIndicesItr())
		{
			Inset.Triangles.Add(Idx);
		}
		
		if (Inset.Apply())
		{
			for (const FVector3d& Vertex : OceanMesh.GetVerticesBuffer())
			{
				// push the set of dilated vertices to the persistent mesh
				FDynamicMeshVertex MeshVertex(FVector3f(Vertex.X, Vertex.Y, 0.f));
				MeshVertex.Color = FColor::Black;
				DilatedWaterBodyMeshVertices.Add(MeshVertex);
			}

			for (const FIndex3i& Triangle : OceanMesh.GetTrianglesBuffer())
			{
				DilatedWaterBodyMeshIndices.Append({ (uint32)Triangle.A, (uint32)Triangle.B, (uint32)Triangle.C });
			}
		}
		else
		{
			UE_LOG(LogWater, Warning, TEXT("Failed to apply mesh inset for shape dilation (%s"), *GetOwner()->GetActorNameOrLabel());
		}
	}	
}

void UWaterBodyOceanComponent::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::WaterZonesRefactor)
	{
		if (AWaterZone* WaterZone = GetWaterZone())
		{
			SetVisualExtents(WaterZone->GetZoneExtent());
		}
	}
}

FBoxSphereBounds UWaterBodyOceanComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FVector Min(FVector(-VisualExtents / 2.f, -1 * GetChannelDepth()));
	FVector Max(FVector(VisualExtents / 2.f, 0.f));
	return FBoxSphereBounds(FBox(Min, Max)).TransformBy(LocalToWorld);
}

void UWaterBodyOceanComponent::OnUpdateBody(bool bWithExclusionVolumes)
{
	AActor* OwnerActor = GetOwner();
	check(OwnerActor);

	if (GetCollisionEnabled() != ECollisionEnabled::NoCollision)
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

			CopySharedCollisionSettingsToComponent(BoxComponent);
			CopySharedNavigationSettingsToComponent(BoxComponent);

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

			CopySharedCollisionSettingsToComponent(CollisionComponent);
			CopySharedNavigationSettingsToComponent(CollisionComponent);

			CollisionComponent->InitializeFromConvexElements(ConvexSet);
		}
	}
	else
	{
		// clear existing
		Reset();
	}
}