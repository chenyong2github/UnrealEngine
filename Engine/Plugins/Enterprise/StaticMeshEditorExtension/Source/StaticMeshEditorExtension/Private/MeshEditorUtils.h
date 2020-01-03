// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshElement.h"
#include "EditableMesh.h"
#include "Widgets/SWidget.h"

class FMeshEditingContext;
class UPrimitiveComponent;
class UWorld;

struct FIntersectionData
{
	/** True if we have a valid interaction grabber sphere right now */
	bool bUseGrabberSphere;

	bool bIsPerspectiveView;

	FTransform CameraToWorld;

	TSharedPtr<FMeshEditingContext> EditingContext;

	/** The sphere for radial interactions */
	FSphere GrabberSphere;

	/** World space start location of the interaction ray the last time we were ticked */
	FVector LaserStart;

	/** World space end location of the interaction ray */
	FVector LaserEnd;

	EEditableMeshElementType MeshElementSelectionMode;

	float WorldScaleFactor;

	/** Transient: True if we have a valid interaction grabber sphere right now */
	bool bTraceComplex;

	/** The hit point.  With a ray, this could be the impact point along the ray.  With grabber sphere interaction, this
		would be the point within the sphere radius where we've found a point on an object to interact with */
	FVector HitLocation;

	/** True if it should include backfaces in the intersection test */
	bool bIncludeBackfaces;

	/** Default constructor that initializes everything to safe values */
	FIntersectionData();
};

// IntersectionData with 3 more intersection rays to represent a quad
struct FQuadIntersectionData : FIntersectionData
{
	FQuadIntersectionData();
	FQuadIntersectionData(const FIntersectionData& IntersectionData);

	FVector LaserStart2;
	FVector LaserStart3;
	FVector LaserStart4;
	FVector LaserEnd2;
	FVector LaserEnd3;
	FVector LaserEnd4;
};

class FMeshEditingUtils
{
public:
	static FMeshElement FindClosestMeshElement(const UPrimitiveComponent* Component, FIntersectionData& IntersectionData);
	static TArray<FMeshElement> FindMeshElementsInVolume(const UPrimitiveComponent* Component, const FQuadIntersectionData& QuadIntersectionData);

	/** Checks to see that the mesh element actually exists in the mesh */
	inline static bool IsElementIDValid( const FMeshElement& MeshElement, const UEditableMesh* EditableMesh )
	{
		if( EditableMesh != nullptr )
		{
			switch( MeshElement.ElementAddress.ElementType )
			{
				case EEditableMeshElementType::Vertex:
					return EditableMesh->IsValidVertex( FVertexID( MeshElement.ElementAddress.ElementID ) );

				case EEditableMeshElementType::Edge:
					return EditableMesh->IsValidEdge( FEdgeID( MeshElement.ElementAddress.ElementID ) );

				case EEditableMeshElementType::Polygon:
					return EditableMesh->IsValidPolygon( FPolygonID( MeshElement.ElementAddress.ElementID ) );
			}
		}

		return false;
	}

	static FBox GetElementsBoundingBox(const TArray<FMeshElement>& MeshElements, const UEditableMesh* EditableMesh );
};

template< typename ToolbarType >
class SToolbarWidget : public SWidget
{
public:

	SLATE_BEGIN_ARGS(SToolbarWidget)
	{}

	SLATE_ARGUMENT(TSharedPtr<ToolbarType>, EditingToolbar)

	SLATE_END_ARGS()

public:

	SToolbarWidget()
	{
	}

	virtual ~SToolbarWidget()
	{
		EditingToolbar.Reset();
	}

	void Construct(const FArguments& InArgs)
	{
		Visibility = EVisibility::Collapsed;
		EditingToolbar = InArgs._EditingToolbar;
	}

	// Begin SWidget overrides.
	virtual void OnArrangeChildren(const FGeometry&, FArrangedChildren&) const override
	{
	}

	virtual FChildren* GetChildren() override
	{
		return nullptr;
	}

	virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&, bool) const override
	{
		return LayerId;
	}
	// End SWidget overrides.

protected:
	// Begin SWidget overrides.
	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}
	// End SWidget overrides.

private:

	TSharedPtr<ToolbarType> EditingToolbar;
};