// Copyright Epic Games, Inc. All Rights Reserved.

#include "AliasModelToCADKernelConverter.h"

#include "CADKernelTools.h"
#include "Hal/PlatformMemory.h"
#include "MeshDescriptionHelper.h"

#include "CADKernel/Core/Session.h"
#include "CADKernel/Geo/Curves/NURBSCurve.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Geo/Surfaces/NURBSSurface.h"
#include "CADKernel/Geo/Surfaces/Surface.h"

#include "CADKernel/Math/Point.h"
#include "CADKernel/Mesh/Meshers/ParametricMesher.h"
#include "CADKernel/Mesh/Structure/ModelMesh.h"

#include "CADKernel/Topo/Body.h"
//#include "CADKernel/Topo/Joiner.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLoop.h"

#ifdef USE_OPENMODEL

// Alias API wrappes object in AlObjects. This is an abstract base class which holds a reference to an anonymous data structure.
// The only way to compare two AlObjects is to compare their data structure. That is the reason why private fields are made public. 
#define private public
#include "AlCurve.h"
#include "AlDagNode.h"
#include "AlShell.h"
#include "AlShellNode.h"
#include "AlSurface.h"
#include "AlSurfaceNode.h"
#include "AlTrimBoundary.h"
#include "AlTrimCurve.h"
#include "AlTrimRegion.h"
#include "AlTM.h"

using namespace CADKernel;

namespace UE_DATASMITHWIRETRANSLATOR_NAMESPACE
{

namespace AliasToCADKernelUtils
{

	template<typename Surface_T>
	TSharedPtr<FSurface> AddNURBSSurface(double GeometricTolerance, Surface_T& AliasSurface, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix)
	{
		FNurbsSurfaceHomogeneousData NURBSData;
		NURBSData.bSwapUV= true;
		NURBSData.bIsRational = true;

		NURBSData.PoleUCount = AliasSurface.uNumberOfCVsInclMultiples();
		NURBSData.PoleVCount = AliasSurface.vNumberOfCVsInclMultiples();

		NURBSData.UDegree = AliasSurface.uDegree();  // U order of the surface
		NURBSData.VDegree = AliasSurface.vDegree();  // V order of the surface

		int32 KnotSizeU = AliasSurface.realuNumberOfKnots() + 2;
		int32 KnotSizeV = AliasSurface.realvNumberOfKnots() + 2;

		NURBSData.UNodalVector.SetNumUninitialized(KnotSizeU);
		NURBSData.VNodalVector.SetNumUninitialized(KnotSizeV);

		AliasSurface.realuKnotVector(NURBSData.UNodalVector.GetData() + 1);
		AliasSurface.realvKnotVector(NURBSData.VNodalVector.GetData() + 1);

		NURBSData.UNodalVector[0] = NURBSData.UNodalVector[1];
		NURBSData.UNodalVector[KnotSizeU - 1] = NURBSData.UNodalVector[KnotSizeU - 2];
		NURBSData.VNodalVector[0] = NURBSData.VNodalVector[1];
		NURBSData.VNodalVector[KnotSizeV - 1] = NURBSData.VNodalVector[KnotSizeV - 2];

		const int32 CoordinateCount = NURBSData.PoleUCount * NURBSData.PoleVCount * 4;
		NURBSData.HomogeneousPoles.SetNumUninitialized(CoordinateCount);

		if (InObjectReference == EAliasObjectReference::WorldReference)
		{
			AliasSurface.CVsWorldPositionInclMultiples(NURBSData.HomogeneousPoles.GetData());
		}
		else if (InObjectReference == EAliasObjectReference::ParentReference)
		{
			AlTM TranformMatrix(InAlMatrix);
			AliasSurface.CVsAffectedPositionInclMultiples(TranformMatrix, NURBSData.HomogeneousPoles.GetData());
		}
		else  // EAliasObjectReference::LocalReference
		{
			AliasSurface.CVsUnaffectedPositionInclMultiples(NURBSData.HomogeneousPoles.GetData());
		}

		return FEntity::MakeShared<FNURBSSurface>(GeometricTolerance, NURBSData);
	}
}

TSharedPtr<FTopologicalEdge> FAliasModelToCADKernelConverter::AddEdge(const AlTrimCurve& AliasTrimCurve, TSharedPtr<CADKernel::FSurface>& CarrierSurface)
{
	FNurbsCurveData NurbsCurveData;

	NurbsCurveData.Degree = AliasTrimCurve.degree();
	int ControlPointCount = AliasTrimCurve.numberOfCVs();

	NurbsCurveData.Dimension = 2;
	NurbsCurveData.bIsRational = true;

	int32 KnotCount = AliasTrimCurve.realNumberOfKnots() + 2;

	NurbsCurveData.Weights.SetNumUninitialized(ControlPointCount);
	NurbsCurveData.Poles.SetNumUninitialized(ControlPointCount);
	NurbsCurveData.NodalVector.SetNumUninitialized(KnotCount);

	using AlPoint = double[3];
	// Notice that each CV has three coordinates - the three coordinates describe 2D parameter space, with a homogeneous coordinate.
	// Each control point is u, v and w, where u and v are parameter space and w is the homogeneous coordinate.
	AliasTrimCurve.CVsUVPosition(NurbsCurveData.NodalVector.GetData() + 1, (AlPoint*) NurbsCurveData.Poles.GetData());

	AliasTrimCurve.realKnotVector(NurbsCurveData.NodalVector.GetData() + 1);
	NurbsCurveData.NodalVector[0] = NurbsCurveData.NodalVector[1];
	NurbsCurveData.NodalVector[KnotCount - 1] = NurbsCurveData.NodalVector[KnotCount - 2];

	for (int32 Index = 0; Index < ControlPointCount; ++Index)
	{
		NurbsCurveData.Weights[Index] = NurbsCurveData.Poles[Index].Z;
		NurbsCurveData.Poles[Index].Z = 0;
	}

	TSharedRef<FNURBSCurve> Nurbs = FEntity::MakeShared<FNURBSCurve>(NurbsCurveData);

	TSharedRef<FRestrictionCurve> RestrictionCurve = FEntity::MakeShared<FRestrictionCurve>(CarrierSurface.ToSharedRef(), Nurbs);
	TSharedPtr<FTopologicalEdge> Edge = FTopologicalEdge::Make(RestrictionCurve);
	if (!Edge.IsValid())
	{
		return TSharedPtr<FTopologicalEdge>();
	}

	// Only TrimCurve with twin need to be in the map used in LinkEdgesLoop 
	TUniquePtr<AlTrimCurve> TwinCurve(AliasTrimCurve.getTwinCurve());
	if (TwinCurve.IsValid())
	{
		AlEdge2CADKernelEdge.Add(AliasTrimCurve.fSpline, Edge);
	}

	return Edge;
}

TSharedPtr<FTopologicalLoop> FAliasModelToCADKernelConverter::AddLoop(const AlTrimBoundary& TrimBoundary, TSharedPtr<CADKernel::FSurface>& CarrierSurface)
{
	TArray<TSharedPtr<FTopologicalEdge>> Edges;
	TArray<CADKernel::EOrientation> Directions;

	for (TUniquePtr<AlTrimCurve> TrimCurve(TrimBoundary.firstCurve()); TrimCurve.IsValid(); TrimCurve = TUniquePtr<AlTrimCurve>(TrimCurve->nextCurve()))
	{
		TSharedPtr<FTopologicalEdge> Edge = AddEdge(*TrimCurve, CarrierSurface);
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

	TSharedPtr<FTopologicalLoop> Loop = FTopologicalLoop::Make(Edges, Directions, GeometricTolerance);
	return Loop;
}

void FAliasModelToCADKernelConverter::LinkEdgesLoop(const AlTrimBoundary& TrimBoundary, CADKernel::FTopologicalLoop& Loop)
{
	for (TUniquePtr<AlTrimCurve> TrimCurve(TrimBoundary.firstCurve()); TrimCurve.IsValid(); TrimCurve = TUniquePtr<AlTrimCurve>(TrimCurve->nextCurve()))
	{
		TSharedPtr<FTopologicalEdge>* Edge = AlEdge2CADKernelEdge.Find(TrimCurve->fSpline);
		if (!Edge || !Edge->IsValid() || (*Edge)->IsDeleted() || (*Edge)->IsDegenerated())
		{
			continue;
		}

		ensure(&Loop == (*Edge)->GetLoop());

		// Link edges
		TUniquePtr<AlTrimCurve> TwinCurve(TrimCurve->getTwinCurve());
		if (TwinCurve.IsValid())
		{
			if (TSharedPtr<FTopologicalEdge>* TwinEdge = AlEdge2CADKernelEdge.Find(TwinCurve->fSpline))
			{
				if (TwinEdge->IsValid() && !(*TwinEdge)->IsDeleted() && !(*TwinEdge)->IsDegenerated())
				{
					(*Edge)->Link(**TwinEdge, SquareTolerance);
				}
			}
		}
	}
}

TSharedPtr<CADKernel::FTopologicalFace> FAliasModelToCADKernelConverter::AddTrimRegion(const AlTrimRegion& TrimRegion, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation)
{
	TSharedPtr<FSurface> Surface = AliasToCADKernelUtils::AddNURBSSurface(GeometricTolerance, TrimRegion, InObjectReference, InAlMatrix);
	if (!Surface.IsValid())
	{
		return TSharedPtr<FTopologicalFace>();
	}

	TArray<TSharedPtr<FTopologicalLoop>> Loops;
	for (TUniquePtr<AlTrimBoundary> TrimBoundary(TrimRegion.firstBoundary()); TrimBoundary.IsValid(); TrimBoundary = TUniquePtr<AlTrimBoundary>(TrimBoundary->nextBoundary()))
	{
		TSharedPtr<FTopologicalLoop> Loop = AddLoop(*TrimBoundary, Surface);
		if (Loop.IsValid())
		{
			LinkEdgesLoop(*TrimBoundary, *Loop);
			Loops.Add(Loop);
		}
	}

	if (Loops.Num() == 0)
	{
		FMessage::Printf(Log, TEXT("The Face %s is degenerate, this face is ignored\n"), TrimRegion.name());
		return TSharedPtr<FTopologicalFace>();
	}

	TSharedRef<FTopologicalFace> Face = FEntity::MakeShared<FTopologicalFace>(Surface);
	Face->SetPatchId(LastFaceId++);

	Face->AddLoops(Loops);
	return Face;
}

void FAliasModelToCADKernelConverter::AddFace(const AlSurface& Surface, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation, TSharedRef<CADKernel::FShell>& Shell)
{
	TUniquePtr<AlTrimRegion> TrimRegion(Surface.firstTrimRegion());
	if (TrimRegion.IsValid())
	{
		for (; TrimRegion.IsValid(); TrimRegion = TUniquePtr<AlTrimRegion>(TrimRegion->nextRegion()))
		{
			TSharedPtr<FTopologicalFace> Face = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation);
			if (Face.IsValid())
			{
				Shell->Add(Face.ToSharedRef(), bInOrientation ? CADKernel::EOrientation::Front : CADKernel::EOrientation::Back);
			}
		}
		return;
	}

	TSharedPtr<FSurface> CADKernelSurface = AliasToCADKernelUtils::AddNURBSSurface(GeometricTolerance, Surface, InObjectReference, InAlMatrix);
	if (CADKernelSurface.IsValid())
	{
		TSharedRef<FTopologicalFace> Face = FEntity::MakeShared<FTopologicalFace>(CADKernelSurface);
		Face->ApplyNaturalLoops();
		Shell->Add(Face, bInOrientation ? CADKernel::EOrientation::Front : CADKernel::EOrientation::Back);
	}
}

void FAliasModelToCADKernelConverter::AddShell(const AlShell& InShell, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation, TSharedRef<CADKernel::FShell>& CADKernelShell)
{
	for(TUniquePtr<AlTrimRegion> TrimRegion(InShell.firstTrimRegion()); TrimRegion.IsValid(); TrimRegion = TUniquePtr<AlTrimRegion>(TrimRegion->nextRegion()))
	{
		TSharedPtr<FTopologicalFace> Face = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation);
		if (Face.IsValid())
		{
			CADKernelShell->Add(Face.ToSharedRef(), bInOrientation ? CADKernel::EOrientation::Front : CADKernel::EOrientation::Back);
		}
	}
}

bool FAliasModelToCADKernelConverter::AddBRep(AlDagNode& DagNode, EAliasObjectReference InObjectReference)
{
	AlEdge2CADKernelEdge.Empty();

	TSharedRef<FBody> CADKernelBody = FEntity::MakeShared<FBody>();
	TSharedRef<FShell> CADKernelShell = FEntity::MakeShared<FShell>();
	CADKernelBody->AddShell(CADKernelShell);

	boolean bAlOrientation;
	DagNode.getSurfaceOrientation(bAlOrientation);
	bool bOrientation = (bool)bAlOrientation;

	AlMatrix4x4 AlMatrix;
	if (InObjectReference == EAliasObjectReference::ParentReference)
	{
		DagNode.localTransformationMatrix(AlMatrix);
	}

	AlObjectType objectType = DagNode.type();
	switch (objectType)
	{
		// Push all leaf nodes into 'leaves'
	case kShellNodeType:
	{
		if (AlShellNode* ShellNode = DagNode.asShellNodePtr())
		{
			AlShell* ShellPtr = ShellNode->shell();
			if(AlIsValid(ShellPtr))
			{
				TUniquePtr<AlShell> AliasShell(ShellPtr);
				AddShell(*ShellPtr, InObjectReference, AlMatrix, bOrientation, CADKernelShell);
			}
		}
		break;
	}

	case kSurfaceNodeType:
	{
		if (AlSurfaceNode* SurfaceNode = DagNode.asSurfaceNodePtr())
		{
			AlSurface* SurfacePtr = SurfaceNode->surface();
			if (AlIsValid(SurfacePtr))
			{
				TUniquePtr<AlSurface> AliasSurface(SurfacePtr);
				AddFace(*SurfacePtr, InObjectReference, AlMatrix, bOrientation, CADKernelShell);
			}
		}
		break;
	}
	default:
		break;
	}

	if (CADKernelShell->FaceCount() == 0)
	{
		return false;
	}

	// Create body from faces
	CADKernelSession.GetModel()->Add(CADKernelBody);
	return true;
}

}

#endif