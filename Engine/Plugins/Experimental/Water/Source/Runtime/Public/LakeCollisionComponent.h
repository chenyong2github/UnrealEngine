// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/ShapeComponent.h"
#include "LakeCollisionComponent.generated.h"

UCLASS(ClassGroup = (Custom))
class WATER_API ULakeCollisionComponent : public UPrimitiveComponent
{
	friend class FLakeCollisionSceneProxy;

	GENERATED_UCLASS_BODY()
public:
	void UpdateCollision(FVector InBoxExtent, bool bSplinePointsChanged);
	
	virtual bool IsZeroExtent() const override { return BoxExtent.IsZero(); }
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const;
	virtual UBodySetup* GetBodySetup() override;

	/** Collects custom navigable geometry of component.
    *   Substract the MaxWaveHeight to the Lake collision so nav mesh geometry is exported a ground level
	*	@return true if regular navigable geometry exporting should be run as well */
	virtual bool DoCustomNavigableGeometryExport(FNavigableGeometryExport& GeomExport) const override;

protected:
	void UpdateBodySetup();
	void CreateLakeBodySetupIfNeeded();
private:
	UPROPERTY(NonPIEDuplicateTransient)
	class UBodySetup* CachedBodySetup;

	UPROPERTY()
	FVector BoxExtent;
};