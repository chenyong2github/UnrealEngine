// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshEditorUtils.h"
#include "PolygonEditingToolbar.h"
#include "MeshEditingContext.h"


#include "EditableMesh.h"
#include "EditableMeshFactory.h"
#include "HAL/IConsoleManager.h"
#include "StaticMeshAttributes.h"

#include "Engine/World.h"
#include "Templates/Tuple.h"

const IConsoleVariable* LaserFuzzySelectionDistance = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.LaserFuzzySelectionDistance" ));
const IConsoleVariable* GrabberSphereFuzzySelectionDistance = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.GrabberSphereFuzzySelectionDistance" ));
const IConsoleVariable* OverlayPerspectiveDistanceBias = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.OverlayPerspectiveDistanceBias" ));
const IConsoleVariable* OverlayOrthographicDistanceBias = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.OverlayOrthographicDistanceBias" ));
const IConsoleVariable* OverlayDistanceScaleFactor = IConsoleManager::Get().FindConsoleVariable(TEXT( "MeshEd.OverlayDistanceScaleFactor" ));

#define LOCTEXT_NAMESPACE "StaticMeshEditorExtensionToolbar"

DEFINE_LOG_CATEGORY_STATIC(LogMeshEditingUtils, Log, All);

typedef TTuple<FPlane, FVector[3]> FTriangleData;

namespace MeshEditingUtilsImpl
{
	inline float GetValue(const IConsoleVariable* ConsoleVariable, float DefaultValue)
	{
		return ConsoleVariable ? ConsoleVariable->GetFloat() : DefaultValue;
	}

	FPlane ComputeNonNormalizedTrianglePlane(FVector VertexPositions[3]);
	bool IsTriangleIntersecting( const FVector& RayStart, const FVector& RayEnd, const float RayFuzzyDistance, const FTriangleData& TriangleData, const FVector& CameraLocation, const bool bIsPerspectiveView, const float FuzzyDistanceScaleFactor, float& ClosestDistanceToRay, float& ClosestDistanceOnRay, const bool bAlreadyHitTriangle);
	FEditableMeshElementAddress FindIntersectingPolygon(const UEditableMesh& EditableMesh, const FVector& RayStart, const FVector& RayEnd, const float RayFuzzyDistance, const FVector& CameraLocation, const bool bIsPerspectiveView, const float FuzzyDistanceScaleFactor, const bool bIncludeBackfaces);
	FEditableMeshElementAddress FindIntersectingPolygon(const UPrimitiveComponent* HitComponent, FIntersectionData& IntersectionData);

	int32 TrianglePlaneRelativePosition(const FPlane& Plane, const FTriangleData& TriangleData);
	bool IsTriangleInVolume(const TArray<FPlane>& Planes, const FTriangleData& TriangleData);
	TArray<FEditableMeshElementAddress> FindPolygonsInVolume(const UEditableMesh& EditableMesh, const TArray<FPlane>& Planes, const FVector& CameraLocation, const bool bIncludeBackfaces);
	TArray<FEditableMeshElementAddress> FindPolygonsInVolume(const UPrimitiveComponent* HitComponent, const FQuadIntersectionData& QuadIntersectionData);

	TMap<FPolygonID, FTriangleData> GetFilteredTriangleData(const FMeshDescription* MeshDescription, const TArray<FPolygonID>& Polygons, const FVector& CameraLocation, bool bIncludeBackfaces);
}

FIntersectionData::FIntersectionData()
	: bUseGrabberSphere( false )
	, bIsPerspectiveView( false )
	, GrabberSphere( 0 )
	, LaserStart( FVector::ZeroVector )
	, LaserEnd( FVector::ZeroVector )
	, MeshElementSelectionMode(EEditableMeshElementType::Any)
	, WorldScaleFactor(1.0f)
	, bTraceComplex(false)
	, HitLocation( FVector::ZeroVector )
	, bIncludeBackfaces( false )
{
}

FQuadIntersectionData::FQuadIntersectionData()
	: LaserStart2(ForceInit)
	, LaserStart3(ForceInit)
	, LaserStart4(ForceInit)
	, LaserEnd2(ForceInit)
	, LaserEnd3(ForceInit)
	, LaserEnd4(ForceInit)
{
}

FQuadIntersectionData::FQuadIntersectionData(const FIntersectionData& IntersectionData)
	: FIntersectionData(IntersectionData)
	, LaserStart2(ForceInit)
	, LaserStart3(ForceInit)
	, LaserStart4(ForceInit)
	, LaserEnd2(ForceInit)
	, LaserEnd3(ForceInit)
	, LaserEnd4(ForceInit)
{
}

FMeshElement FMeshEditingUtils::FindClosestMeshElement(const UPrimitiveComponent* Component, FIntersectionData& IntersectionData)
{
	FEditableMeshElementAddress MeshElementAddress = MeshEditingUtilsImpl::FindIntersectingPolygon(Component, IntersectionData);

	if (MeshElementAddress.ElementType != EEditableMeshElementType::Invalid)
	{
		FMeshElement MeshElement;

		MeshElement.Component = const_cast<UPrimitiveComponent*>(Component);
		MeshElement.ElementAddress = MeshElementAddress;

		return MeshElement;
	}

	return FMeshElement();
}

TArray<FMeshElement> FMeshEditingUtils::FindMeshElementsInVolume(const UPrimitiveComponent* Component, const FQuadIntersectionData& QuadIntersectionData)
{
	TArray<FMeshElement> MeshElements;
	TArray<FEditableMeshElementAddress> MeshElementAddresses = MeshEditingUtilsImpl::FindPolygonsInVolume(Component, QuadIntersectionData);

	for (FEditableMeshElementAddress MeshElementAddress : MeshElementAddresses)
	{
		if (MeshElementAddress.ElementType != EEditableMeshElementType::Invalid)
		{
			FMeshElement& MeshElement = MeshElements.AddDefaulted_GetRef();

			MeshElement.Component = const_cast<UPrimitiveComponent*>(Component);
			MeshElement.ElementAddress = MeshElementAddress;
		}
	}

	return MeshElements;
}

bool MeshEditingUtilsImpl::IsTriangleIntersecting(
		const FVector& RayStart,
		const FVector& RayEnd,
		const float RayFuzzyDistance,
		const FTriangleData& TriangleData,
		const FVector& CameraLocation,
		const bool bIsPerspectiveView,
		const float FuzzyDistanceScaleFactor,
		float& ClosestDistanceToRay,
		float& ClosestDistanceOnRay,
		const bool bAlreadyHitTriangle
	)
{
	const FPlane& Plane = TriangleData.Get<0>();

	bool bHit = false;
	FVector IntersectionPoint;

	// Only allow intersection points that are in the direction of the ray end
	// Relevant in the case where ray start is on the clipping plane; we don't want to intersect
	// with the clipped part of a polygon
	if( FMath::SegmentPlaneIntersection( RayStart, RayEnd, Plane, IntersectionPoint ) && ((RayEnd - RayStart) | (IntersectionPoint - RayStart)) >= 0.0f)
	{
		const FVector* TriangleVertexPositions = TriangleData.Get<1>();
		// Reverse order for points is intentional
		const FVector& A = TriangleVertexPositions[ 2 ];
		const FVector& B = TriangleVertexPositions[ 1 ];
		const FVector& C = TriangleVertexPositions[ 0 ];

		// Following code was borrowed from FMath::ComputeBaryCentric2D
		// The cross-product to compute the normal has been removed as it is provided
		FVector N(Plane.X, Plane.Y, Plane.Z);

		// Compute twice area of triangle ABC
		const float AreaABCInv = FMath::InvSqrt(N.SizeSquared());

		// Normalize plane's normal
		N *= AreaABCInv;

		// Compute a contribution
		const float AreaPBC = N | ((B-IntersectionPoint) ^ (C-IntersectionPoint));
		const float a = AreaPBC * AreaABCInv;

		// Compute b contribution
		const float AreaPCA = N | ((C-IntersectionPoint) ^ (A-IntersectionPoint));
		const float b = AreaPCA * AreaABCInv;

		if( a > 0.0f && b > 0.0f && (1.0f - a- b) > 0.0f )
		{
			const float DistanceToCamera = bIsPerspectiveView ? ( CameraLocation - IntersectionPoint ).Size() : 0.0f;
			const float DistanceBias = bIsPerspectiveView ? MeshEditingUtilsImpl::GetValue( OverlayPerspectiveDistanceBias, 0.05f ) : MeshEditingUtilsImpl::GetValue( OverlayOrthographicDistanceBias, 1.0f );
			const float DistanceBasedScaling = DistanceBias + DistanceToCamera * FuzzyDistanceScaleFactor;

			const float DistanceToRay = 0.0f;  // We intersected the triangle, otherwise we wouldn't even be in here
			const float DistanceOnRay = ( IntersectionPoint - RayStart ).SizeSquared();
			if( DistanceOnRay < ClosestDistanceOnRay ||
				( !bAlreadyHitTriangle && FMath::Abs( DistanceOnRay - ClosestDistanceOnRay ) < RayFuzzyDistance * DistanceBasedScaling ) )
			{
				ClosestDistanceToRay = DistanceToRay;
				ClosestDistanceOnRay = DistanceOnRay;

				bHit = true;
			}
		}
	}

	return bHit;
}

FPlane MeshEditingUtilsImpl::ComputeNonNormalizedTrianglePlane(FVector VertexPositions[3])
{
		const FVector& A = VertexPositions[ 2 ];
		const FVector& B = VertexPositions[ 1 ];
		const FVector& C = VertexPositions[ 0 ];
		
		FVector Normal = ( B - A ) ^ ( C - A );

		return FPlane( Normal, FVector::DotProduct( A, Normal ) );
}

TMap<FPolygonID, FTriangleData> MeshEditingUtilsImpl::GetFilteredTriangleData(const FMeshDescription* MeshDescription, const TArray<FPolygonID>& Polygons, const FVector& CameraLocation, bool bIncludeBackfaces)
{
	static TMap<FPolygonID, FTriangleData> FilteredTriangleData;
	FilteredTriangleData.Reset();

	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>(MeshAttribute::Vertex::Position);
	TPolygonAttributesConstRef<FVector> PolygonCenters = MeshDescription->PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Center);

	FVector TriangleVertexPositions[3];
	for (const FPolygonID PolygonID : Polygons)
	{
		const TArray<FTriangleID>& PolygonTriangleIDs = MeshDescription->GetPolygonTriangleIDs(PolygonID);

		for (const FTriangleID MeshTriangleID : PolygonTriangleIDs)
		{
			for (int32 TriangleVertex = 0; TriangleVertex < 3; ++TriangleVertex)
			{
				const FVertexInstanceID VertexInstanceID = MeshDescription->GetTriangleVertexInstance(MeshTriangleID, TriangleVertex);
				const FVertexID VertexID = MeshDescription->GetVertexInstanceVertex(VertexInstanceID);

				TriangleVertexPositions[TriangleVertex] = VertexPositions[VertexID];
			}

			const FPlane PolygonPlane = ComputeNonNormalizedTrianglePlane(TriangleVertexPositions);
			if (PolygonPlane.SizeSquared() <= SMALL_NUMBER)
			{
				continue;
			}

			const FVector PolygonCenter = PolygonCenters[PolygonID];

			if (bIncludeBackfaces || (FVector::DotProduct(CameraLocation - PolygonCenter, FVector(PolygonPlane.X, PolygonPlane.Y, PolygonPlane.Z)) > 0.0f))
			{
				FTriangleData& TriangleData = FilteredTriangleData.Add(PolygonID);

				TriangleData.Get<0>() = PolygonPlane;
				FVector* TriangleVertexPositionsPtr = TriangleData.Get<1>();
				TriangleVertexPositionsPtr[0] = TriangleVertexPositions[0];
				TriangleVertexPositionsPtr[1] = TriangleVertexPositions[1];
				TriangleVertexPositionsPtr[2] = TriangleVertexPositions[2];

				break;
			}
		}
	}

	 return FilteredTriangleData;
}

FEditableMeshElementAddress MeshEditingUtilsImpl::FindIntersectingPolygon( const UEditableMesh& EditableMesh, const FVector& RayStart, const FVector& RayEnd, const float RayFuzzyDistance, const FVector& CameraLocation, const bool bIsPerspectiveView, const float FuzzyDistanceScaleFactor, const bool bIncludeBackfaces )
{
	FEditableMeshElementAddress HitElementAddress;
	HitElementAddress.SubMeshAddress = EditableMesh.GetSubMeshAddress();

	// Figure out our candidate set of polygons by performing a spatial query on the mesh
	static TArray<FPolygonID> CandidateTriangles;
	CandidateTriangles.Reset();

	check( EditableMesh.IsSpatialDatabaseAllowed() );	// We need a spatial database to do this query fast!
	EditableMesh.SearchSpatialDatabaseForPolygonsPotentiallyIntersectingLineSegment( RayStart, RayEnd, /* Out */ CandidateTriangles );

	if (CandidateTriangles.Num() == 0)
	{
		return HitElementAddress;
	}

	TMap<FPolygonID, FTriangleData> FilteredTriangles = GetFilteredTriangleData(EditableMesh.GetMeshDescription(), CandidateTriangles, CameraLocation, bIncludeBackfaces);

	float ClosestDistanceOnRay = TNumericLimits<float>::Max();
	float ClosestDistanceToRay = TNumericLimits<float>::Max();

	for( auto PolygonData : FilteredTriangles)
	{
		const bool bAlreadyHitTriangle = ( HitElementAddress.ElementType == EEditableMeshElementType::Polygon );
		const bool bHit = IsTriangleIntersecting( RayStart, RayEnd, RayFuzzyDistance, PolygonData.Value, CameraLocation, bIsPerspectiveView, FuzzyDistanceScaleFactor, ClosestDistanceToRay, ClosestDistanceOnRay, bAlreadyHitTriangle );
		if( bHit )
		{
			HitElementAddress.ElementType = EEditableMeshElementType::Polygon;
			HitElementAddress.ElementID = PolygonData.Key;
		}
	}

	return HitElementAddress;
}

FEditableMeshElementAddress MeshEditingUtilsImpl::FindIntersectingPolygon(const UPrimitiveComponent* HitComponent, FIntersectionData& IntersectionData)
{
	FEditableMeshElementAddress MeshElementAddress;

	if (IntersectionData.EditingContext->GetEditableMesh() == nullptr)
	{
		return MeshElementAddress;
	}

	const float WorldSpaceRayFuzzyDistance = MeshEditingUtilsImpl::GetValue( LaserFuzzySelectionDistance, 4.0f ) * IntersectionData.WorldScaleFactor;
	const float WorldSpaceGrabberSphereFuzzyDistance = MeshEditingUtilsImpl::GetValue( GrabberSphereFuzzySelectionDistance, 2.0f ) * IntersectionData.WorldScaleFactor;

	const FMatrix& InvRenderMatrix = HitComponent->GetRenderMatrix().InverseFast();

	const float RayFuzzyDistance = InvRenderMatrix.TransformVector( FVector( WorldSpaceRayFuzzyDistance, 0.0f, 0.0f ) ).Size();
	const float GrabberSphereFuzzyDistance = InvRenderMatrix.TransformVector( FVector( WorldSpaceGrabberSphereFuzzyDistance, 0.0f, 0.0f ) ).Size();

	// Shapes are in world space, but we need it in the local space of our component
	const FVector RayStart = InvRenderMatrix.TransformPosition( IntersectionData.LaserStart );
	const FVector RayEnd = InvRenderMatrix.TransformPosition( IntersectionData.LaserEnd );

	const FVector CameraLocation = InvRenderMatrix.TransformPosition( IntersectionData.CameraToWorld.GetLocation() );
	const float FuzzyDistanceScaleFactor = InvRenderMatrix.TransformVector( FVector( MeshEditingUtilsImpl::GetValue( OverlayDistanceScaleFactor, 0.002f ) / IntersectionData.WorldScaleFactor, 0.0f, 0.0f ) ).Size();

	return FindIntersectingPolygon(
		*IntersectionData.EditingContext->GetEditableMesh(),
		RayStart,
		RayEnd,
		RayFuzzyDistance,
		CameraLocation,
		IntersectionData.bIsPerspectiveView,
		FuzzyDistanceScaleFactor,
		IntersectionData.bIncludeBackfaces );
}

FBox FMeshEditingUtils::GetElementsBoundingBox(const TArray<FMeshElement>& MeshElements, const UEditableMesh* EditableMesh)
{
	FBox BoundingBox( ForceInitToZero );

	const FMeshDescription* MeshDescription = EditableMesh->GetMeshDescription();
	TVertexAttributesConstRef<FVector> VertexPositions = MeshDescription->VertexAttributes().GetAttributesRef<FVector>( MeshAttribute::Vertex::Position );

	for (const FMeshElement& MeshElement : MeshElements)
	{
		const UPrimitiveComponent* Component = MeshElement.Component.Get();
		if (Component == nullptr)
		{
			continue;
		}

		switch(MeshElement.ElementAddress.ElementType)
		{
			case EEditableMeshElementType::Vertex:
			{
				const FVertexID VertexID( MeshElement.ElementAddress.ElementID );
				const FVector VertexPosition = VertexPositions[ VertexID ];
				BoundingBox += Component->GetComponentTransform().TransformPosition( VertexPosition );
				break;
			}

			case EEditableMeshElementType::Edge:
			{
				const FEdgeID EdgeID( MeshElement.ElementAddress.ElementID );

				const FVertexID VertexID0 = EditableMesh->GetEdgeVertex( EdgeID, 0 );
				const FVector VertexPosition0 = VertexPositions[ VertexID0 ];
				BoundingBox += Component->GetComponentTransform().TransformPosition( VertexPosition0 );

				const FVertexID VertexID1 = EditableMesh->GetEdgeVertex( EdgeID, 1 );
				const FVector VertexPosition1 = VertexPositions[ VertexID1 ];
				BoundingBox += Component->GetComponentTransform().TransformPosition( VertexPosition1 );
				break;
			}

			case EEditableMeshElementType::Polygon:
			{
				const FPolygonID PolygonID( MeshElement.ElementAddress.ElementID );

				for( const FVertexInstanceID VertexInstanceID : MeshDescription->GetPolygonVertexInstances( PolygonID ) )
				{
					const FVector VertexPosition = VertexPositions[ MeshDescription->GetVertexInstanceVertex( VertexInstanceID ) ];
					BoundingBox += Component->GetComponentTransform().TransformPosition( VertexPosition );
				}
				break;
			}
		}
	}

	return BoundingBox;
}

int32 MeshEditingUtilsImpl::TrianglePlaneRelativePosition(const FPlane& Plane, const FTriangleData& TriangleData)
{
	// Return the position of a triangle relative to a plane: above, below or intersects it
	const FVector* TriangleVertexPositions = TriangleData.Get<1>();

	// The triangle vertex positions are projected against the plane normal to see where they are relative to the plane
	float DistA = Plane.PlaneDot(TriangleVertexPositions[0]);
	float DistB = Plane.PlaneDot(TriangleVertexPositions[1]);
	float DistC = Plane.PlaneDot(TriangleVertexPositions[2]);

	if (DistA > 0 && DistB > 0 && DistC > 0)
	{
		// All vertices are above the plane
		return 1;
	}
	else if (DistA < 0 && DistB < 0 && DistC < 0)
	{
		// All vertices are below the plane
		return -1;
	}
	// There is some intersection with the plane or a vertex is on the plane
	return 0;
}

bool MeshEditingUtilsImpl::IsTriangleInVolume(const TArray<FPlane>& Planes, const FTriangleData& TriangleData)
{
	FConvexVolume Frustum;
	for (const FPlane& Plane : Planes)
	{
		Frustum.Planes.Add(Plane.Flip());
	}
	Frustum.Init();

	const FVector* TriangleVertexPositions = TriangleData.Get<1>();

	return Frustum.IntersectLineSegment(TriangleVertexPositions[0], TriangleVertexPositions[1])
		|| Frustum.IntersectLineSegment(TriangleVertexPositions[1], TriangleVertexPositions[2])
		|| Frustum.IntersectLineSegment(TriangleVertexPositions[0], TriangleVertexPositions[2]);
}

TArray<FEditableMeshElementAddress> MeshEditingUtilsImpl::FindPolygonsInVolume(const UEditableMesh& EditableMesh, const TArray<FPlane>& Planes, const FVector& CameraLocation, const bool bIncludeBackfaces)
{
	TArray<FEditableMeshElementAddress> HitElementAddresses;

	// Figure out our candidate set of polygons by performing a spatial query on the mesh
	TArray<FPolygonID> CandidateTriangles;

	check(EditableMesh.IsSpatialDatabaseAllowed());	// We need a spatial database to do this query fast!
	EditableMesh.SearchSpatialDatabaseForPolygonsInVolume(Planes, CandidateTriangles);

	if (CandidateTriangles.Num() == 0)
	{
		return HitElementAddresses;
	}

	TMap<FPolygonID, FTriangleData> FilteredTriangles = GetFilteredTriangleData(EditableMesh.GetMeshDescription(), CandidateTriangles, CameraLocation, bIncludeBackfaces);

	for (auto PolygonData : FilteredTriangles)
	{
		bool bIsInside = IsTriangleInVolume(Planes, PolygonData.Value);
		if (bIsInside)
		{
			FEditableMeshElementAddress& HitElementAddress = HitElementAddresses.AddDefaulted_GetRef();
			HitElementAddress.SubMeshAddress = EditableMesh.GetSubMeshAddress();
			HitElementAddress.ElementType = EEditableMeshElementType::Polygon;
			HitElementAddress.ElementID = PolygonData.Key;
		}
	}

	return HitElementAddresses;
}

TArray<FEditableMeshElementAddress> MeshEditingUtilsImpl::FindPolygonsInVolume(const UPrimitiveComponent* HitComponent, const FQuadIntersectionData& QuadIntersectionData)
{
	UEditableMesh* EditableMesh = QuadIntersectionData.EditingContext->GetEditableMesh();

	if (EditableMesh == nullptr)
	{
		return TArray<FEditableMeshElementAddress>();
	}

	const FMatrix& InvRenderMatrix = HitComponent->GetRenderMatrix().InverseFast();

	// Intersection data are in world space, but we need it in the local space of our component
	const FVector Origin = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserStart);
	const FVector Origin2 = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserStart2);
	const FVector Origin3 = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserStart3);
	const FVector Origin4 = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserStart4);
	const FVector TopLeft = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserEnd);
	const FVector TopRight = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserEnd2);
	const FVector BottomLeft = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserEnd3);
	const FVector BottomRight = InvRenderMatrix.TransformPosition(QuadIntersectionData.LaserEnd4);

	// A volume can be defined by the planes formed by the end points of the QuadIntersectionData (similar to a pyramid)
	// The points of the planes are ordered so that their normals point towards the inside of the volume (using left-hand rule)
	TArray<FPlane> Planes;
	if (QuadIntersectionData.bIsPerspectiveView)
	{
		// For very small window selection (points very close together), it's possible to get a null-normal plane because of
		// very small vectors, so use this alternate computation that will normalize the vectors before computing the plane normal
		FVector Normal1 = (Origin3 - Origin).GetSafeNormal();
		FVector Normal2 = (Origin2 - Origin).GetSafeNormal();
		if (Normal1 != FVector::ZeroVector && Normal2 != FVector::ZeroVector)
		{
			FVector PlaneNormal = (Normal1 ^ Normal2).GetSafeNormal();
			if (PlaneNormal != FVector::ZeroVector)
			{
				Planes.Add(FPlane(Origin, PlaneNormal));			// near plane
			}
		}
	}
	Planes.Add(FPlane(Origin, TopLeft, BottomLeft));		// left plane
	Planes.Add(FPlane(Origin2, TopRight, TopLeft));			// top plane
	Planes.Add(FPlane(Origin3, BottomLeft, BottomRight));	// bottom plane
	Planes.Add(FPlane(Origin4, BottomRight, TopRight));		// right plane

	const FVector CameraLocation = InvRenderMatrix.TransformPosition(QuadIntersectionData.CameraToWorld.GetLocation());

	return FindPolygonsInVolume(*EditableMesh, Planes, CameraLocation, QuadIntersectionData.bIncludeBackfaces);
}

#undef LOCTEXT_NAMESPACE