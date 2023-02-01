// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "PhysicsEngine/ShapeElem.h"
#include "SkinnedLevelSetElem.generated.h"

namespace Chaos
{
class FLevelSet;
template<typename TConcrete> class TWeightedLatticeImplicitObject;
}

USTRUCT()
struct ENGINE_API FKSkinnedLevelSetElem : public FKShapeElem
{
	GENERATED_USTRUCT_BODY()

	FKSkinnedLevelSetElem() :
		FKShapeElem(EAggCollisionShape::SkinnedLevelSet)
	{}

	FKSkinnedLevelSetElem(const FKSkinnedLevelSetElem& Other)
	{
		CloneElem(Other);
	}

	const FKSkinnedLevelSetElem& operator=(const FKSkinnedLevelSetElem& Other)
	{
		CloneElem(Other);
		return *this;
	}

	void SetWeightedLevelSet(TUniquePtr< Chaos::TWeightedLatticeImplicitObject<Chaos::FLevelSet>>&& InWeightedLevelSet);

	FTransform GetTransform() const;

	// Draw functions
	virtual void DrawElemWire(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, const float Scale, const FColor Color) const override;
	virtual void DrawElemSolid(class FPrimitiveDrawInterface* PDI, const FTransform& ElemTM, const float Scale, const FMaterialRenderProxy* MaterialRenderProxy) const override;
	void GetElemSolid(const FTransform& ElemTM, const FVector& Scale3D, const FMaterialRenderProxy* MaterialRenderProxy, int32 ViewIndex, class FMeshElementCollector& Collector) const;

	FBox CalcAABB(const FTransform& BoneTM, const FVector& Scale3D) const;
	FIntVector3 LevelSetGridResolution() const;
	FIntVector3 LatticeGridResolution() const;

	bool Serialize(FArchive& Ar);

	const TSharedPtr<Chaos::TWeightedLatticeImplicitObject<Chaos::FLevelSet>>& GetWeightedLevelSet() const
	{
		return WeightedLevelSet;
	}

private:

	TSharedPtr<Chaos::TWeightedLatticeImplicitObject<Chaos::FLevelSet>, ESPMode::ThreadSafe> WeightedLevelSet;

	/** Helper function to safely copy instances of this shape*/
	 void CloneElem(const FKSkinnedLevelSetElem& Other);
};

/* Enable our own serialization function to handle FKSkinnedLevelSetElem */
template<>
struct TStructOpsTypeTraits<FKSkinnedLevelSetElem> : public TStructOpsTypeTraitsBase2<FKSkinnedLevelSetElem>
{
	enum
	{
		WithSerializer = true
	};
};
