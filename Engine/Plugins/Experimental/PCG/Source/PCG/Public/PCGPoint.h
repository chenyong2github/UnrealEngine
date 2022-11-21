// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

#include "PCGPoint.generated.h"

UENUM()
enum class EPCGPointProperties : uint8
{
	Density,
	BoundsMin,
	BoundsMax,
	Extents,
	Color,
	Position,
	Rotation,
	Scale,
	Transform,
	Steepness,
	LocalCenter
};

USTRUCT(BlueprintType)
struct PCG_API FPCGPoint
{
	GENERATED_BODY()
public:
	FPCGPoint() = default;
	FPCGPoint(const FTransform& InTransform, float InDensity, int32 InSeed);

	FBox GetLocalBounds() const;
	void SetLocalBounds(const FBox& InBounds);
	FBoxSphereBounds GetDensityBounds() const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	FTransform Transform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	float Density = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	FVector BoundsMin = -FVector::One();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	FVector BoundsMax = FVector::One();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	FVector4 Color = FVector4::One();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties, meta = (ClampMin = "0", ClampMax = "1"))
	float Steepness = 0.5f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Properties)
	int32 Seed = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "Properties|Metadata")
	int64 MetadataEntry = -1;

	FVector GetExtents() const { return (BoundsMax - BoundsMin) / 2.0; }
	void SetExtents(const FVector& InExtents)
	{
		const FVector Center = GetLocalCenter();
		BoundsMin = Center - InExtents;
		BoundsMax = Center + InExtents;
	}

	FVector GetLocalCenter() const { return (BoundsMax + BoundsMin) / 2.0; }
	void SetLocalCenter(const FVector& InCenter)
	{
		const FVector Delta = InCenter - GetLocalCenter();
		BoundsMin += Delta;
		BoundsMax += Delta;
	}

	using PointCustomPropertyGetter = TFunction<bool(const FPCGPoint&, void*)>;
	using PointCustomPropertySetter = TFunction<bool(FPCGPoint&, const void*)>;

	struct PointCustomPropertyGetterSetter
	{
	public:
		PointCustomPropertyGetterSetter() = default;

		PointCustomPropertyGetterSetter(const PointCustomPropertyGetter& InGetter, const PointCustomPropertySetter& InSetter, int16 InType, FName InName);

		template<typename T>
		bool Get(const FPCGPoint& Point, T& OutValue) const
		{
			if (PCG::Private::IsOfTypes<T>(Type))
			{
				return Getter(Point, &OutValue);
			}
			else
			{
				return false;
			}
		}

		template<typename T>
		bool Set(FPCGPoint& Point, const T& InValue)
		{
			if (PCG::Private::IsOfTypes<T>(Type))
			{
				return Setter(Point, &InValue);
			}
			else
			{
				return false;
			}
		}

		bool IsValid() const { return Type >= 0 && Type < (int16)EPCGMetadataTypes::Count; }
		int16 GetType() const { return Type; }
		FName GetName() const { return Name; }

	private:
		PointCustomPropertyGetter Getter = [](const FPCGPoint&, void*) { return false; };
		PointCustomPropertySetter Setter = [](FPCGPoint&, const void*) { return false; };
		int16 Type = -1;
		FName Name = NAME_None;
	};

	static bool HasCustomPropertyGetterSetter(FName Name);
	static PointCustomPropertyGetterSetter CreateCustomPropertyGetterSetter(FName Name);
};
