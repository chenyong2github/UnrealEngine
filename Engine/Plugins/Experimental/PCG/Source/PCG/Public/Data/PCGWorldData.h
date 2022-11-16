// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGSpatialData.h"
#include "PCGVolumeData.h"
#include "PCGSurfaceData.h"

#include "Engine/EngineTypes.h"

#include "PCGWorldData.generated.h"

class UWorld;
class UPCGMetadata;
class UPCGComponent;

USTRUCT(BlueprintType)
struct FPCGWorldVolumetricQueryParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bSearchForOverlap = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bIgnorePCGHits = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bIgnoreSelfHits = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data|Advanced")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldStatic;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGWorldVolumetricData : public UPCGVolumeData
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, const FBox& InBounds = FBox(EForceInit::ForceInit));

	//~Begin UPCGSpatialData interface
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
protected:
	virtual UPCGSpatialData* CopyInternal() const override;
	//~End UPCGSpatialData interface

public:
	//~Begin UPCGSpatialDataWithPointCache
	virtual bool SupportsBoundedPointData() const { return true; }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override { return CreatePointData(Context, FBox(EForceInit::ForceInit)); }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context, const FBox& InBounds) const override;
	//~End UPCGSpatialDataWithPointCache

	UPROPERTY()
	TWeakObjectPtr<UWorld> World = nullptr;

	UPROPERTY()
	TWeakObjectPtr<UPCGComponent> OriginatingComponent = nullptr;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data, meta = (ShowOnlyInnerProperties))
	FPCGWorldVolumetricQueryParams QueryParams;
};

USTRUCT(BlueprintType)
struct FPCGWorldRayHitQueryParams
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bOverrideDefaultParams = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data, meta = (EditCondition = "bOverrideDefaultParams"))
	FVector RayOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data, meta = (EditCondition = "bOverrideDefaultParams"))
	FVector RayDirection = FVector(0.0, 0.0, -1.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data, meta = (EditCondition = "bOverrideDefaultParams"))
	double RayLength = 1.0e+5; // 100m

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bIgnorePCGHits = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data)
	bool bIgnoreSelfHits = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data|Advanced")
	TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_WorldStatic;

	// TODO: see in FCollisionQueryParams if there are some flags we want to expose
	// examples: bTraceComplex, bReturnFaceIndex, bReturnPhysicalMaterial, some ignore patterns

	//TODO UPROPERTY()
	//bool bUseMetadataFromLandscape = true;
};

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGWorldRayHitData : public UPCGSurfaceData
{
	GENERATED_BODY()

public:
	void Initialize(UWorld* InWorld, const FBox& InBounds = FBox(EForceInit::ForceInit));

	//~Begin UPCGData interface
	virtual EPCGDataType GetDataType() const override { return Super::GetDataType(); }
	//~End UPCGData interface

	//~Begin UPCGSpatialData interface
	virtual FBox GetBounds() const override { return Bounds; }
	virtual FBox GetStrictBounds() const override { return Bounds; }
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool HasNonTrivialTransform() const override { return true; }
protected:
	virtual UPCGSpatialData* CopyInternal() const override;
	//~End UPCGSpatialData interface

public:
	// ~Begin UPCGSpatialDataWithPointCache interface
	virtual bool SupportsBoundedPointData() const { return true; }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override { return CreatePointData(Context, FBox(EForceInit::ForceInit)); }
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context, const FBox& InBounds) const override;
	// ~End UPCGConcreteDataWithPointCache interface

	UPROPERTY()
	TWeakObjectPtr<UWorld> World = nullptr;

	UPROPERTY()
	TWeakObjectPtr<UPCGComponent> OriginatingComponent = nullptr;

	UPROPERTY()
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Data, meta = (ShowOnlyInnerProperties))
	FPCGWorldRayHitQueryParams QueryParams;
};