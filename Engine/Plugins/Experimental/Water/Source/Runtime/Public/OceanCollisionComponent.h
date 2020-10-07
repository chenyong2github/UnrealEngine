// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/ShapeComponent.h"
#include "Components/BoxComponent.h"
#include "PhysicsEngine/ConvexElem.h"
#include "OceanCollisionComponent.generated.h"

UCLASS(ClassGroup = (Custom))
class WATER_API UOceanCollisionComponent : public UPrimitiveComponent
{
	friend class FOceanCollisionSceneProxy;

	GENERATED_UCLASS_BODY()
public:

	void InitializeFromConvexElements(const TArray<FKConvexElem>& ConvexElements);

	virtual bool IsZeroExtent() const override { return BoundingBox.GetExtent().IsZero(); }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;
	virtual UBodySetup* GetBodySetup() override;

	/** Collects custom navigable geometry of component.
	*   Substract the MaxWaveHeight to the Ocean collision so nav mesh geometry is exported a ground level
	*	@return true if regular navigable geometry exporting should be run as well */
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;

protected:
	void UpdateBodySetup(const TArray<FKConvexElem>& ConvexElements);
	void CreateOceanBodySetupIfNeeded();
private:
	UPROPERTY(NonPIEDuplicateTransient)
	class UBodySetup* CachedBodySetup;

	FBox BoundingBox;
};



UCLASS()
class WATER_API UOceanBoxCollisionComponent : public UBoxComponent
{
	GENERATED_UCLASS_BODY()
public:
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;
};

