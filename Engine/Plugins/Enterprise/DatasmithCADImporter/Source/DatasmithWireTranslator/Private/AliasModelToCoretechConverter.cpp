// Copyright Epic Games, Inc. All Rights Reserved.

#include "AliasModelToCoretechConverter.h"

#include "Hal/PlatformMemory.h"

#include "CoreTechSurfaceHelper.h"
#include "CoreTechTypes.h"

#ifdef USE_OPENMODEL

// Alias API wrappes object in AlObjects. This is an abstract base class which holds a reference to an anonymous data structure.
// The only way to compare two AlObjects is to compare their data structure. That is the reason why private fields are made public. 
// see FAliasCoretechWrapper::AddTrimCurve
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

namespace UE_DATASMITHWIRETRANSLATOR_NAMESPACE
{

namespace AliasToCoreTechUtils
{
	template<typename Surface_T>
	uint64 CreateCTNurbs(Surface_T& Surface, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix)
	{
		CADLibrary::FNurbsSurface CTSurface;

		CTSurface.ControlPointDimension = 4;  // Control Hull Dimension (3 for non-rational or 4 for rational)

		CTSurface.ControlPointSizeU = Surface.uNumberOfCVsInclMultiples();
		CTSurface.ControlPointSizeV = Surface.vNumberOfCVsInclMultiples();

		CTSurface.OrderU = Surface.uDegree() + 1;  // U order of the surface
		CTSurface.OrderV = Surface.vDegree() + 1;  // V order of the surface

		CTSurface.KnotSizeU = Surface.realuNumberOfKnots() + 2;
		CTSurface.KnotSizeV = Surface.realvNumberOfKnots() + 2;

		CTSurface.KnotValuesU.SetNumUninitialized(CTSurface.KnotSizeU);
		CTSurface.KnotValuesV.SetNumUninitialized(CTSurface.KnotSizeV);

		Surface.realuKnotVector(CTSurface.KnotValuesU.GetData() + 1);
		Surface.realvKnotVector(CTSurface.KnotValuesV.GetData() + 1);

		CTSurface.KnotMultiplicityU.Init(1, CTSurface.KnotSizeU);
		CTSurface.KnotMultiplicityV.Init(1, CTSurface.KnotSizeV);

		CTSurface.KnotValuesU[0] = CTSurface.KnotValuesU[1];
		CTSurface.KnotValuesV[0] = CTSurface.KnotValuesV[1];
		CTSurface.KnotValuesU[CTSurface.KnotSizeU - 1] = CTSurface.KnotValuesU[CTSurface.KnotSizeU - 2];
		CTSurface.KnotValuesV[CTSurface.KnotSizeV - 1] = CTSurface.KnotValuesV[CTSurface.KnotSizeV - 2];

		const int32 NbCoords = CTSurface.ControlPointSizeU * CTSurface.ControlPointSizeV * CTSurface.ControlPointDimension;
		CTSurface.ControlPoints.SetNumUninitialized(NbCoords);

		if (InObjectReference == EAliasObjectReference::WorldReference)
		{
			Surface.CVsWorldPositionInclMultiples(CTSurface.ControlPoints.GetData());
		}
		else if (InObjectReference == EAliasObjectReference::ParentReference)
		{
			AlTM TranformMatrix(InAlMatrix);
			Surface.CVsAffectedPositionInclMultiples(TranformMatrix, CTSurface.ControlPoints.GetData());
		}
		else  // EAliasObjectReference::LocalReference
		{
			Surface.CVsUnaffectedPositionInclMultiples(CTSurface.ControlPoints.GetData());
		}

		uint64 CTSurfaceID = 0;
		return CTKIO_CreateNurbsSurface(CTSurface, CTSurfaceID) ? CTSurfaceID : 0;
	}
}

uint64 FAliasModelToCoretechConverter::AddTrimCurve(const AlTrimCurve& TrimCurve)
{
	CADLibrary::FNurbsCurve CTCurve;

	CTCurve.Order = TrimCurve.degree() + 1;
	CTCurve.ControlPointSize = TrimCurve.numberOfCVs();
	CTCurve.ControlPointDimension = 3;
	CTCurve.KnotSize = TrimCurve.realNumberOfKnots() + 2;

	CTCurve.ControlPoints.SetNumUninitialized(CTCurve.ControlPointSize * CTCurve.ControlPointDimension);
	CTCurve.KnotValues.SetNumUninitialized(CTCurve.KnotSize);

	using UVPoint2 = double[3];
	TrimCurve.CVsUVPosition(CTCurve.KnotValues.GetData(), (UVPoint2*)CTCurve.ControlPoints.GetData());

	CTCurve.KnotMultiplicity.Init(1, CTCurve.KnotSize);

	TrimCurve.realKnotVector(CTCurve.KnotValues.GetData() + 1);

	CTCurve.KnotValues[0] = CTCurve.KnotValues[1];
	CTCurve.KnotValues[CTCurve.KnotSize - 1] = CTCurve.KnotValues[CTCurve.KnotSize - 2];

	uint64 CoedgeID = 0;
	if (CADLibrary::CTKIO_CreateCoedge(CTCurve, (bool) TrimCurve.isReversed(), CoedgeID))
	{
		// Build topo
		if (AlTrimCurve *TwinCurve = TrimCurve.getTwinCurve())
		{
			if (uint64* TwinCoedgeID = AlEdge2CTEdge.Find(TwinCurve->fSpline))
			{
				CADLibrary::CTKIO_MatchCoedges(*TwinCoedgeID, CoedgeID);
			}
			// only TrimCurve with twin need to be in the map
			AlEdge2CTEdge.Add(TrimCurve.fSpline, CoedgeID);
		}
		return CoedgeID;
	}

	return 0;
}

uint64 FAliasModelToCoretechConverter::AddTrimBoundary(const AlTrimBoundary& TrimBoundary)
{
	TArray<uint64> Edges;
	for(TUniquePtr<AlTrimCurve> TrimCurve(TrimBoundary.firstCurve()); TrimCurve.IsValid(); TrimCurve = TUniquePtr<AlTrimCurve>(TrimCurve->nextCurve()))
	{
		if (uint64 CoedgeID = AddTrimCurve(*TrimCurve))
		{
			Edges.Add(CoedgeID);
		}
	}

	uint64 LoopID;
	return CADLibrary::CTKIO_CreateLoop(Edges, LoopID) ? LoopID : 0;
}

uint64 FAliasModelToCoretechConverter::AddTrimRegion(const AlTrimRegion& TrimRegion, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation)
{
	uint64 SurfaceID = AliasToCoreTechUtils::CreateCTNurbs(TrimRegion, InObjectReference, InAlMatrix);
	if (SurfaceID == 0)
	{
		return 0;
	}

	TArray<uint64> Boundaries;
	for (TUniquePtr<AlTrimBoundary> TrimBoundary(TrimRegion.firstBoundary()); TrimBoundary.IsValid(); TrimBoundary = TUniquePtr<AlTrimBoundary>(TrimBoundary->nextBoundary()))
	{
		if (uint64 LoopId = AddTrimBoundary(*TrimBoundary))
		{
			Boundaries.Add(LoopId);
		}
	}

	uint64 FaceID;
	return CADLibrary::CTKIO_CreateFace(SurfaceID, bInOrientation, Boundaries, FaceID) ? FaceID : 0;
}

void FAliasModelToCoretechConverter::AddFace(const AlSurface& Surface, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceList)
{
	TUniquePtr<AlTrimRegion> TrimRegion(Surface.firstTrimRegion());
	if (TrimRegion.IsValid())
	{
		for (; TrimRegion.IsValid(); TrimRegion = TUniquePtr<AlTrimRegion>(TrimRegion->nextRegion()))
		{
			if (uint64 FaceID = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation))
			{
				OutFaceList.Add(FaceID);
			}
		}
		return;
	}

	uint64 SurfaceID = AliasToCoreTechUtils::CreateCTNurbs(Surface, InObjectReference, InAlMatrix);

	uint64 FaceID;
	if (CADLibrary::CTKIO_CreateFace(SurfaceID, bInOrientation, TArray<uint64>(), FaceID) && FaceID != 0)
	{
		OutFaceList.Add(FaceID);
	}
}

void FAliasModelToCoretechConverter::AddShell(const AlShell& Shell, EAliasObjectReference InObjectReference, const AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceList)
{
	for (TUniquePtr<AlTrimRegion> TrimRegion(Shell.firstTrimRegion()); TrimRegion.IsValid(); TrimRegion = TUniquePtr<AlTrimRegion>(TrimRegion->nextRegion()))
	{
		if (uint64 FaceID = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation))
		{
			OutFaceList.Add(FaceID);
		}
	}
}

bool FAliasModelToCoretechConverter::AddBRep(AlDagNode& DagNode, const FColor& Color, EAliasObjectReference InObjectReference)
{
	TArray<uint64> FaceList;

	AlEdge2CTEdge.Empty();

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
			if (AlIsValid(ShellPtr))
			{
				TUniquePtr<AlShell> Shell(ShellPtr);
				AddShell(*ShellPtr, InObjectReference, AlMatrix, bOrientation, FaceList);
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
				TUniquePtr<AlSurface> Surface(SurfacePtr);
				AddFace(*SurfacePtr, InObjectReference, AlMatrix, bOrientation, FaceList);
			}
		}
		break;
	}

	default:
		break;

	}

	if (FaceList.Num() == 0)
	{
		return false;
	}

	// Create body from faces
	uint64 BodyID;
	if (CADLibrary::CTKIO_CreateBody(FaceList, BodyID))
	{
		// Setup parenting
		return CADLibrary::CTKIO_AddBodies({ BodyID }, MainObjectId) ? true : false;
	}

	return false;
}

}
#endif
