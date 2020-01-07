// Copyright Epic Games, Inc. All Rights Reserved.

#include "AliasCoretechWrapper.h"

#include "Hal/PlatformMemory.h"

#ifdef CAD_LIBRARY
#include "CoreTechHelper.h"

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
	CT_OBJECT_ID CreateCTNurbs(Surface_T& Surface, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix)
	{
		CT_UINT32 ControlPointDimension = 4;  // Control Hull Dimension (3 for non-rational or 4 for rational)

		CT_UINT32 ControlPointSizeU = Surface.uNumberOfCVsInclMultiples();
		CT_UINT32 ControlPointSizeV = Surface.vNumberOfCVsInclMultiples();

		CT_UINT32 OrderU = Surface.uDegree() + 1;  // U order of the surface
		CT_UINT32 OrderV = Surface.vDegree() + 1;  // V order of the surface

		CT_UINT32 KnotSizeU = Surface.realuNumberOfKnots() + 2;
		CT_UINT32 KnotSizeV = Surface.realvNumberOfKnots() + 2;

		TArray<CT_DOUBLE> KnotValuesU;        // Axis knot value array
		TArray<CT_DOUBLE> KnotValuesV;        // Axis knot value array
		TArray<CT_UINT32> KnotMultiplicityU;  // Axis knot multiplicity array
		TArray<CT_UINT32> KnotMultiplicityV;  // Axis knot multiplicity array

		KnotValuesU.SetNumUninitialized(KnotSizeU);
		KnotValuesV.SetNumUninitialized(KnotSizeV);

		Surface.realuKnotVector(KnotValuesU.GetData() + 1);
		Surface.realvKnotVector(KnotValuesV.GetData() + 1);

		KnotMultiplicityU.Init(1, KnotSizeU);
		KnotMultiplicityV.Init(1, KnotSizeV);

		KnotValuesU[0] = KnotValuesU[1];
		KnotValuesV[0] = KnotValuesV[1];
		KnotValuesU[KnotSizeU - 1] = KnotValuesU[KnotSizeU - 2];
		KnotValuesV[KnotSizeV - 1] = KnotValuesV[KnotSizeV - 2];

		TArray<CT_DOUBLE> ControlPoint;
		int NbCoords = ControlPointSizeU * ControlPointSizeV * 4;
		ControlPoint.SetNumUninitialized(NbCoords);

		if (InObjectReference == EAliasObjectReference::WorldReference)
		{
			Surface.CVsWorldPositionInclMultiples(ControlPoint.GetData());
		}
		else if (InObjectReference == EAliasObjectReference::ParentReference)
		{
			AlTM TranformMatrix(InAlMatrix);
			Surface.CVsAffectedPositionInclMultiples(TranformMatrix, ControlPoint.GetData());
		}
		else  // EAliasObjectReference::LocalReference
		{
			Surface.CVsUnaffectedPositionInclMultiples(ControlPoint.GetData());
		}

		CT_OBJECT_ID CTSurfaceID = 0;
		CT_IO_ERROR Result = CT_SNURBS_IO::Create(CTSurfaceID,
			OrderU, OrderV,
			KnotSizeU, KnotSizeV,
			ControlPointSizeU, ControlPointSizeV,
			4, ControlPoint.GetData(),
			KnotValuesU.GetData(), KnotValuesV.GetData(),
			KnotMultiplicityU.GetData(), KnotMultiplicityV.GetData()
		);
		if (Result != IO_OK)
		{
			return 0;
		}
		return CTSurfaceID;
	}
}

CT_IO_ERROR FAliasCoretechWrapper::Tessellate(FMeshDescription& Mesh, FMeshParameters& MeshParameters)
{
	// Apply stitching if applicable
	TopoFixes(1.);

	// Perform tessellation
	return CADLibrary::Tessellate(MainObjectId, ImportParams, Mesh, MeshParameters);
}

CT_OBJECT_ID FAliasCoretechWrapper::Add3DCurve(AlCurve& Curve)
{
	curveFormType Form = Curve.form();
	CT_UINT32 Order = Curve.degree() + 1;
	CT_UINT32 ControlPointSize = Curve.numberOfCVs();
	CT_UINT32 RealKnotSize = Curve.realNumberOfKnots() + 2;
	TArray<UVPoint4> ControlPoints;

	ControlPoints.SetNumUninitialized(ControlPointSize);
	using UVPoint14 = double[4];
	Curve.CVsUnaffectedPositionInclMultiples((UVPoint14*)ControlPoints.GetData());

	TArray<double> CTControlPoints;
	CTControlPoints.SetNumUninitialized(ControlPointSize * 4);
	int Index = 0;
	for (CT_UINT32 IndexPoint = 0; IndexPoint < ControlPointSize; ++IndexPoint)
	{
		CTControlPoints[Index++] = ControlPoints[IndexPoint].x;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].y;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].z;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].w;
	}

	TArray<CT_UINT32> KnotMult;
	KnotMult.Init(1, RealKnotSize);

	TArray<CT_DOUBLE> Knots;
	Knots.SetNumUninitialized(RealKnotSize);
	Curve.realKnotVector(Knots.GetData() + 1);
	Knots[0] = Knots[1];
	Knots[RealKnotSize - 1] = Knots[RealKnotSize - 2];

	CT_OBJECT_ID NurbsID;
	CT_IO_ERROR setUvCurveError = CT_CNURBS_IO::Create(
		NurbsID,               /*!< [out] Id of created coedge */
		Order,                  /*!< [in] Order of curve */
		RealKnotSize,           /*!< [in] Knot vector size */
		ControlPointSize,       /*!< [in] Control Hull Size */
		4,                      /*!< [in] Control Hull Dimension (2 for non-rational or 3 for rational) */
		CTControlPoints.GetData(), /*!< [in] Control hull array */
		Knots.GetData(),           /*!< [in] Value knot array */
		KnotMult.GetData(),        /*!< [in] Multiplicity Knot array */
		Knots[0],               /*!< [in] start parameter of coedge on the uv curve (t range=[knot[0], knot[knot_size-1]]) */
		Knots[RealKnotSize - 1]   /*!< [in] end parameter of coedge on the uv curve (t range=[knot[0], knot[knot_size-1]]) */
	);

	if (!setUvCurveError)
	{
		return 0;
	}

	return NurbsID;
}

CT_OBJECT_ID FAliasCoretechWrapper::AddTrimCurve(AlTrimCurve& TrimCurve)
{
	boolean bIsReversed = TrimCurve.isReversed();
	curveFormType Form = TrimCurve.form();
	CT_UINT32 Order = TrimCurve.degree() + 1;
	CT_UINT32 ControlPointSize = TrimCurve.numberOfCVs();
	CT_UINT32 RealKnotSize = TrimCurve.realNumberOfKnots() + 2;
	TArray<UVPoint> ControlPoints;
	TArray<double> Weigths;

	ControlPoints.SetNumUninitialized(ControlPointSize);
	Weigths.SetNumUninitialized(ControlPointSize);
	using UVPoint2 = double[3];
	TrimCurve.CVsUVPosition(Weigths.GetData(), (UVPoint2*)ControlPoints.GetData());

	TArray<CT_UINT32> KnotMult;
	KnotMult.Init(1, RealKnotSize);

	TArray<CT_DOUBLE> Knots;
	Knots.SetNumUninitialized(RealKnotSize);
	TrimCurve.realKnotVector(Knots.GetData() + 1);
	Knots[0] = Knots[1];
	Knots[RealKnotSize - 1] = Knots[RealKnotSize - 2];

	CT_OBJECT_ID D3CurveId = 0;
	CT_OBJECT_ID CoedgeID;
	CT_IO_ERROR Result = CT_COEDGE_IO::Create(CoedgeID, bIsReversed ? CT_ORIENTATION::CT_REVERSE : CT_ORIENTATION::CT_FORWARD, D3CurveId);
	if (Result != IO_OK)
	{
		return 0;
	}

	Result = CT_COEDGE_IO::SetUVCurve(
		CoedgeID,               /*!< [out] Id of created coedge */
		Order,                  /*!< [in] Order of curve */
		RealKnotSize,           /*!< [in] Knot vector size */
		ControlPointSize,       /*!< [in] Control Hull Size */
		3,                      /*!< [in] Control Hull Dimension (2 for non-rational or 3 for rational) */
		(double*) ControlPoints.GetData(), /*!< [in] Control hull array */
		Knots.GetData(),           /*!< [in] Value knot array */
		KnotMult.GetData(),        /*!< [in] Multiplicity Knot array */
		Knots[0],               /*!< [in] start parameter of coedge on the uv curve (t range=[knot[0], knot[knot_size-1]]) */
		Knots[RealKnotSize - 1]   /*!< [in] end parameter of coedge on the uv curve (t range=[knot[0], knot[knot_size-1]]) */
	);
	if (Result != IO_OK)
	{
		return 0;
	}

	// Build topo
	AlTrimCurve *TwinCurve = TrimCurve.getTwinCurve();
	if (TwinCurve) {
		CT_OBJECT_ID *TwinCoedgeID = AlEdge2CTEdge.Find(TwinCurve);
		if (TwinCoedgeID)
		{
			CT_COEDGE_IO::MatchCoedges(*TwinCoedgeID, CoedgeID);
		}
		AlEdge2CTEdge.Add(&TrimCurve, CoedgeID);
	}

	return CoedgeID;
}

CT_OBJECT_ID FAliasCoretechWrapper::AddTrimBoundary(AlTrimBoundary& TrimBoundary)
{
	AlTrimCurve *TrimCurve = TrimBoundary.firstCurve();
	CT_LIST_IO Coedges;
	while (TrimCurve)
	{
		CT_OBJECT_ID CoedgeID = AddTrimCurve(*TrimCurve);
		if (CoedgeID)
		{
			Coedges.PushBack(CoedgeID);
		}
		TrimCurve = TrimCurve->nextCurve();
	}

	CT_OBJECT_ID LoopId;

	CT_IO_ERROR Result = CT_LOOP_IO::Create(LoopId, Coedges);
	if (Result != IO_OK)
	{
		return 0;
	}
	return LoopId;
}

CT_OBJECT_ID FAliasCoretechWrapper::AddTrimRegion(AlTrimRegion& TrimRegion, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation)
{
	CT_OBJECT_ID NurbsId = AliasToCoreTechUtils::CreateCTNurbs(TrimRegion, InObjectReference, InAlMatrix);
	if (NurbsId == 0)
	{
		return 0;
	}

	CT_LIST_IO Boundaries;
	CT_OBJECT_ID FaceID;
	CT_ORIENTATION FaceOrient = bInOrientation ? CT_ORIENTATION::CT_FORWARD : CT_ORIENTATION::CT_REVERSE;

	AlTrimBoundary *TrimBoundary = TrimRegion.firstBoundary();
	while (TrimBoundary)
	{
		CT_OBJECT_ID LoopId = AddTrimBoundary(*TrimBoundary);
		if (LoopId)
		{
			Boundaries.PushBack(LoopId);
		}
		TrimBoundary = TrimBoundary->nextBoundary();
	}
	CT_IO_ERROR Result = CT_FACE_IO::Create(FaceID, NurbsId, FaceOrient, Boundaries);
	if (Result == IO_OK)
	{
		return FaceID;
	}
	return 0;
}

void FAliasCoretechWrapper::AddFace(AlSurface& Surface, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, CT_LIST_IO& OutFaceList)
{
	AlTrimRegion *TrimRegion = Surface.firstTrimRegion();
	if (TrimRegion)
	{
		while (TrimRegion)
		{
			CT_OBJECT_ID FaceID = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation);
			if (FaceID)
			{
				OutFaceList.PushBack(FaceID);
			}
			TrimRegion = TrimRegion->nextRegion();
		}
		return;
	}

	CT_OBJECT_ID NurbsId = AliasToCoreTechUtils::CreateCTNurbs(Surface, InObjectReference, InAlMatrix);
	CT_LIST_IO Boundaries;
	CT_OBJECT_ID FaceID;
	CT_ORIENTATION FaceOrient = bInOrientation ? CT_ORIENTATION::CT_FORWARD : CT_ORIENTATION::CT_REVERSE;
	CT_IO_ERROR Result = CT_FACE_IO::Create(FaceID, NurbsId, FaceOrient, Boundaries);
	if (Result == IO_OK)
	{
		OutFaceList.PushBack(FaceID);
	}
}

void FAliasCoretechWrapper::AddShell(AlShell& Shell, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, CT_LIST_IO& OutFaceLis)
{
	AlTrimRegion *TrimRegion = Shell.firstTrimRegion();
	while (TrimRegion)
	{
		CT_OBJECT_ID FaceID = AddTrimRegion(*TrimRegion, InObjectReference, InAlMatrix, bInOrientation);
		if (FaceID)
		{
			OutFaceLis.PushBack(FaceID);
		}
		TrimRegion = TrimRegion->nextRegion();
	}
}

CT_IO_ERROR FAliasCoretechWrapper::AddBRep(TArray<AlDagNode*>& InDagNodeSet, EAliasObjectReference InObjectReference)
{
	CT_IO_ERROR Result = IO_OK;
	if (!IsSessionValid())
	{
		return IO_ERROR;
	}

	CT_LIST_IO FaceList;

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
			AlShellNode *ShellNode = DagNode->asShellNodePtr();
			if (ShellNode)
			{
				AlShell *Shell = ShellNode->shell();
				if (Shell)
				{
					AddShell(*Shell, InObjectReference, AlMatrix, bOrientation, FaceList);
				}
			}
			break;
		}
		case(kSurfaceNodeType):
		{
			const char* shaderName = nullptr;
			AlSurfaceNode *SurfaceNode = DagNode->asSurfaceNodePtr();
			if (SurfaceNode)
			{
				AlSurface *Surface = SurfaceNode->surface();
				if (Surface)
				{
					AddFace(*Surface, InObjectReference, AlMatrix, bOrientation, FaceList);
				}
			}
			break;
		}
		}
		if (FaceList.IsEmpty())
		{
			return IO_ERROR;
		}
	}

	// Create body from faces
	CT_OBJECT_ID BodyID;
	Result = CT_BODY_IO::CreateFromFaces(BodyID, CT_BODY_PROP::CT_BODY_PROP_EXACT | CT_BODY_PROP::CT_BODY_PROP_CLOSE, FaceList);
	if (Result != IO_OK)
	{
		return Result;
	}

	CT_LIST_IO Bodies;
	Bodies.PushBack(BodyID);

	// Setup parenting
	Result = CT_COMPONENT_IO::AddChildren(MainObjectId, Bodies);
	return Result;
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
#endif
