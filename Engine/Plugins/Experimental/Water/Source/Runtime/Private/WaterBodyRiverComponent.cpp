// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterBodyRiverComponent.h"
#include "WaterSplineComponent.h"
#include "WaterSubsystem.h"
#include "WaterUtils.h"
#include "Algo/ForEach.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Algo/Transform.h"

// for working around Chaos issue
#include "Chaos/Particles.h"
#include "Chaos/Convex.h"

#if WITH_EDITOR
#include "WaterIconHelper.h"
#endif

// ----------------------------------------------------------------------------------

UWaterBodyRiverComponent::UWaterBodyRiverComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// @todo_water : Remove these checks (Once AWaterBody is no more Blueprintable, these methods should become PURE_VIRTUAL and this class should overload them)
	check(!IsFlatSurface());
	check(!IsWaterSplineClosedLoop());
	check(!IsHeightOffsetSupported());
}


TArray<UPrimitiveComponent*> UWaterBodyRiverComponent::GetCollisionComponents(bool bInOnlyEnabledComponents) const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(SplineMeshComponents.Num());

	Algo::TransformIf(SplineMeshComponents, Result, 
		[bInOnlyEnabledComponents](USplineMeshComponent* SplineComp) { return ((SplineComp != nullptr) && (!bInOnlyEnabledComponents || (SplineComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision))); }, 
		[](USplineMeshComponent* SplineComp) { return SplineComp; });

	return Result;
}

TArray<UPrimitiveComponent*> UWaterBodyRiverComponent::GetStandardRenderableComponents() const
{
	TArray<UPrimitiveComponent*> Result;
	Result.Reserve(SplineMeshComponents.Num());

	Algo::TransformIf(SplineMeshComponents, Result, 
		[](USplineMeshComponent* SplineComp) { return (SplineComp != nullptr); }, 
		[](USplineMeshComponent* SplineComp) { return SplineComp; });

	return Result;
}

void UWaterBodyRiverComponent::OnUpdateBody(bool bWithExclusionVolumes)
{
	if (const USplineComponent* WaterSpline = GetWaterSpline())
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();
		const int32 NumberOfMeshComponentsNeeded = WaterSpline->IsClosedLoop() ? NumSplinePoints : NumSplinePoints - 1;
		if (SplineMeshComponents.Num() != NumberOfMeshComponentsNeeded)
		{
			GenerateMeshes();
		}
		else
		{
			for (int32 SplinePtIndex = 0; SplinePtIndex < NumberOfMeshComponentsNeeded; ++SplinePtIndex)
			{
				USplineMeshComponent* MeshComp = SplineMeshComponents[SplinePtIndex];

				// Validate all mesh components.  Blueprints can null these out somehow
				if (MeshComp)
				{
					UpdateSplineMesh(MeshComp, SplinePtIndex);
					MeshComp->MarkRenderStateDirty();
				}
				else
				{
					GenerateMeshes();
					break;
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------------

static FColor PackWaterFlow(float VelocityMagnitude, float DirectionAngle)
{
	check((DirectionAngle >= 0.f) && (DirectionAngle <= TWO_PI));

	const float MaxVelocity = FWaterUtils::GetWaterMaxFlowVelocity(false);

	float NormalizedMagnitude = FMath::Clamp(VelocityMagnitude, 0.f, MaxVelocity) / MaxVelocity;
	float NormalizedAngle = DirectionAngle / TWO_PI;
	float MappedMagnitude = NormalizedMagnitude * TNumericLimits<uint16>::Max();
	float MappedAngle = NormalizedAngle * TNumericLimits<uint16>::Max();

	uint16 ResultMag = (uint16)MappedMagnitude;
	uint16 ResultAngle = (uint16)MappedAngle;
	
	FColor Result;
	Result.R = (ResultMag >> 8) & 0xFF;
	Result.G = (ResultMag >> 0) & 0xFF;
	Result.B = (ResultAngle >> 8) & 0xFF;
	Result.A = (ResultAngle >> 0) & 0xFF;

	return Result;
}

static void AddVerticesForRiverSplineStep(
	float DistanceAlongSpline, 
	const UWaterBodyRiverComponent* Component, 
	const UWaterSplineComponent* SplineComp, 
	const UWaterSplineMetadata* WaterSplineMetadata,
	TArray<FDynamicMeshVertex>& Vertices, 
	TArray<uint32>& Indices, 
	TArray<FDynamicMeshVertex>& DilatedVertices, 
	TArray<uint32>& DilatedIndices)
{
	check((Component != nullptr) && (SplineComp != nullptr) && (WaterSplineMetadata != nullptr));
	
	const FVector Tangent = SplineComp->GetTangentAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();
	const FVector Up = SplineComp->GetUpVectorAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();

	const FVector Normal = FVector::CrossProduct(Tangent, Up).GetSafeNormal();
	const FVector Pos = SplineComp->GetLocationAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local);

	const float Key = SplineComp->SplineCurves.ReparamTable.Eval(DistanceAlongSpline, 0.f);
	const float HalfWidth = WaterSplineMetadata->RiverWidth.Eval(Key) / 2;
	float Velocity = WaterSplineMetadata->WaterVelocityScalar.Eval(Key);

	// Distance from the center of the spline to place our first vertices
	FVector OutwardDistance = Normal * HalfWidth;
	// Prevent there being a relative height difference between the two vertices even when the spline has a slight roll to it
	OutwardDistance.Z = 0.f;

	const float DilationAmount = Component->ShapeDilation;
	const FVector DilationOffset = Normal * DilationAmount;

	FDynamicMeshVertex Left(FVector3f(Pos - OutwardDistance));
	FDynamicMeshVertex Right(FVector3f(Pos + OutwardDistance));

	FDynamicMeshVertex DilatedFarLeft(FVector3f(Pos - OutwardDistance - DilationOffset));
	FDynamicMeshVertex DilatedLeft(FVector3f(Pos - OutwardDistance));
	FDynamicMeshVertex DilatedRight(FVector3f(Pos + OutwardDistance));
	FDynamicMeshVertex DilatedFarRight(FVector3f(Pos + OutwardDistance + DilationOffset));

	float FlowDirection = Tangent.HeadingAngle() + FMath::DegreesToRadians(Component->GetRelativeRotation().Yaw);

	// Restrict all angles between [0, 2 PI]. UnwindRadians returns a value between [-Pi, Pi] so we must remap again:
	FlowDirection = FMath::UnwindRadians(FlowDirection);
	if (FlowDirection < 0.f)
	{
		FlowDirection += TWO_PI;
	}

	// If negative velocity, inverse the direction and change the velocity back to positive.
	if (Velocity < 0.f)
	{
		Velocity *= -1.f;
		FlowDirection = FMath::Fmod(PI + FlowDirection, TWO_PI);
	}

	const FColor EmptyFlowData = FColor(0.f);

	const FColor PackedFlowData = PackWaterFlow(Velocity, FlowDirection);
	Left.Color = PackedFlowData;
	Right.Color = PackedFlowData;

	DilatedFarLeft.Color = EmptyFlowData;
	DilatedLeft.Color = EmptyFlowData;
	DilatedRight.Color = EmptyFlowData;
	DilatedFarRight.Color = EmptyFlowData;

	{
		/* Non - dilated river segment geometry:
		   2 --- 3
		   |  /  |
		   0 --- 1
		*/
		const uint32 BaseIndex = Vertices.Num();
		Vertices.Append({ Left, Right });
		Indices.Append({ BaseIndex, BaseIndex + 3, BaseIndex + 1, BaseIndex, BaseIndex + 2, BaseIndex + 3 });
	}
	
	if (DilationAmount > 0.f)
	{
		/* Dilated River segment geometry:
			4---5   6---7
			| / |   | / |
			0---1   2---3
		*/
		const uint32 BaseIndex = DilatedVertices.Num();
		DilatedVertices.Append({ DilatedFarLeft, DilatedLeft, DilatedRight, DilatedFarRight });
		// Append left dilation quad
		DilatedIndices.Append({ BaseIndex, BaseIndex + 5, BaseIndex + 1, BaseIndex, BaseIndex + 4, BaseIndex + 5 });
		// Append right dilation quad
		DilatedIndices.Append({ BaseIndex + 2, BaseIndex + 6, BaseIndex + 7, BaseIndex + 2, BaseIndex + 7, BaseIndex + 3 });
	}
}

enum class ERiverBoundaryEdge {
	Start,
	End,
};

static void AddTerminalVerticesForRiverSpline(ERiverBoundaryEdge Edge, const UWaterBodyRiverComponent* Component, const UWaterSplineComponent* SplineComp, const UWaterSplineMetadata* WaterSplineMetadata, TArray<FDynamicMeshVertex>& Vertices, TArray<uint32>& Indices)
{
	check((Component != nullptr) && (SplineComp != nullptr) && (WaterSplineMetadata != nullptr));

	const float DistanceAlongSpline = (Edge == ERiverBoundaryEdge::Start) ? 0.f : SplineComp->GetSplineLength();
	
	const FVector Tangent = SplineComp->GetTangentAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();
	const FVector Up = SplineComp->GetUpVectorAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local).GetSafeNormal();
	
	const FVector Normal = FVector::CrossProduct(Tangent, Up).GetSafeNormal();
	const FVector Pos = SplineComp->GetLocationAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::Local);
	
	const float Key = SplineComp->SplineCurves.ReparamTable.Eval(DistanceAlongSpline, 0.f);
	const float HalfWidth = WaterSplineMetadata->RiverWidth.Eval(Key) / 2;

	const float DilationAmount = Component->ShapeDilation;
	const FVector DilationOffset = Normal * DilationAmount;
	FVector OutwardDistance = Normal * HalfWidth;
	OutwardDistance.Z = 0.f;

	FVector TangentialOffset = Tangent * DilationAmount;
	TangentialOffset.Z = 0.f;

	// For the starting edge the tangential offset is negative to push it backwards
	if (Edge == ERiverBoundaryEdge::Start)
	{
		TangentialOffset *= -1;
	}

	FDynamicMeshVertex BackLeft(FVector3f(Pos - OutwardDistance + TangentialOffset - DilationOffset));
	FDynamicMeshVertex Left(FVector3f(Pos - OutwardDistance + TangentialOffset));
	FDynamicMeshVertex Right(FVector3f(Pos + OutwardDistance + TangentialOffset));
	FDynamicMeshVertex BackRight(FVector3f(Pos + OutwardDistance + TangentialOffset + DilationOffset));

	// Initialize the vertex data to correct represent what we expect the dilated region to look like (no flow data)
	BackLeft.Color = FColor(0.f);
	Left.Color = FColor(0.f);
	Right.Color = FColor(0.f);
	BackRight.Color = FColor(0.f);

	/* Dilated edge segment geometry:
		4---5-----6---7
		| / |  /  | / |
		0---1-----2---3
	*/

	Vertices.Append({ BackLeft, Left, Right, BackRight });

	const uint32 BaseIndex = Edge == ERiverBoundaryEdge::Start ? 0 : Vertices.Num() - 8;

	// Since iterating the spline returns the final distance, all the side quads are already added for the final point so we only need to do this for the first point.
	if (Edge == ERiverBoundaryEdge::Start)
	{ 

		Indices.Append({ BaseIndex + 0, BaseIndex + 5, BaseIndex + 1, BaseIndex + 0, BaseIndex + 4, BaseIndex + 5 });
		Indices.Append({ BaseIndex + 2, BaseIndex + 7, BaseIndex + 3, BaseIndex + 2, BaseIndex + 6, BaseIndex + 7 });
	}
	Indices.Append({ BaseIndex + 1, BaseIndex + 6, BaseIndex + 2, BaseIndex + 1, BaseIndex + 5, BaseIndex + 6 });
}

// ----------------------------------------------------------------------------------

void UWaterBodyRiverComponent::GenerateWaterBodyMesh()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateRiverMesh);

	WaterBodyMeshVertices.Empty();
	WaterBodyMeshIndices.Empty();

	const UWaterSplineComponent* SplineComp = GetWaterSpline();
	if ((SplineComp == nullptr) || (WaterSplineMetadata == nullptr) || (SplineComp->GetNumberOfSplinePoints() < 2))
	{
		return;
	}

	TArray<double> Distances;
	TArray<FVector> Points;
	SplineComp->DivideSplineIntoPolylineRecursiveWithDistances(0.f, SplineComp->GetSplineLength(), ESplineCoordinateSpace::Local, FMath::Square(10.f), Points, Distances);
	if (Distances.Num() == 0)
	{
		return;
	}
	
	TArray<FDynamicMeshVertex> Vertices;
	TArray<uint32> Indices;

	TArray<FDynamicMeshVertex> DilatedVertices;
	TArray<uint32> DilatedIndices;

	// Add an extra point at the start to dilate starting edge
	if (ShapeDilation > 0.f)
	{
		AddTerminalVerticesForRiverSpline(ERiverBoundaryEdge::Start, this, SplineComp, WaterSplineMetadata, DilatedVertices, DilatedIndices);
	}

	for (double DistanceAlongSpline : Distances)
	{
		AddVerticesForRiverSplineStep(DistanceAlongSpline, this, SplineComp, WaterSplineMetadata, Vertices, Indices, DilatedVertices, DilatedIndices);
	}
	// Remove the last two triangles since AddverticesForRiverSplineStep doesn't know which distance is the final distance and tries to create a triangle
	// which links to vertices that don't exist.
	Indices.SetNum(Indices.Num() - 6);
	
	// Add an extra point at the end to dilate ending edge
	if (ShapeDilation > 0.f)
	{
		AddTerminalVerticesForRiverSpline(ERiverBoundaryEdge::End, this, SplineComp, WaterSplineMetadata, DilatedVertices, DilatedIndices);
	}

	WaterBodyMeshVertices = MoveTemp(Vertices);
	WaterBodyMeshIndices = MoveTemp(Indices);
	DilatedWaterBodyMeshVertices = MoveTemp(DilatedVertices);
	DilatedWaterBodyMeshIndices = MoveTemp(DilatedIndices);
}

FBoxSphereBounds UWaterBodyRiverComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox BoxExtent(ForceInit);
	for (const USplineMeshComponent* SplineMeshComponent : SplineMeshComponents)
	{
		if (SplineMeshComponent != nullptr)
		{
			const FBox SplineMeshComponentBounds = SplineMeshComponent->CalcBounds(SplineMeshComponent->GetRelativeTransform()).GetBox();
			BoxExtent += SplineMeshComponentBounds;
		}
	}
	// Spline mesh components aren't storing our vertical bounds so account for that with the ChannelDepth parameter.
	BoxExtent.Max.Z += MaxWaveHeightOffset;
	BoxExtent.Min.Z -= GetChannelDepth();
	return FBoxSphereBounds(BoxExtent).TransformBy(LocalToWorld);
}

void UWaterBodyRiverComponent::UpdateMaterialInstances()
{
	Super::UpdateMaterialInstances();

	CreateOrUpdateLakeTransitionMID();
	CreateOrUpdateOceanTransitionMID();
}

void UWaterBodyRiverComponent::SetLakeTransitionMaterial(UMaterialInterface* InMaterial)
{
	LakeTransitionMaterial = InMaterial;
	UpdateMaterialInstances();
}

void UWaterBodyRiverComponent::SetOceanTransitionMaterial(UMaterialInterface* InMaterial)
{
	OceanTransitionMaterial = InMaterial;
	UpdateMaterialInstances();
}

void UWaterBodyRiverComponent::Reset()
{
	for (USplineMeshComponent* Comp : SplineMeshComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	SplineMeshComponents.Empty();
}

UMaterialInstanceDynamic* UWaterBodyRiverComponent::GetRiverToLakeTransitionMaterialInstance()
{
	CreateOrUpdateLakeTransitionMID();
	return LakeTransitionMID;
}

UMaterialInstanceDynamic* UWaterBodyRiverComponent::GetRiverToOceanTransitionMaterialInstance()
{
	CreateOrUpdateOceanTransitionMID();
	return OceanTransitionMID;
}

#if WITH_EDITOR
TArray<UPrimitiveComponent*> UWaterBodyRiverComponent::GetBrushRenderableComponents() const
{
	TArray<UPrimitiveComponent*> BrushRenderableComponents;
	
	BrushRenderableComponents.Reserve(SplineMeshComponents.Num());
	Algo::ForEach(SplineMeshComponents, [&BrushRenderableComponents](USplineMeshComponent* Comp)
	{
		if (Comp != nullptr)
		{
			BrushRenderableComponents.Add(StaticCast<UPrimitiveComponent*>(Comp));
		}
	});
	
	return BrushRenderableComponents;
}
#endif //WITH_EDITOR

void UWaterBodyRiverComponent::CreateOrUpdateLakeTransitionMID()
{
	if (GetWorld())
	{
		LakeTransitionMID = FWaterUtils::GetOrCreateTransientMID(LakeTransitionMID, TEXT("LakeTransitionMID"), LakeTransitionMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(LakeTransitionMID);
	}
}

void UWaterBodyRiverComponent::CreateOrUpdateOceanTransitionMID()
{
	if (GetWorld())
	{
		OceanTransitionMID = FWaterUtils::GetOrCreateTransientMID(OceanTransitionMID, TEXT("OceanTransitionMID"), OceanTransitionMaterial, GetTransientMIDFlags());

		SetDynamicParametersOnMID(OceanTransitionMID);
	}
}

void UWaterBodyRiverComponent::GenerateMeshes()
{
	Reset();

	AActor* Owner = GetOwner();
	check(Owner);
	
	if (const USplineComponent* WaterSpline = GetWaterSpline())
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();

		const int32 NumberOfMeshComponentsNeeded = WaterSpline->IsClosedLoop() ? NumSplinePoints : NumSplinePoints - 1;
		for (int32 SplinePtIndex = 0; SplinePtIndex < NumberOfMeshComponentsNeeded; ++SplinePtIndex)
		{
			FString Name = FString::Printf(TEXT("SplineMeshComponent_%d"), SplinePtIndex);
			USplineMeshComponent* MeshComp = NewObject<USplineMeshComponent>(Owner, *Name, RF_Transactional);
			MeshComp->SetNetAddressable(); // it's deterministically named so it's addressable over network (needed for collision)
			SplineMeshComponents.Add(MeshComp);
			MeshComp->SetMobility(Owner->GetRootComponent()->Mobility);
			MeshComp->SetupAttachment(this);
			if (GetWorld() && GetWorld()->bIsWorldInitialized)
			{
				MeshComp->RegisterComponent();
			}

			// Call UpdateSplineMesh after RegisterComponent so that physics state creation can happen (needs the component to be registered)
			UpdateSplineMesh(MeshComp, SplinePtIndex);
		}
	}
}

void UWaterBodyRiverComponent::UpdateSplineMesh(USplineMeshComponent* MeshComp, int32 SplinePointIndex)
{
	if (const USplineComponent* WaterSpline = GetWaterSpline())
	{
		const int32 NumSplinePoints = WaterSpline->GetNumberOfSplinePoints();

		const int32 StartSplinePointIndex = SplinePointIndex;
		const int32 StopSplinePointIndex = WaterSpline->IsClosedLoop() && StartSplinePointIndex == NumSplinePoints - 1 ? 0 : StartSplinePointIndex + 1;

		UStaticMesh* StaticMesh = GetWaterMeshOverride();
		if (StaticMesh == nullptr)
		{
			StaticMesh = UWaterSubsystem::StaticClass()->GetDefaultObject<UWaterSubsystem>()->DefaultRiverMesh;
		}
		check(StaticMesh != nullptr);
		MeshComp->SetStaticMesh(StaticMesh);
		MeshComp->SetMaterial(0, GetWaterMaterialInstance());

		CopySharedCollisionSettingsToComponent(MeshComp);
		CopySharedNavigationSettingsToComponent(MeshComp);
		MeshComp->SetCastShadow(false);

		const bool bUpdateMesh = false;
		const FVector StartScale = WaterSpline->GetScaleAtSplinePoint(StartSplinePointIndex);
		const FVector EndScale = WaterSpline->GetScaleAtSplinePoint(StopSplinePointIndex);

		// Scale the water mesh so that it is the size of the bounds
		FVector StaticMeshExtent = 2.0f * StaticMesh->GetBounds().BoxExtent;
		StaticMeshExtent.X = FMath::Max(StaticMeshExtent.X, 0.0001f);
		StaticMeshExtent.Y = FMath::Max(StaticMeshExtent.Y, 0.0001f);
		StaticMeshExtent.Z = 1.0f;

		MeshComp->SetStartScale(FVector2D(StartScale / StaticMeshExtent), bUpdateMesh);
		MeshComp->SetEndScale(FVector2D(EndScale / StaticMeshExtent), bUpdateMesh);

		FVector StartPos, StartTangent;
		WaterSpline->GetLocationAndTangentAtSplinePoint(StartSplinePointIndex, StartPos, StartTangent, ESplineCoordinateSpace::Local);

		FVector EndPos, EndTangent;
		WaterSpline->GetLocationAndTangentAtSplinePoint(StopSplinePointIndex, EndPos, EndTangent, ESplineCoordinateSpace::Local);

		MeshComp->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent, bUpdateMesh);

		MeshComp->UpdateMesh();

		//
		// GROSS HACK to work around the issue above that CreatePhysicsMeshes() doesn't currently work at Runtime.
		// The "cook" for a FKConvexElem just creates and caches a Chaos::FConvex instance, and the restore from cooked
		// data just restores that and passes it to the FKConvexElem. So we're just going to do that ourselves.
		// Code below is taken from FChaosDerivedDataCooker::BuildConvexMeshes() 
		//
		if (MeshComp->GetBodySetup())
		{
			for (FKConvexElem& Elem : MeshComp->GetBodySetup()->AggGeom.ConvexElems)
			{
				const int32 NumHullVerts = Elem.VertexData.Num();
				TArray<Chaos::FConvex::FVec3Type> ConvexVertices;
				ConvexVertices.SetNum(NumHullVerts);
				for (int32 VertIndex = 0; VertIndex < NumHullVerts; ++VertIndex)
				{
					ConvexVertices[VertIndex] = Elem.VertexData[VertIndex];
				}

				TSharedPtr<Chaos::FConvex, ESPMode::ThreadSafe> ChaosConvex = MakeShared<Chaos::FConvex, ESPMode::ThreadSafe>(ConvexVertices, 0.0f);

				Elem.SetChaosConvexMesh(MoveTemp(ChaosConvex));
			}

			MeshComp->RecreatePhysicsState();
		}
	}
}

#if WITH_EDITOR
void UWaterBodyRiverComponent::OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged)
{
	Super::OnPostEditChangeProperty(PropertyChangedEvent, bShapeOrPositionChanged, bWeightmapSettingsChanged);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UWaterBodyRiverComponent, LakeTransitionMaterial) ||
		PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UWaterBodyRiverComponent, OceanTransitionMaterial))
	{
		UpdateMaterialInstances();
	}
}
const TCHAR* UWaterBodyRiverComponent::GetWaterSpriteTextureName() const
{
	return TEXT("/Water/Icons/WaterBodyRiverSprite");
}
#endif