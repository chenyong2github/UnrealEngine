// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenNurbsBRepToCADKernelConverter.h"

#ifdef USE_OPENNURBS
#include "CoreTechSurfaceHelper.h"

#pragma warning(push)
#pragma warning(disable:4265)
#pragma warning(disable:4005) // TEXT macro redefinition
#include "opennurbs.h"
#pragma warning(pop)

#include <vector>

#include "CADKernelTools.h"

#include "CADKernel/Core/Session.h"
#include "CADKernel/Geo/Curves/NURBSCurve.h"
#include "CADKernel/Geo/Surfaces/NURBSSurface.h"
#include "CADKernel/Geo/Surfaces/Surface.h"

#include "CADKernel/Math/Boundary.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Mesh/Meshers/ParametricMesher.h"
#include "CADKernel/Mesh/Structure/ModelMesh.h"

#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLoop.h"


using namespace CADLibrary;
using namespace CADKernel;

namespace
{
	enum EAxis { U, V };
	struct PerAxisInfo
	{
		ON_NurbsSurface& OpenNurbsSurface;
		EAxis Axis;
		int32& Degree; // Degree + 1
		TArray<double>& Knots; // t values with superflux values
		int32& CtrlVertCount; // number of control points

		uint32 KnotSize;  // ON knots
		uint32 KnotCount; // CT knots

		TArray<uint32> KnotMultiplicities; // from ON, not relevant as we send n-plicated knots to CT (dbg only)

		PerAxisInfo(EAxis InAxis, ON_NurbsSurface& InSurface, FNurbsSurfaceHomogeneousData& NurbsInfo)
			: OpenNurbsSurface(InSurface)
			, Axis(InAxis)
			, Degree(Axis == U ? NurbsInfo.UDegree : NurbsInfo.VDegree)
			, Knots(Axis == U ? NurbsInfo.UNodalVector : NurbsInfo.VNodalVector)
			, CtrlVertCount(Axis == U ? NurbsInfo.PoleUCount : NurbsInfo.PoleVCount)
		{
			// detect cases not handled by CADKernel, that is knot vectors with multiplicity < order on either end
			Degree = OpenNurbsSurface.Order(Axis) - 1;
			if (OpenNurbsSurface.KnotMultiplicity(Axis, 0) < Degree || OpenNurbsSurface.KnotMultiplicity(Axis, KnotSize - 3) < Degree)
			{
				OpenNurbsSurface.IncreaseDegree(Axis, OpenNurbsSurface.Degree(Axis) + 1);
			}
			Populate();
		}

	private:
		void Populate()
		{
			Degree = OpenNurbsSurface.Order(Axis) - 1;
			CtrlVertCount = OpenNurbsSurface.CVCount(Axis);
			KnotSize = Degree + CtrlVertCount + 1;
			KnotCount = OpenNurbsSurface.KnotCount(Axis);

			KnotMultiplicities.SetNumUninitialized(KnotSize - 2);
			for (uint32 Index = 0; Index < KnotSize - 2; ++Index)
			{
				KnotMultiplicities[Index] = OpenNurbsSurface.KnotMultiplicity(Axis, Index); // 0 and < Order + CV_count - 2
			}

			Knots.Reserve(KnotSize);
			Knots.Add(OpenNurbsSurface.SuperfluousKnot(Axis, 0));
			for (uint32 i = 0; i < KnotCount; ++i)
			{
				Knots.Add(OpenNurbsSurface.Knot(Axis, i));
			}
			Knots.Add(OpenNurbsSurface.SuperfluousKnot(Axis, 1));
		}
	};
}

TSharedRef<FSurface> FOpenNurbsBRepToCADKernelConverter::AddSurface(ON_NurbsSurface& OpenNurbsSurface)
{
	FNurbsSurfaceHomogeneousData NurbsData;

	int32 ControlVertexDimension = OpenNurbsSurface.CVSize();

	PerAxisInfo UInfo(U, OpenNurbsSurface, NurbsData);
	PerAxisInfo VInfo(V, OpenNurbsSurface, NurbsData);

	NurbsData.HomogeneousPoles.SetNumUninitialized(UInfo.CtrlVertCount * VInfo.CtrlVertCount * OpenNurbsSurface.CVSize());
	double* ControlPoints = NurbsData.HomogeneousPoles.GetData();
	NurbsData.bIsRational = OpenNurbsSurface.IsRational();
	ON::point_style PointStyle = OpenNurbsSurface.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational;
	for (int32 UIndex = 0; UIndex < UInfo.CtrlVertCount; ++UIndex)
	{
		for (int32 VIndex = 0; VIndex < VInfo.CtrlVertCount; ++VIndex, ControlPoints += ControlVertexDimension)
		{
			OpenNurbsSurface.GetCV(UIndex, VIndex, PointStyle, ControlPoints);
		}
	}

#ifdef REMOVE_NEGATIVE_WEIGHT
	if (NurbsData.bIsRational)
	{
		bool bHasNegativeWeight = false;
		for (int32 Index = 3; Index < NurbsData.HomogeneousPoles.Num(); Index += 4)
		{
			double Weight = ControlPoints[Index];
			if (Weight < 0.)
			{
				bHasNegativeWeight = true;
				break;
			}
		}

		if (bHasNegativeWeight)
		{
			UInfo.IncreaseDegree();
			VInfo.IncreaseDegree();
			BuildHull();
		}
	}
#endif

	return FEntity::MakeShared<FNURBSSurface>(GeometricTolerance, NurbsData);
}

TSharedPtr<FTopologicalLoop> FOpenNurbsBRepToCADKernelConverter::AddLoop(const ON_BrepLoop& OpenNurbsLoop, TSharedRef<FSurface>& CarrierSurface)
{
	if (!OpenNurbsLoop.IsValid())
	{
		return TSharedPtr<FTopologicalLoop>();
	}

	ON_BrepLoop::TYPE LoopType = OpenNurbsLoop.m_type;
	bool bIsOuter = (LoopType == ON_BrepLoop::TYPE::outer);

	int32 EdgeCount = OpenNurbsLoop.TrimCount();
	TArray<TSharedPtr<FTopologicalEdge>> Edges;
	TArray<CADKernel::EOrientation> Directions;

	Edges.Reserve(EdgeCount);
	Directions.Reserve(EdgeCount);

	for (int32 Index = 0; Index < EdgeCount; ++Index)
	{
		ON_BrepTrim& Trim = *OpenNurbsLoop.Trim(Index);

		TSharedPtr<FTopologicalEdge> Edge = AddEdge(Trim, CarrierSurface);
		if (Edge.IsValid())
		{
			Edges.Add(Edge);
			Directions.Emplace(CADKernel::EOrientation::Front);
		}
	}

	if (Edges.Num() == 0)
	{
		return TSharedPtr<FTopologicalLoop>();
	}

	return FTopologicalLoop::Make(Edges, Directions, GeometricTolerance);
}

TSharedPtr<FTopologicalEdge> FOpenNurbsBRepToCADKernelConverter::AddEdge(const ON_BrepTrim& OpenNurbsTrim, TSharedRef<FSurface>& CarrierSurface)
{
	ON_BrepEdge* OpenNurbsEdge = OpenNurbsTrim.Edge();
	if (OpenNurbsEdge == nullptr)
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	ON_NurbsCurve OpenNurbsCurve;
	int NurbFormSuccess = OpenNurbsTrim.GetNurbForm(OpenNurbsCurve); // 0:Nok 1:Ok 2:OkBut
	if (NurbFormSuccess == 0)
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	FNurbsCurveData NurbsCurveData;

	NurbsCurveData.Dimension = 2;
	NurbsCurveData.Degree = OpenNurbsCurve.Order() - 1;

	int32 KnotCount = OpenNurbsCurve.KnotCount();
	int32 ControlPointCount = OpenNurbsCurve.CVCount();

	NurbsCurveData.NodalVector.Reserve(KnotCount + 2);
	NurbsCurveData.NodalVector.Emplace(OpenNurbsCurve.SuperfluousKnot(0));
	for (int Index = 0; Index < KnotCount; ++Index)
	{
		NurbsCurveData.NodalVector.Emplace(OpenNurbsCurve.Knot(Index));
	}
	NurbsCurveData.NodalVector.Emplace(OpenNurbsCurve.SuperfluousKnot(1));

	NurbsCurveData.bIsRational = OpenNurbsCurve.IsRational();

	NurbsCurveData.Poles.SetNumUninitialized(ControlPointCount);

	double* ControlPoints = (double*) NurbsCurveData.Poles.GetData();
	for (int32 Index = 0; Index < ControlPointCount; ++Index, ControlPoints += 3)
	{
		OpenNurbsCurve.GetCV(Index, OpenNurbsCurve.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational, ControlPoints);
	}

	if(NurbsCurveData.bIsRational)
	{
		NurbsCurveData.Weights.SetNumUninitialized(ControlPointCount);
		for (int32 Index = 0; Index < ControlPointCount; ++Index)
		{
			NurbsCurveData.Weights[Index] = NurbsCurveData.Poles[Index].Z;
		}
	}

	for (int32 Index = 0; Index < ControlPointCount; ++Index)
	{
		NurbsCurveData.Poles[Index].Z = 0;
	}

	TSharedRef<FNURBSCurve> Nurbs = FEntity::MakeShared<FNURBSCurve>(NurbsCurveData);

	TSharedRef<FRestrictionCurve> RestrictionCurve = FEntity::MakeShared<FRestrictionCurve>(CarrierSurface, Nurbs);

	ON_Interval dom = OpenNurbsCurve.Domain();
	FLinearBoundary Boundary(dom.m_t[0], dom.m_t[1]);
	TSharedPtr<FTopologicalEdge> Edge = FTopologicalEdge::Make(RestrictionCurve, Boundary);
	if (!Edge.IsValid())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	// Build topo i.e. find in the twin trims the the first edge and link both edges
	for (int32 Index = 0; Index < OpenNurbsEdge->m_ti.Count(); ++Index)
	{
		int32 LinkedEdgeId = OpenNurbsEdge->m_ti[Index];
		if (LinkedEdgeId == OpenNurbsTrim.m_trim_index)
		{
			continue;
		}

		TSharedPtr<CADKernel::FTopologicalEdge>* TwinEdge = OpenNurbsTrimId2CADKernelEdge.Find(LinkedEdgeId);
		if (TwinEdge)
		{
			Edge->Link(TwinEdge->ToSharedRef(), SquareTolerance);
			break;
		}
	}
	OpenNurbsTrimId2CADKernelEdge.Add(OpenNurbsTrim.m_trim_index, Edge);

	return Edge;
}

TSharedPtr<FTopologicalFace> FOpenNurbsBRepToCADKernelConverter::AddFace(const ON_BrepFace& OpenNurbsFace)
{

	ON_NurbsSurface OpenNurbsSurface;
	OpenNurbsFace.NurbsSurface(&OpenNurbsSurface);

	TSharedRef<FSurface> Surface = AddSurface(OpenNurbsSurface);

	TSharedRef<FTopologicalFace> Face = FEntity::MakeShared<FTopologicalFace>(Surface);

	const ON_BrepLoop* OuterLoop = OpenNurbsFace.OuterLoop();
	if (OuterLoop == nullptr)
	{
		Face->ApplyNaturalLoops();
		return Face;
	}

	int LoopCount = OpenNurbsFace.LoopCount();
	for (int LoopIndex = 0; LoopIndex < LoopCount; ++LoopIndex)
	{
		const ON_BrepLoop& OpenNurbsLoop = *OpenNurbsFace.Loop(LoopIndex);
		TSharedPtr<FTopologicalLoop> Loop = AddLoop(OpenNurbsLoop, Surface);
		if(Loop.IsValid())
		{
			Face->AddLoop(Loop);
		}
	}

	return Face;
}

bool FOpenNurbsBRepToCADKernelConverter::AddBRep(ON_Brep& BRep, const ON_3dVector& Offset)
{
	OpenNurbsTrimId2CADKernelEdge.Empty();

	TSharedRef<FBody> Body = FEntity::MakeShared<FBody>();
	TSharedRef<FShell> Shell = FEntity::MakeShared<FShell>();
	Body->AddShell(Shell);

	BRep.Translate(Offset);

	BRep.FlipReversedSurfaces();

	// Create faces
	int FaceCount = BRep.m_F.Count();
	for (int index = 0; index < FaceCount; index++)
	{
		const ON_BrepFace& OpenNurbsFace = BRep.m_F[index];
		TSharedPtr<FTopologicalFace> Face = AddFace(OpenNurbsFace);
		if (Face.IsValid())
		{
			Shell->Add(Face.ToSharedRef(), CADKernel::EOrientation::Front);
		}
	}

	BRep.Translate(-Offset);

	CADKernelSession.GetModel()->Add(Body);

	return true;
}

#endif // defined(USE_OPENNURBS)
