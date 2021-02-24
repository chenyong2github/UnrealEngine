// Copyright Epic Games, Inc. All Rights Reserved.

#include "AliasCoretechWrapper.h"

#include "Hal/PlatformMemory.h"

#include "CoreTechHelper.h"
#include "CoreTechTypes.h"

#ifdef USE_OPENMODEL
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

using namespace CADLibrary;

TWeakPtr<FAliasCoretechWrapper> FAliasCoretechWrapper::SharedSession;

struct UVPoint
{
	double x;
	double y;
	double z;
};

struct UVPoint4
{
	double x;
	double y;
	double z;
	double w;
};


namespace AliasToCoreTechUtils
{
	template<typename Surface_T>
	uint64 CreateCTNurbs(Surface_T& Surface, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix)
	{
		FNurbsSurface CTSurface;

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

bool FAliasCoretechWrapper::Tessellate(FMeshDescription& Mesh, FMeshParameters& MeshParameters)
{
	// Apply stitching if applicable
	TopoFixes(1.);

	// Perform tessellation
	return CADLibrary::Tessellate(MainObjectId, ImportParams, Mesh, MeshParameters);
}

uint64 FAliasCoretechWrapper::Add3DCurve(AlCurve& Curve)
{
	FNurbsCurve CTCurve;

	curveFormType Form = Curve.form();
	CTCurve.Order = Curve.degree() + 1;
	CTCurve.ControlPointDimension = 4;
	CTCurve.ControlPointSize = Curve.numberOfCVs();
	CTCurve.KnotSize = Curve.realNumberOfKnots() + 2;

	TArray<UVPoint4> ControlPoints;
	ControlPoints.SetNumUninitialized(CTCurve.ControlPointSize);

	using UVPoint14 = double[4];
	Curve.CVsUnaffectedPositionInclMultiples((UVPoint14*)ControlPoints.GetData());

	CTCurve.ControlPoints.SetNumUninitialized(CTCurve.ControlPointSize * CTCurve.ControlPointDimension);
	int Index = 0;
	for (uint32 IndexPoint = 0; IndexPoint < CTCurve.ControlPointSize; ++IndexPoint)
	{
		CTCurve.ControlPoints[Index++] = ControlPoints[IndexPoint].x;
		CTCurve.ControlPoints[Index++] = ControlPoints[IndexPoint].y;
		CTCurve.ControlPoints[Index++] = ControlPoints[IndexPoint].z;
		CTCurve.ControlPoints[Index++] = ControlPoints[IndexPoint].w;
	}

	CTCurve.KnotMultiplicity.Init(1, CTCurve.KnotSize);

	CTCurve.KnotValues.SetNumUninitialized(CTCurve.KnotSize);
	Curve.realKnotVector(CTCurve.KnotValues.GetData() + 1);

	CTCurve.KnotValues[0] = CTCurve.KnotValues[1];
	CTCurve.KnotValues[CTCurve.KnotSize - 1] = CTCurve.KnotValues[CTCurve.KnotSize - 2];

	uint64 CurveID = 0;
	return CADLibrary::CTKIO_CreateNurbsCurve(CTCurve, CurveID) ? CurveID : 0;
}

uint64 FAliasCoretechWrapper::AddTrimCurve(AlTrimCurve& TrimCurve)
{
	FNurbsCurve CTCurve;

	curveFormType Form = TrimCurve.form();
	CTCurve.Order = TrimCurve.degree() + 1;
	CTCurve.ControlPointSize = TrimCurve.numberOfCVs();
	CTCurve.ControlPointDimension = 3;
	CTCurve.KnotSize = TrimCurve.realNumberOfKnots() + 2;

	TArray<double> Weigths;

	CTCurve.ControlPoints.SetNumUninitialized(CTCurve.ControlPointSize * CTCurve.ControlPointDimension);
	Weigths.SetNumUninitialized(CTCurve.ControlPointSize);

	using UVPoint2 = double[3];
	TrimCurve.CVsUVPosition(Weigths.GetData(), (UVPoint2*)CTCurve.ControlPoints.GetData());

	CTCurve.KnotMultiplicity.Init(1, CTCurve.KnotSize);

	CTCurve.KnotValues.SetNumUninitialized(CTCurve.KnotSize);
	TrimCurve.realKnotVector(CTCurve.KnotValues.GetData() + 1);
	CTCurve.KnotValues[0] = CTCurve.KnotValues[1];
	CTCurve.KnotValues[CTCurve.KnotSize - 1] = CTCurve.KnotValues[CTCurve.KnotSize - 2];

	boolean bIsReversed = TrimCurve.isReversed();

	uint64 CoedgeID = 0;
	if (CTKIO_CreateCoedge(CTCurve, (bool)TrimCurve.isReversed(), CoedgeID))
	{
		// Build topo
		if (AlTrimCurve *TwinCurve = TrimCurve.getTwinCurve())
		{
			if (uint64 *TwinCoedgeID = AlEdge2CTEdge.Find(TwinCurve))
			{
				CTKIO_MatchCoedges(*TwinCoedgeID, CoedgeID);
			}

			AlEdge2CTEdge.Add(&TrimCurve, CoedgeID);
		}

		return CoedgeID;
	}

	return 0;
}

uint64 FAliasCoretechWrapper::AddTrimBoundary(AlTrimBoundary& TrimBoundary)
{
	AlTrimCurve *TrimCurve = TrimBoundary.firstCurve();
	TArray<uint64> Edges;
	while (TrimCurve)
	{
		if (uint64 CoedgeID = AddTrimCurve(*TrimCurve))
		{
			Edges.Add(CoedgeID);
		}

		TrimCurve = TrimCurve->nextCurve();
	}

	uint64 LoopID;
	return CTKIO_CreateLoop(Edges, LoopID) ? LoopID : 0;
}

uint64 FAliasCoretechWrapper::AddTrimRegion(AlTrimRegion& TrimRegion, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation)
{
	uint64 SurfaceID = AliasToCoreTechUtils::CreateCTNurbs(TrimRegion, InObjectReference, InAlMatrix);
	if (SurfaceID == 0)
	{
		return 0;
	}

	TArray<uint64> Boundaries;
	AlTrimBoundary *TrimBoundary = TrimRegion.firstBoundary();

	while (TrimBoundary)
	{
		uint64 LoopId = AddTrimBoundary(*TrimBoundary);
		if (LoopId)
		{
			Boundaries.Add(LoopId);
		}
		TrimBoundary = TrimBoundary->nextBoundary();
	}

	uint64 FaceID;
	return CTKIO_CreateFace(SurfaceID, bInOrientation, Boundaries, FaceID) ? FaceID : 0;
}

void FAliasCoretechWrapper::AddFace(AlSurface& Surface, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceList)
{
	if (AlTrimRegion *TrimRegion = Surface.firstTrimRegion())
	{
		while (TrimRegion)
		{
			if(uint64 FaceID = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation))
			{
				OutFaceList.Add(FaceID);
			}

			TrimRegion = TrimRegion->nextRegion();
		}

		return;
	}

	uint64 SurfaceID = AliasToCoreTechUtils::CreateCTNurbs(Surface, InObjectReference, InAlMatrix);

	uint64 FaceID;
	if (CTKIO_CreateFace(SurfaceID, bInOrientation, TArray<uint64>(), FaceID) && FaceID != 0)
	{
		OutFaceList.Add(FaceID);
	}
}

void FAliasCoretechWrapper::AddShell(AlShell& Shell, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceList)
{
	if (AlTrimRegion *TrimRegion = Shell.firstTrimRegion())
	{
		while (TrimRegion)
		{
			if(uint64 FaceID = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation))
			{
				OutFaceList.Add(FaceID);
			}

			TrimRegion = TrimRegion->nextRegion();
		}
	}
}

bool FAliasCoretechWrapper::AddBRep(TArray<AlDagNode*>& InDagNodeSet, EAliasObjectReference InObjectReference)
{
	if (!IsSessionValid())
	{
		return false;
	}

	TArray<uint64> FaceList;

	AlEdge2CTEdge.Empty();

	for (auto DagNode : InDagNodeSet)
	{
		if (DagNode == nullptr)
		{
			continue;
		}

		boolean bAlOrientation;
		DagNode->getSurfaceOrientation(bAlOrientation);
		bool bOrientation = (bool) bAlOrientation;

		AlMatrix4x4 AlMatrix;
		if (InObjectReference == EAliasObjectReference::ParentReference)
		{
			DagNode->localTransformationMatrix(AlMatrix);
		}

		AlObjectType objectType = DagNode->type();
		switch (objectType)
		{
			// Push all leaf nodes into 'leaves'
			case(kShellNodeType):
			{
				const char* shaderName = nullptr;
				if (AlShellNode *ShellNode = DagNode->asShellNodePtr())
				{
					if (AlShell *Shell = ShellNode->shell())
					{
						AddShell(*Shell, InObjectReference, AlMatrix, bOrientation, FaceList);
					}
				}
				break;
			}
			case(kSurfaceNodeType):
			{
				const char* shaderName = nullptr;
				if (AlSurfaceNode *SurfaceNode = DagNode->asSurfaceNodePtr())
				{
					if (AlSurface *Surface = SurfaceNode->surface())
					{
						AddFace(*Surface, InObjectReference, AlMatrix, bOrientation, FaceList);
					}
				}
				break;
			}
		}

		if (FaceList.Num() == 0)
		{
			return false;
		}
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

TSharedPtr<FAliasCoretechWrapper> FAliasCoretechWrapper::GetSharedSession()
{
	TSharedPtr<FAliasCoretechWrapper> Session = FAliasCoretechWrapper::SharedSession.Pin();
	if (!Session.IsValid())
	{
		Session = MakeShared<FAliasCoretechWrapper>(TEXT("Al2CTSharedSession"));
		SharedSession = Session;
	}
	return Session;
}

#endif
