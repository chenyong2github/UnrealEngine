// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGLandscapeData.h"

#include "PCGHelpers.h"
#include "PCGSubsystem.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGSpatialData.h"
#include "Grid/PCGLandscapeCache.h"
#include "Helpers/PCGAsync.h"

#include "Landscape.h"
#include "LandscapeEdit.h"

void UPCGLandscapeData::Initialize(ALandscapeProxy* InLandscape, const FBox& InBounds)
{
	check(InLandscape);
	Landscape = InLandscape;
	TargetActor = InLandscape;
	Bounds = InBounds;

	Transform = Landscape->GetActorTransform();

	// TODO: find a better way to do this - maybe there should be a prototype metadata in the landscape cache
	if (Landscape->GetWorld())
	{
		if (UPCGSubsystem* Subsystem = Landscape->GetWorld()->GetSubsystem<UPCGSubsystem>())
		{
			if (FPCGLandscapeCache* LandscapeCache = Subsystem->GetLandscapeCache())
			{
				const TArray<FName> Layers = Subsystem->GetLandscapeCache()->GetLayerNames(Landscape.Get());

				for (const FName& Layer : Layers)
				{
					Metadata->CreateFloatAttribute(Layer, 0.0f, /*bAllowInterpolation=*/true);
				}
			}
			else
			{
				UE_LOG(LogPCG, Error, TEXT("Landscape is unable to access the landscape cache"));
			}
		}
	}
}

FBox UPCGLandscapeData::GetBounds() const
{
	return Bounds;
}

FBox UPCGLandscapeData::GetStrictBounds() const
{
	// TODO: if the landscape contains holes, then the strict bounds
	// should be empty
	return Bounds;
}

bool UPCGLandscapeData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	if (!Landscape || !Landscape->GetWorld())
	{
		return false;
	}

	UPCGSubsystem* Subsystem = Landscape->GetWorld()->GetSubsystem<UPCGSubsystem>();

	if (!Subsystem || !Subsystem->GetLandscapeCache())
	{
		UE_LOG(LogPCG, Error, TEXT("PCG Subsystem or landscape cache are not initialized"));
		return false;
	}

	// Compute landscape-centric coordinates from the transform
	// TODO: support bounds tests/interpolation
	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();

	if (!LandscapeInfo)
	{
		return false;
	}

	const FTransform LocalTransform = InTransform * Transform.Inverse();
	const FVector LocalPoint = LocalTransform.GetLocation();

	const FIntPoint ComponentMapKey(FMath::FloorToInt(LocalPoint.X / LandscapeInfo->ComponentSizeQuads), FMath::FloorToInt(LocalPoint.Y / LandscapeInfo->ComponentSizeQuads));

	if (ULandscapeComponent* LandscapeComponent = LandscapeInfo->XYtoComponentMap.FindRef(ComponentMapKey))
	{
		check(Subsystem->GetLandscapeCache());
		const FPCGLandscapeCacheEntry* LandscapeCacheEntry = Subsystem->GetLandscapeCache()->GetCacheEntry(LandscapeComponent, ComponentMapKey);

		if (!LandscapeCacheEntry)
		{
			return false;
		}

		// Compute the 4 points indices
		const FVector2D ComponentLocalPoint(LocalPoint.X - ComponentMapKey.X * LandscapeInfo->ComponentSizeQuads, LocalPoint.Y - ComponentMapKey.Y * LandscapeInfo->ComponentSizeQuads);
		const int32 X0Y0 = FMath::FloorToInt(ComponentLocalPoint.X) + FMath::FloorToInt(ComponentLocalPoint.Y) * (LandscapeInfo->ComponentSizeQuads + 1);
		const int32 X1Y0 = X0Y0 + 1;
		const int32 X0Y1 = X0Y0 + (LandscapeInfo->ComponentSizeQuads + 1);
		const int32 X1Y1 = X0Y1 + 1;

		FPCGPoint PX0Y0;
		FPCGPoint PX1Y0;
		FPCGPoint PX0Y1;
		FPCGPoint PX1Y1;

		LandscapeCacheEntry->GetPoint(X0Y0, PX0Y0, OutMetadata);
		LandscapeCacheEntry->GetPoint(X1Y0, PX1Y0, OutMetadata);
		LandscapeCacheEntry->GetPoint(X0Y1, PX0Y1, OutMetadata);
		LandscapeCacheEntry->GetPoint(X1Y1, PX1Y1, OutMetadata);


		PCGPointHelpers::Bilerp(
			PX0Y0,
			PX1Y0,
			PX0Y1,
			PX1Y1,
			OutMetadata,
			OutPoint,
			OutMetadata,
			FMath::Fractional(LocalPoint.X),
			FMath::Fractional(LocalPoint.Y));

		return true;
	}
	else
	{
		return false;
	}
}

const UPCGPointData* UPCGLandscapeData::CreatePointData(FPCGContext* Context, const FBox& InBounds) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGLandscapeData::CreatePointData);

	if (!Landscape || !Landscape->GetWorld())
	{
		return nullptr;
	}

	UPCGSubsystem* Subsystem = Landscape->GetWorld()->GetSubsystem<UPCGSubsystem>();

	if (!Subsystem || !Subsystem->GetLandscapeCache())
	{
		UE_LOG(LogPCG, Error, TEXT("PCG Subsystem or landscape cache are not initialized"));
		return nullptr;
	}

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();

	if (!LandscapeInfo)
	{
		return nullptr;
	}

	const int32 ComponentSizeQuads = LandscapeInfo->ComponentSizeQuads;

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGLandscapeData*>(this));
	Data->InitializeFromData(this);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	FBox EffectiveBounds = Bounds;
	if (InBounds.IsValid)
	{
		EffectiveBounds = Bounds.Overlap(InBounds);
	}

	// Early out
	if (!EffectiveBounds.IsValid)
	{
		return Data;
	}

	// TODO: add offset to nearest edge, will have an impact if the grid size doesn't match the landscape size
	const FVector MinPt = Transform.InverseTransformPosition(EffectiveBounds.Min);
	const FVector MaxPt = Transform.InverseTransformPosition(EffectiveBounds.Max);

	// Note: the MaxX/Y here are inclusive, hence the floor & the +1 in the sizes
	const int32 MinX = FMath::CeilToInt(MinPt.X);
	const int32 MaxX = FMath::FloorToInt(MaxPt.X);
	const int32 MinY = FMath::CeilToInt(MinPt.Y);
	const int32 MaxY = FMath::FloorToInt(MaxPt.Y);

	//Early out if the bounds do not overlap any landscape vertices
	if (MaxX < MinX || MaxY < MinY)
	{
		return Data;
	}

	const int64 PointCountUpperBound = (1 + MaxX - MinX) * (1 + MaxY - MinY);
	if (PointCountUpperBound > 0)
	{
		Points.Reserve(PointCountUpperBound);
	}

	const int32 MinComponentX = MinX / ComponentSizeQuads;
	const int32 MaxComponentX = MaxX / ComponentSizeQuads;
	const int32 MinComponentY = MinY / ComponentSizeQuads;
	const int32 MaxComponentY = MaxY / ComponentSizeQuads;

	for (int32 ComponentX = MinComponentX; ComponentX <= MaxComponentX; ++ComponentX)
	{
		for (int32 ComponentY = MinComponentY; ComponentY <= MaxComponentY; ++ComponentY)
		{
			FIntPoint ComponentMapKey(ComponentX, ComponentY);
			if (ULandscapeComponent* LandscapeComponent = LandscapeInfo->XYtoComponentMap.FindRef(ComponentMapKey))
			{
				const FPCGLandscapeCacheEntry* LandscapeCacheEntry = Subsystem->GetLandscapeCache()->GetCacheEntry(LandscapeComponent, ComponentMapKey);

				if (!LandscapeCacheEntry)
				{
					continue;
				}

				// Rebase our bounds in the component referential
				const int32 LocalMinX = FMath::Clamp(MinX - ComponentMapKey.X * ComponentSizeQuads, 0, ComponentSizeQuads - 1);
				const int32 LocalMaxX = FMath::Clamp(MaxX - ComponentMapKey.X * ComponentSizeQuads, 0, ComponentSizeQuads - 1);

				const int32 LocalMinY = FMath::Clamp(MinY - ComponentMapKey.Y * ComponentSizeQuads, 0, ComponentSizeQuads - 1);
				const int32 LocalMaxY = FMath::Clamp(MaxY - ComponentMapKey.Y * ComponentSizeQuads, 0, ComponentSizeQuads - 1);

				// We can't really copy data from the component points wholesale because the component points have an additional boundary point.
				// TODO: consider optimizing this, though it will impact the Sample then
				for (int32 LocalX = LocalMinX; LocalX <= LocalMaxX; ++LocalX)
				{
					for (int32 LocalY = LocalMinY; LocalY <= LocalMaxY; ++LocalY)
					{
						FPCGPoint& Point = Points.Emplace_GetRef();
						LandscapeCacheEntry->GetPoint(LocalX + LocalY * (ComponentSizeQuads + 1), Point, Data->Metadata);
					}
				}
			}
		}
	}

	check(Points.Num() <= PointCountUpperBound);
	UE_LOG(LogPCG, Verbose, TEXT("Landscape %s extracted %d of %d potential points"), *Landscape->GetFName().ToString(), Points.Num(), PointCountUpperBound);

	return Data;
}