// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/UI/Visu.h"

class FString;
namespace CADKernel
{
	class FAABB;
	class FAABB2D;
	class FBody;
	class FCurve;
	class FRestrictionCurve;
	class FTopologicalEdge;
	class FEdgeMesh;
	class FEntity;
	class FGroup;
	class FMesh;

	class FVertexMesh;
	class FEdgeMesh;
	class FFaceMesh;
	class FModelMesh;

	class FModel;
	class FShell;
	class FSurface;
	class FFaceMesh;
	class FTopologicalFace;
	class FTopologicalLoop;
	class FTopologicalVertex;
	class FVertexMesh;

	struct FLinearBoundary;

	void Wait(bool bMakeWait = true);

	void Open3DDebugSession(FString name, const TArray<FIdent>& idList = TArray<FIdent>());
	void Close3DDebugSession();

	class CADKERNEL_API F3DDebugSession
	{
	public :
		F3DDebugSession(FString name, const TArray<FIdent>& Idents = TArray<FIdent>())
		{
			Open3DDebugSession(name, Idents);
		}

		~F3DDebugSession()
		{
			Close3DDebugSession();
		}
	};

	CADKERNEL_API void Open3DDebugSegment(FIdent Ident);
	CADKERNEL_API void Close3DDebugSegment();

	class CADKERNEL_API F3DDebugSegment
	{
	public:
		F3DDebugSegment(FIdent Ident)
		{
			Open3DDebugSegment(Ident);
		}

		~F3DDebugSegment()
		{
			Close3DDebugSegment();
		}
	};

	void CADKERNEL_API FlushVisu();

	template<typename TPoint>
	void DrawPoint(const TPoint& InPoint, EVisuProperty Property = EVisuProperty::BluePoint)
	{
		FSystem::Get().GetVisu()->DrawPoint(InPoint, Property);
	}

	/**
	 * Draw a mesh element i.e. Element of dimension 1 is an edge, of dimension 2 is a triangle or quadrangle, of dimension 3 is tetrahedron, pyramid, hexahedron, ...
	 */
	CADKERNEL_API void DrawElement(int32 Dimension, TArray<FPoint>& Points, EVisuProperty Property = EVisuProperty::Element);

	template<typename TPoint>
	void Draw(const TArray<TPoint>& Points, EVisuProperty Property = EVisuProperty::BlueCurve)
	{
		FSystem::Get().GetVisu()->DrawPolyline(Points, Property);
	}

	CADKERNEL_API void DrawMesh(const TSharedPtr<FMesh>& mesh);

	CADKERNEL_API void DisplayEdgeCriteriaGrid(int32 EdgeId, const TArray<FPoint>& Points3D);

	template<typename TPoint>
	void DisplayPoint(const TPoint& Point, FIdent Ident)
	{
		F3DDebugSegment G(Ident);
		DrawPoint(Point);
	}

	template<typename TPoint>
	void DisplayPoint(const TPoint& Point, EVisuProperty Property = EVisuProperty::BluePoint)
	{
		DrawPoint(Point, Property);
	}

	template<typename TPoint>
	void DisplayPoint(const TPoint& Point, EVisuProperty Property, FIdent Ident)
	{
		F3DDebugSegment G(Ident);
		DrawPoint(Point, Property);
	}

	CADKERNEL_API void DisplayProductTree(const TSharedPtr<FEntity>& RootId);
	CADKERNEL_API void DisplayProductTree(const TSharedPtr<FModel>& Model);
	CADKERNEL_API void DisplayProductTree(const TSharedPtr<FBody>& Body);
	CADKERNEL_API void DisplayProductTree(const TSharedPtr<FShell>& Shell);

	CADKERNEL_API void DisplayAABB(const FAABB& aabb, FIdent Ident = 0);
	CADKERNEL_API void DisplayAABB2D(const FAABB2D& aabb, FIdent Ident = 0);

	CADKERNEL_API void DisplayEntity(const TSharedPtr<FEntity>& Entity);
	CADKERNEL_API void DisplayEntity2D(const TSharedPtr<FEntity>& Entity);

	CADKERNEL_API void DisplayLoop(const TSharedPtr<FTopologicalFace>& Entity);
	CADKERNEL_API void DisplayIsoCurve(const TSharedPtr<FSurface>& CarrierSurface, double Coordinate, EIso Type);

	CADKERNEL_API void Display(const FPlane& plane, FIdent Ident = 0);

	CADKERNEL_API void Display(const TSharedPtr<FCurve>& Curve);
	CADKERNEL_API void Display(const TSharedPtr<FSurface>& CarrierSurface);

	CADKERNEL_API void Display(const TSharedPtr<FGroup>& Group);
	CADKERNEL_API void Display(const TSharedPtr<FModel>& Model);
	CADKERNEL_API void Display(const TSharedPtr<FBody>& Body);
	CADKERNEL_API void Display(const TSharedPtr<FShell>& Shell);
	CADKERNEL_API void Display(const TSharedPtr<FTopologicalEdge>& Edge, EVisuProperty Property = EVisuProperty::BlueCurve);
	CADKERNEL_API void Display(const TSharedPtr<FTopologicalFace>& Face);
	CADKERNEL_API void Display(const TSharedPtr<FTopologicalLoop>& Loop);
	CADKERNEL_API void Display(const TSharedPtr<FTopologicalVertex>& Vertex);

	CADKERNEL_API void Display2D(const TSharedPtr<FTopologicalEdge>& Edge, EVisuProperty Property = EVisuProperty::BlueCurve);
	CADKERNEL_API void Display2D(const TSharedPtr<FTopologicalFace>& Face);
	CADKERNEL_API void Display2D(const TSharedPtr<FTopologicalLoop>& Loop);
	CADKERNEL_API void Display2D(const TSharedPtr<FSurface>& CarrierSurface);

	CADKERNEL_API void DisplayMesh(const TSharedPtr<FMesh>& Mesh);
	CADKERNEL_API void DisplayMesh(const TSharedRef<FFaceMesh>& Mesh);
	CADKERNEL_API void DisplayMesh(const TSharedRef<FEdgeMesh>& Mesh);
	CADKERNEL_API void DisplayMesh(const TSharedRef<FVertexMesh>& Mesh);

	CADKERNEL_API void Display(const TSharedPtr<FModelMesh>& MeshModel);

	CADKERNEL_API void DisplayControlPolygon(const TSharedPtr<FCurve>& Entity);
	CADKERNEL_API void DisplayControlPolygon(const TSharedPtr<FSurface>& Entity);

	template<typename TPoint>
	void DisplaySegment(const TPoint& Point1, const TPoint& Point2, FIdent Ident = 0, EVisuProperty Property = EVisuProperty::Element, bool bWithOrientation = false)
	{
		F3DDebugSegment G(Ident);
		if (bWithOrientation)
		{
			DrawSegmentOrientation(Point1, Point2, Property);
		}
		DrawSegment(Point1, Point2, Property);
	};

	template<typename TPoint>
	void DisplayPolyline(const TArray<TPoint>& Points, EVisuProperty Property)
	{
		Open3DDebugSegment(0);
		Draw(Points, Property);
		Close3DDebugSegment();
	}

	CADKERNEL_API void DrawQuadripode(double Height, double Base, FPoint& Centre, FPoint& Direction);

	CADKERNEL_API void Draw(const TSharedPtr<FTopologicalEdge>& Edge, EVisuProperty Property = EVisuProperty::BlueCurve);
	CADKERNEL_API void Draw(const TSharedPtr<FTopologicalFace>& Face);
	CADKERNEL_API void Draw2D(const TSharedPtr<FTopologicalFace>& Face);
	CADKERNEL_API void Draw(const TSharedPtr<FShell>& Shell);

	CADKERNEL_API void Draw(const TSharedPtr<FCurve>& Curve, EVisuProperty Property = EVisuProperty::BlueCurve);
	CADKERNEL_API void Draw(const TSharedPtr<FCurve>& Curve, const FLinearBoundary& Boundary, EVisuProperty Property = EVisuProperty::BlueCurve);
	CADKERNEL_API void Draw(const FLinearBoundary& Boundary, const TSharedPtr<FRestrictionCurve>& Curve, EVisuProperty Property = EVisuProperty::BlueCurve);

	template<typename TPoint>
	void DrawSegment(const TPoint& Point1, const TPoint& Point2, EVisuProperty Property = EVisuProperty::Element)
	{
		TArray<FPoint> Points;
		Points.Add(Point1);
		Points.Add(Point2);
		Draw(Points, Property);
	}

	template<typename TPoint>
	void DrawSegmentOrientation(const TPoint& Point1, const TPoint& Point2, EVisuProperty Property = EVisuProperty::Element)
	{
		double Length = Point1.Distance(Point2);
		double Height = Length / 10.0;
		double Base = Height / 2;

		FPoint Middle = (Point1 + Point2) / 2.;
		FPoint Tangent = Point2 - Point1;
		DrawQuadripode(Height, Base, Middle, Tangent);
	}

	CADKERNEL_API void DrawIsoCurves(const TSharedPtr<FTopologicalFace>& Face);


} // namespace CADKernel

