// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGPrimitiveData.h"

#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"
#include "PCGHelpers.h"

#include "Components/PrimitiveComponent.h"

void UPCGPrimitiveData::Initialize(UPrimitiveComponent* InPrimitive)
{
	check(InPrimitive);
	Primitive = InPrimitive;
	TargetActor = InPrimitive->GetOwner();
	CachedBounds = Primitive->Bounds.GetBox();
	// Not obvious to find strict bounds, leave at the default value
}

bool UPCGPrimitiveData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	FCollisionShape CollisionShape;
	CollisionShape.SetBox(FVector3f(InBounds.GetExtent() * InTransform.GetScale3D()));

	const FVector BoxCenter = InTransform.TransformPosition(InBounds.GetCenter());

	if (Primitive->OverlapComponent(BoxCenter, InTransform.GetRotation(), CollisionShape))
	{
		OutPoint.Transform = InTransform;
		OutPoint.SetLocalBounds(InBounds);
		OutPoint.Density = 1.0f;
		return true;
	}
	else
	{
		return false;
	}
}

const UPCGPointData* UPCGPrimitiveData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGPrimitiveData::CreatePointData);

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGPrimitiveData*>(this));
	Data->InitializeFromData(this);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	const int32 MinX = FMath::CeilToInt(CachedBounds.Min.X / VoxelSize.X);
	const int32 MaxX = FMath::FloorToInt(CachedBounds.Max.X / VoxelSize.X);
	const int32 MinY = FMath::CeilToInt(CachedBounds.Min.Y / VoxelSize.Y);
	const int32 MaxY = FMath::FloorToInt(CachedBounds.Max.Y / VoxelSize.Y);
	const int32 MinZ = FMath::CeilToInt(CachedBounds.Min.Z / VoxelSize.Z);
	const int32 MaxZ = FMath::FloorToInt(CachedBounds.Max.Z / VoxelSize.Z);

	const int32 NumIterations = (MaxX - MinX) * (MaxY - MinY) * (MaxZ - MinZ);

	FPCGAsync::AsyncPointProcessing(Context, NumIterations, Points, [this, MinX, MaxX, MinY, MaxY, MinZ, MaxZ](int32 Index, FPCGPoint& OutPoint)
	{
		const int X = MinX + (Index % (MaxX - MinX));
		const int Y = MinY + (Index / (MaxX - MinX) % (MaxY - MinY));
		const int Z = MinZ + (Index / ((MaxX - MinX) * (MaxY - MinY)));

		const FVector SampleLocation(X * VoxelSize.X, Y * VoxelSize.Y, Z * VoxelSize.Z);
		const FBox VoxelBox(VoxelSize * -0.5, VoxelSize * 0.5);
		if (SamplePoint(FTransform(SampleLocation), VoxelBox, OutPoint, nullptr))
		{
			OutPoint.Seed = PCGHelpers::ComputeSeed(X, Y, Z);
			return true;
		}
		else
		{
			return false;
		}
	});

	UE_LOG(LogPCG, Verbose, TEXT("Primitive %s extracted %d points"), *Primitive->GetFName().ToString(), Points.Num());

	return Data;
}