// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AliasCoretechWrapper.h"

#ifdef CAD_LIBRARY
#include "CoreTechHelper.h"

#ifdef USE_OPENMODEL
#include "AlCurve.h"
#include "AlDagNode.h"
#include "AlShell.h"
#include "AlShellNode.h"
//#include "AlCurve.h"
#include "AlSurface.h"
#include "AlSurfaceNode.h"
#include "AlTrimBoundary.h"
#include "AlTrimCurve.h"
#include "AlTrimRegion.h"
#include "AlTM.h"

#include <vector>


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
	CT_OBJECT_ID CreateCTNurbs(Surface_T& Surface, bool bWorldPosition)
	{
		CT_UINT32 ControlPointDimension = 4;  // Control Hull Dimension (3 for non-rational or 4 for rational)

		CT_UINT32 ControlPointSizeU = Surface.uNumberOfCVsInclMultiples();
		CT_UINT32 ControlPointSizeV = Surface.vNumberOfCVsInclMultiples();

		CT_UINT32 OrderU = Surface.uDegree() + 1;  // U order of the surface
		CT_UINT32 OrderV = Surface.vDegree() + 1;  // V order of the surface

		CT_UINT32 KnotSizeU = Surface.realuNumberOfKnots() + 2;
		CT_UINT32 KnotSizeV = Surface.realvNumberOfKnots() + 2;

		std::vector <CT_DOUBLE> KnotValuesU;        // Axis knot value array
		std::vector <CT_DOUBLE> KnotValuesV;        // Axis knot value array
		std::vector <CT_UINT32> KnotMultiplicityU;  // Axis knot multiplicity array
		std::vector <CT_UINT32> KnotMultiplicityV;  // Axis knot multiplicity array

		KnotValuesU.resize(KnotSizeU);
		KnotValuesV.resize(KnotSizeV);

		Surface.realuKnotVector(KnotValuesU.data() + 1);
		Surface.realvKnotVector(KnotValuesV.data() + 1);

		KnotMultiplicityU.resize(KnotSizeU, 1);
		KnotMultiplicityV.resize(KnotSizeV, 1);

		KnotValuesU[0] = KnotValuesU[1];
		KnotValuesV[0] = KnotValuesV[1];
		KnotValuesU[KnotSizeU - 1] = KnotValuesU[KnotSizeU - 2];
		KnotValuesV[KnotSizeV - 1] = KnotValuesV[KnotSizeV - 2];

		std::vector <CT_DOUBLE> ControlPoint;
		int NbCoords = ControlPointSizeU * ControlPointSizeV * 4;
		ControlPoint.resize(NbCoords);
		if(bWorldPosition)
		{
			Surface.CVsWorldPositionInclMultiples(ControlPoint.data());
		}
		else 
		{
			Surface.CVsUnaffectedPositionInclMultiples(ControlPoint.data());
		}

		CT_OBJECT_ID CTSurfaceID = 0;
		CT_IO_ERROR Result = CT_SNURBS_IO::Create(CTSurfaceID,
			OrderU, OrderV,
			KnotSizeU, KnotSizeV,
			ControlPointSizeU, ControlPointSizeV,
			4, ControlPoint.data(),
			KnotValuesU.data(), KnotValuesV.data(),
			KnotMultiplicityU.data(), KnotMultiplicityV.data()
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
	std::vector <UVPoint4> ControlPoints;

	ControlPoints.resize(ControlPointSize);
	using UVPoint14 = double[4];
	Curve.CVsUnaffectedPositionInclMultiples((UVPoint14*)ControlPoints.data());

	std::vector <double> CTControlPoints;
	CTControlPoints.resize(ControlPointSize * 4);
	int Index = 0;
	for (CT_UINT32 IndexPoint = 0; IndexPoint < ControlPointSize; ++IndexPoint)
	{
		CTControlPoints[Index++] = ControlPoints[IndexPoint].x;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].y;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].z;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].w;
	}

	std::vector<CT_UINT32> KnotMult;
	KnotMult.resize(RealKnotSize, 1);

	std::vector <CT_DOUBLE> Knots;
	Knots.resize(RealKnotSize);
	Curve.realKnotVector(Knots.data() + 1);
	Knots[0] = Knots[1];
	Knots[RealKnotSize - 1] = Knots[RealKnotSize - 2];

	CT_OBJECT_ID NurbsID;
	CT_IO_ERROR setUvCurveError = CT_CNURBS_IO::Create(
		NurbsID,               /*!< [out] Id of created coedge */
		Order,                  /*!< [in] Order of curve */
		RealKnotSize,           /*!< [in] Knot vector size */
		ControlPointSize,       /*!< [in] Control Hull Size */
		4,                      /*!< [in] Control Hull Dimension (2 for non-rational or 3 for rational) */
		CTControlPoints.data(), /*!< [in] Control hull array */
		Knots.data(),           /*!< [in] Value knot array */
		KnotMult.data(),        /*!< [in] Multiplicity Knot array */
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
	std::vector <UVPoint> ControlPoints;
	std::vector <double> Weigths;

	CT_OBJECT_ID D3CurveId = 0;
	/*
    AlCurve * D3Curve = TrimCurve.unaffected3DCopyNoReverse();
	if (D3Curve)
	{
		D3CurveId = Add3DCurve(*D3Curve);
	}
    */

	ControlPoints.resize(ControlPointSize);
	Weigths.resize(ControlPointSize);
	using UVPoint2 = double[3];
	TrimCurve.CVsUVPosition(Weigths.data(), (UVPoint2*)ControlPoints.data());

	std::vector <double> CTControlPoints;
	CTControlPoints.resize(ControlPointSize * 3);
	int Index = 0;
	for (CT_UINT32 IndexPoint = 0; IndexPoint < ControlPointSize; ++IndexPoint)
	{
		CTControlPoints[Index++] = ControlPoints[IndexPoint].x;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].y;
		CTControlPoints[Index++] = ControlPoints[IndexPoint].z;
	}

	std::vector<CT_UINT32> KnotMult;
	KnotMult.resize(RealKnotSize, 1);

	std::vector <CT_DOUBLE> Knots;
	Knots.resize(RealKnotSize);
	TrimCurve.realKnotVector(Knots.data() + 1);
	Knots[0] = Knots[1];
	Knots[RealKnotSize - 1] = Knots[RealKnotSize - 2];

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
		CTControlPoints.data(), /*!< [in] Control hull array */
		Knots.data(),           /*!< [in] Value knot array */
		KnotMult.data(),        /*!< [in] Multiplicity Knot array */
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

CT_OBJECT_ID FAliasCoretechWrapper::AddTrimRegion(AlTrimRegion& TrimRegion, bool bWorldPosition, bool bOrientation)
{
	CT_OBJECT_ID NurbsId = AliasToCoreTechUtils::CreateCTNurbs(TrimRegion, bWorldPosition);
	if (NurbsId == 0)
	{
		return 0;
	}

	CT_LIST_IO Boundaries;
	CT_OBJECT_ID FaceID;
	CT_ORIENTATION FaceOrient = bOrientation ? CT_ORIENTATION::CT_FORWARD : CT_ORIENTATION::CT_REVERSE;

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

void FAliasCoretechWrapper::AddFace(AlSurface& Surface, CT_LIST_IO& FaceList, bool bWorldPosition, bool bOrientation)
{
	AlTrimRegion *TrimRegion = Surface.firstTrimRegion();
	if (TrimRegion)
	{
		while (TrimRegion)
		{
			CT_OBJECT_ID FaceID = AddTrimRegion(*TrimRegion, bWorldPosition, bOrientation);
			if (FaceID)
			{
				FaceList.PushBack(FaceID);
			}
			TrimRegion = TrimRegion->nextRegion();
		}
		return;
	}

	CT_OBJECT_ID NurbsId = AliasToCoreTechUtils::CreateCTNurbs(Surface, bWorldPosition);
	CT_LIST_IO Boundaries;
	CT_OBJECT_ID FaceID;
	CT_ORIENTATION FaceOrient = bOrientation ? CT_ORIENTATION::CT_FORWARD : CT_ORIENTATION::CT_REVERSE;
	CT_IO_ERROR Result = CT_FACE_IO::Create(FaceID, NurbsId, FaceOrient, Boundaries);
	if (Result == IO_OK)
	{
		FaceList.PushBack(FaceID);
	}
}

void FAliasCoretechWrapper::AddShell(AlShell& Shell, CT_LIST_IO& FaceList, bool bIsSymmetricBody, bool bOrientation)
{
	AlTrimRegion *TrimRegion = Shell.firstTrimRegion();
	while (TrimRegion)
	{
		CT_OBJECT_ID FaceID = AddTrimRegion(*TrimRegion, bIsSymmetricBody, bOrientation);
		if (FaceID)
		{
			FaceList.PushBack(FaceID);
		}
		TrimRegion = TrimRegion->nextRegion();
	}
}

CT_IO_ERROR FAliasCoretechWrapper::AddBRep(TArray<AlDagNode*>& DagNodeSet, bool bWorldPosition)
{
	CT_IO_ERROR Result = IO_OK;
	if (!IsSessionValid())
	{
		return IO_ERROR;
	}

	if (ImportParams.StitchingTechnique == StitchingSew)
	{
		bWorldPosition = true;
	}

	CT_LIST_IO FaceList;

	AlEdge2CTEdge.Empty();

	for (auto DagNode : DagNodeSet)
	{
		if (DagNode == nullptr)
		{
			continue;
		}
		boolean bAlOrientation;
		DagNode->getSurfaceOrientation(bAlOrientation);
		bool bOrientation = (bool) bAlOrientation;

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
					AddShell(*Shell, FaceList, bWorldPosition, bOrientation);
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
					AddFace(*Surface, FaceList, bWorldPosition, bOrientation);
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
