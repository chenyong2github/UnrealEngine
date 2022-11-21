// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGPoint.h"

namespace PCGPointCustomPropertyNames
{
	const FName ExtentsName = TEXT("Extents");
	const FName LocalCenterName = TEXT("LocalCenter");
	const FName PositionName = TEXT("Position");
	const FName RotationName = TEXT("Rotation");
	const FName ScaleName = TEXT("Scale");
}

FPCGPoint::FPCGPoint(const FTransform& InTransform, float InDensity, int32 InSeed)
	: Transform(InTransform)
	, Density(InDensity)
	, Seed(InSeed)
{
}

FBox FPCGPoint::GetLocalBounds() const
{
	return FBox(BoundsMin, BoundsMax);
}

void FPCGPoint::SetLocalBounds(const FBox& InBounds)
{
	BoundsMin = InBounds.Min;
	BoundsMax = InBounds.Max;
}

FBoxSphereBounds FPCGPoint::GetDensityBounds() const
{
	return FBoxSphereBounds(FBox((2 - Steepness) * BoundsMin, (2 - Steepness) * BoundsMax).TransformBy(Transform));
}

FPCGPoint::PointCustomPropertyGetterSetter::PointCustomPropertyGetterSetter(const FPCGPoint::PointCustomPropertyGetter& InGetter, const FPCGPoint::PointCustomPropertySetter& InSetter, int16 InType, FName InName)
	: Getter(InGetter)
	, Setter(InSetter)
	, Type(InType)
	, Name(InName)
{}

bool FPCGPoint::HasCustomPropertyGetterSetter(FName Name)
{
	return Name == PCGPointCustomPropertyNames::ExtentsName ||
		Name == PCGPointCustomPropertyNames::LocalCenterName ||
		Name == PCGPointCustomPropertyNames::PositionName ||
		Name == PCGPointCustomPropertyNames::RotationName ||
		Name == PCGPointCustomPropertyNames::ScaleName;
}

FPCGPoint::PointCustomPropertyGetterSetter FPCGPoint::CreateCustomPropertyGetterSetter(FName Name)
{
	if (Name == PCGPointCustomPropertyNames::ExtentsName)
	{
		return FPCGPoint::PointCustomPropertyGetterSetter(
			[](const FPCGPoint& Point, void* OutValue) { *reinterpret_cast<FVector*>(OutValue) = Point.GetExtents(); return true; },
			[](FPCGPoint& Point, const void* InValue) { Point.SetExtents(*reinterpret_cast<const FVector*>(InValue)); return true; },
			(int16)PCG::Private::MetadataTypes<FVector>::Id,
			PCGPointCustomPropertyNames::ExtentsName
		);
	}
	else if (Name == PCGPointCustomPropertyNames::LocalCenterName)
	{
		return FPCGPoint::PointCustomPropertyGetterSetter(
			[](const FPCGPoint& Point, void* OutValue) { *reinterpret_cast<FVector*>(OutValue) = Point.GetLocalCenter(); return true; },
			[](FPCGPoint& Point, const void* InValue) { Point.SetLocalCenter(*reinterpret_cast<const FVector*>(InValue)); return true; },
			(int16)PCG::Private::MetadataTypes<FVector>::Id,
			PCGPointCustomPropertyNames::LocalCenterName
		);
	}
	else if (Name == PCGPointCustomPropertyNames::PositionName)
	{
		return FPCGPoint::PointCustomPropertyGetterSetter(
			[](const FPCGPoint& Point, void* OutValue) { *reinterpret_cast<FVector*>(OutValue) = Point.Transform.GetLocation(); return true; },
			[](FPCGPoint& Point, const void* InValue) { Point.Transform.SetLocation(*reinterpret_cast<const FVector*>(InValue)); return true; },
			(int16)PCG::Private::MetadataTypes<FVector>::Id,
			PCGPointCustomPropertyNames::PositionName
		);
	}
	else if (Name == PCGPointCustomPropertyNames::RotationName)
	{
		return FPCGPoint::PointCustomPropertyGetterSetter(
			[](const FPCGPoint& Point, void* OutValue) { *reinterpret_cast<FQuat*>(OutValue) = Point.Transform.GetRotation(); return true; },
			[](FPCGPoint& Point, const void* InValue) { Point.Transform.SetRotation(*reinterpret_cast<const FQuat*>(InValue)); return true; },
			(int16)PCG::Private::MetadataTypes<FQuat>::Id,
			PCGPointCustomPropertyNames::RotationName
		);
	}
	else if (Name == PCGPointCustomPropertyNames::ScaleName)
	{
		return FPCGPoint::PointCustomPropertyGetterSetter(
			[](const FPCGPoint& Point, void* OutValue) { *reinterpret_cast<FVector*>(OutValue) = Point.Transform.GetScale3D(); return true; },
			[](FPCGPoint& Point, const void* InValue) { Point.Transform.SetScale3D(*reinterpret_cast<const FVector*>(InValue)); return true; },
			(int16)PCG::Private::MetadataTypes<FVector>::Id,
			PCGPointCustomPropertyNames::ScaleName
		);
	}

	return FPCGPoint::PointCustomPropertyGetterSetter();
}