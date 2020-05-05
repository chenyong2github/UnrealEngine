// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/MeshComponent.h"
#include "Containers/SparseArray.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveSceneProxy.h"

#include "LineSetComponent.generated.h"

class FPrimitiveSceneProxy;

struct FRenderableLine
{
	FRenderableLine()
		: Start(ForceInitToZero)
		, End(ForceInitToZero)
		, Color(ForceInitToZero)
		, Thickness(0.0f)
		, DepthBias(0.0f)
	{}

	FRenderableLine(const FVector& InStart, const FVector& InEnd, const FColor& InColor, const float InThickness, const float InDepthBias = 0.0f)
		: Start(InStart)
		, End(InEnd)
		, Color(InColor)
		, Thickness(InThickness)
		, DepthBias(InDepthBias)
	{}

	FVector Start;
	FVector End;
	FColor Color;
	float Thickness;
	float DepthBias;
};

UCLASS()
class MODELINGCOMPONENTS_API ULineSetComponent : public UMeshComponent
{
	GENERATED_BODY()

public:

	ULineSetComponent();

	/** Specify material which handles lines */
	void SetLineMaterial(UMaterialInterface* InLineMaterial);

	/** Clear the line set */
	void Clear();

	/** Reserve enough memory for up to the given ID (for inserting via ID) */
	void ReserveLines(const int32 MaxID);

	/** Add a line to be rendered using the component. */
	int32 AddLine(const FRenderableLine& OverlayLine);

	/** Create and add a line to be rendered using the component. */
	inline int32 AddLine(const FVector& InStart, const FVector& InEnd, const FColor& InColor, const float InThickness, const float InDepthBias = 0.0f)
	{
		// This is just a convenience function to avoid client code having to know about FRenderableLine.
		return AddLine(FRenderableLine(InStart, InEnd, InColor, InThickness, InDepthBias));
	}

	/** Insert a line with the given ID to the overlay */
	void InsertLine(const int32 ID, const FRenderableLine& OverlayLine);

	/** Sets the color of a line */
	void SetLineColor(const int32 ID, const FColor& NewColor);

	/** Sets the thickness of a line */
	void SetLineThickness(const int32 ID, const float NewThickness);

	/** Remove a line from the set */
	void RemoveLine(const int32 ID);

	/** Queries whether a line with the given ID exists */
	bool IsLineValid(const int32 ID) const;

private:

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	//~ End UPrimitiveComponent Interface.

	//~ Begin UMeshComponent Interface.
	virtual int32 GetNumMaterials() const override;
	//~ End UMeshComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	UPROPERTY()
	const UMaterialInterface* LineMaterial;

	UPROPERTY()
	mutable FBoxSphereBounds Bounds;

	UPROPERTY()
	mutable bool bBoundsDirty;

	TSparseArray<FRenderableLine> Lines;

	friend class FLineSetSceneProxy;
};