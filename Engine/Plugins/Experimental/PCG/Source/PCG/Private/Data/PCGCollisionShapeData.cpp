// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGCollisionShapeData.h"

#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Elements/PCGVolumeSampler.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SphereComponent.h"

#include "Chaos/GeometryQueries.h"

void UPCGCollisionShapeData::Initialize(UShapeComponent* InComponent)
{
	check(InComponent && IsSupported(InComponent));
	Shape = InComponent->GetCollisionShape();
	Transform = InComponent->GetComponentTransform();

	//Note: Shape is pre-scaled
	ShapeAdapter = MakeUnique<FPhysicsShapeAdapter>(Transform.GetRotation(), Shape);
	TargetActor = InComponent->GetOwner();

	CachedBounds = InComponent->Bounds.GetBox();
	CachedStrictBounds = CachedBounds;
}

bool UPCGCollisionShapeData::IsSupported(UShapeComponent* InComponent)
{
	if (!InComponent)
	{
		return false;
	}

	if(InComponent->IsA<UBoxComponent>() || InComponent->IsA<UCapsuleComponent>() || InComponent->IsA<USphereComponent>())
	{
		return true;
	}

	return false;
}

bool UPCGCollisionShapeData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	FCollisionShape CollisionShape;
	CollisionShape.SetBox(FVector3f(InBounds.GetExtent() * InTransform.GetScale3D())); // make sure to prescale
	FPhysicsShapeAdapter PointAdapter(InTransform.GetRotation(), CollisionShape);

	if (Chaos::Utilities::CastHelper(PointAdapter.GetGeometry(), PointAdapter.GetGeomPose(InTransform.GetTranslation()), [this](const auto& Downcast, const auto& FullGeomTransform) { return Chaos::OverlapQuery(ShapeAdapter->GetGeometry(), ShapeAdapter->GetGeomPose(Transform.GetTranslation()), Downcast, FullGeomTransform, /*Thickness=*/0); }))
	{
		return true;
	}
	else
	{
		return false;
	}
}

const UPCGPointData* UPCGCollisionShapeData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGShapeData::CreatePointData);

	const FVector DefaultVoxelSize = FVector(100.0, 100.0, 100.0);

	PCGVolumeSampler::FVolumeSamplerSettings SamplerSettings;
	SamplerSettings.VoxelSize = DefaultVoxelSize;

	const UPCGPointData* Data = PCGVolumeSampler::SampleVolume(Context, this, SamplerSettings);

	if (Data)
	{
		UE_LOG(LogPCG, Verbose, TEXT("Shape extracted %d points"), Data->GetPoints().Num());
	}

	return Data;
}

UPCGSpatialData* UPCGCollisionShapeData::CopyInternal() const
{
	UPCGCollisionShapeData* NewShapeData = NewObject<UPCGCollisionShapeData>();

	NewShapeData->Transform = Transform;
	NewShapeData->Shape = Shape;
	NewShapeData->ShapeAdapter = MakeUnique<FPhysicsShapeAdapter>(NewShapeData->Transform.GetRotation(), NewShapeData->Shape);
	NewShapeData->CachedBounds = CachedBounds;
	NewShapeData->CachedStrictBounds = CachedStrictBounds;

	return NewShapeData;
}
