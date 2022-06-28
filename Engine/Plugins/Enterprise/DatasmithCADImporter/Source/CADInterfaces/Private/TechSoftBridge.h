// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CADKernel/Core/Types.h"

#ifdef USE_TECHSOFT_SDK

#include "CADFileReport.h"

#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Math/MatrixH.h"

#include "TechSoftInterface.h"

namespace CADKernel
{

class FBody;
class FCriterion;
class FCurve;
class FEntity;
class FFaceMesh;
class FModel;
class FPoint;
class FRestrictionCurve;
class FSession;
class FShell;
class FSurface;
class FSurfacicBoundary;
class FTopologicalEdge;
class FTopologicalLoop;
class FTopologicalShapeEntity;

}

namespace CADLibrary
{

class FArchiveCADObject;
class FTechSoftFileParser;

namespace TechSoftUtils
{

class FUVReparameterization
{
private:
	double Scale[2] = { 1., 1. };
	double Offset[2] = { 0., 0. };
	bool bSwapUV = false;
	bool bNeedApply = false;
	bool bNeedSwapOrientation = false;

public:

	FUVReparameterization()
	{
	}

	void SetCoef(const double InUScale, const double InUOffset, const double InVScale, const double InVOffset)
	{
		Scale[CADKernel::EIso::IsoU] = InUScale;
		Scale[CADKernel::EIso::IsoV] = InVScale;
		Offset[CADKernel::EIso::IsoU] = InUOffset;
		Offset[CADKernel::EIso::IsoV] = InVOffset;
		SetNeedApply();
	}

	bool GetNeedApply() const
	{
		return bNeedApply;
	}

	bool GetSwapUV() const
	{
		return bSwapUV;
	}

	bool GetNeedSwapOrientation() const
	{
		return bNeedSwapOrientation != bSwapUV;
	}

	void SetNeedSwapOrientation()
	{
		bNeedSwapOrientation = true;
	}

	void SetNeedApply()
	{
		if (!FMath::IsNearlyEqual(Scale[CADKernel::EIso::IsoU], 1.) || !FMath::IsNearlyEqual(Scale[CADKernel::EIso::IsoV], 1.) || !FMath::IsNearlyEqual(Offset[CADKernel::EIso::IsoU], 0.) || !FMath::IsNearlyEqual(Offset[CADKernel::EIso::IsoV], 0.))
		{
			bNeedApply = true;
		}
		else
		{
			bNeedApply = false;
		}
	}

	void ScaleUVTransform(double InUScale, double InVScale)
	{
		if (bSwapUV)
		{
			Swap(InUScale, InVScale);
		}
		Scale[CADKernel::EIso::IsoU] *= InUScale;
		Scale[CADKernel::EIso::IsoV] *= InVScale;
		Offset[CADKernel::EIso::IsoU] *= InUScale;
		Offset[CADKernel::EIso::IsoV] *= InVScale;
		SetNeedApply();
	}

	void Process(TArray<CADKernel::FPoint>& Poles) const
	{
		if(bNeedApply)
		{
			for (CADKernel::FPoint& Point : Poles)
			{
				Apply(Point);
			}
		}
		if (bSwapUV)
		{
			for (CADKernel::FPoint& Point : Poles)
			{
				GetSwapUV(Point);
			}
		}
	}

	void AddUVTransform(A3DUVParameterizationData& Transform)
	{
		bSwapUV = (bool) Transform.m_bSwapUV;

		Scale[0] = Scale[0] * Transform.m_dUCoeffA;
		Scale[1] = Scale[1] * Transform.m_dVCoeffA;
		Offset[0] = Offset[0] * Transform.m_dUCoeffA + Transform.m_dUCoeffB;
		Offset[1] = Offset[1] * Transform.m_dVCoeffA + Transform.m_dVCoeffB;
		SetNeedApply();
	}

	void Apply(CADKernel::FPoint2D& Point) const
	{
		Point.U = Scale[CADKernel::EIso::IsoU] * Point.U + Offset[CADKernel::EIso::IsoU];
		Point.V = Scale[CADKernel::EIso::IsoV] * Point.V + Offset[CADKernel::EIso::IsoV];
	}

private:
	void Apply(CADKernel::FPoint& Point) const
	{
		Point.X = Scale[CADKernel::EIso::IsoU] * Point.X + Offset[CADKernel::EIso::IsoU];
		Point.Y = Scale[CADKernel::EIso::IsoV] * Point.Y + Offset[CADKernel::EIso::IsoV];
	}

	void GetSwapUV(CADKernel::FPoint& Point) const
	{
		Swap(Point.X, Point.Y);
	}

};
} // ns TechSoftUtils

class FTechSoftBridge
{
private:
	FTechSoftFileParser& Parser;

	CADKernel::FSession& Session;
	CADKernel::FModel& Model;
	CADKernel::FCADFileReport& Report;

	const double GeometricTolerance;
	const double SquareGeometricTolerance;
	const double SquareJoiningVertexTolerance;

	TMap<const A3DEntity*, CADKernel::FBody*> TechSoftToCADKernel;
	TMap<CADKernel::FBody*, const A3DEntity*> CADKernelToTechSoft;
	TMap<const A3DTopoCoEdge*, TSharedPtr<CADKernel::FTopologicalEdge>> A3DEdgeToEdge;

	double BodyScale = 1;

public:
	FTechSoftBridge(FTechSoftFileParser& InParser, CADKernel::FSession& InSession, CADKernel::FCADFileReport& InReport);
	CADKernel::FBody* AddBody(A3DRiBrepModel* A3DBRepModel, TMap<FString, FString> MetaData, const double InBodyScale);
	CADKernel::FBody* GetBody(A3DRiBrepModel* A3DBRepModel);
	const A3DRiBrepModel* GetA3DBody(CADKernel::FBody* BRepModel);

private:

	void TraverseBrepData(const A3DTopoBrepData* A3DBrepData, TSharedRef<CADKernel::FBody>& Body);

	void TraverseConnex(const A3DTopoConnex* A3DConnex, TSharedRef<CADKernel::FBody>& Body);
	void TraverseShell(const A3DTopoShell* A3DShell, TSharedRef<CADKernel::FBody>& Body);
	void AddFace(const A3DTopoFace* A3DFace, CADKernel::EOrientation Orientation, TSharedRef<CADKernel::FShell>& Body, uint32 Index);

	TSharedPtr<CADKernel::FTopologicalLoop> AddLoop(const A3DTopoLoop* A3DLoop, const TSharedRef<CADKernel::FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization, const bool bIsExternalLoop);
	TSharedPtr<CADKernel::FTopologicalEdge> AddEdge(const A3DTopoCoEdge* A3DCoedge, const TSharedRef<CADKernel::FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization, CADKernel::EOrientation& OutOrientation);

	TSharedPtr<CADKernel::FSurface> AddSurface(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddConeSurface(const A3DSurfCone* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddCylinderSurface(const A3DSurfCylinder* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddLinearTransfoSurface(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddNurbsSurface(const A3DSurfNurbs* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddOffsetSurface(const A3DSurfOffset* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddPlaneSurface(const A3DSurfPlane* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddRevolutionSurface(const A3DSurfRevolution* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddRuledSurface(const A3DSurfRuled* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddSphereSurface(const A3DSurfSphere* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddTorusSurface(const A3DSurfTorus* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);

	TSharedPtr<CADKernel::FSurface> AddBlend01Surface(const A3DSurfBlend01* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddBlend02Surface(const A3DSurfBlend02* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddBlend03Surface(const A3DSurfBlend03* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddCylindricalSurface(const A3DSurfCylindrical* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddPipeSurface(const A3DSurfPipe* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddExtrusionSurface(const A3DSurfExtrusion* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddSurfaceFromCurves(const A3DSurfFromCurves* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddTransformSurface(const A3DSurfTransform* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);

	TSharedPtr<CADKernel::FSurface> AddSurfaceAsNurbs(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<CADKernel::FSurface> AddSurfaceNurbs(const A3DSurfNurbsData& A3DNurbsData, TechSoftUtils::FUVReparameterization& OutUVReparameterization);

	TSharedPtr<CADKernel::FRestrictionCurve> AddRestrictionCurve(const A3DCrvBase* A3DCurve, const TSharedRef<CADKernel::FSurface>& Surface);

	TSharedPtr<CADKernel::FCurve> AddCurve(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	TSharedPtr<CADKernel::FCurve> AddCurveCircle(const A3DCrvCircle* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveComposite(const A3DCrvComposite* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveEllipse(const A3DCrvEllipse* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveHelix(const A3DCrvHelix* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveHyperbola(const A3DCrvHyperbola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveLine(const A3DCrvLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveNurbs(const A3DCrvNurbs* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveParabola(const A3DCrvParabola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurvePolyLine(const A3DCrvPolyLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	TSharedPtr<CADKernel::FCurve> AddCurveBlend02Boundary(const A3DCrvBlend02Boundary* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveEquation(const A3DCrvEquation* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveIntersection(const A3DCrvIntersection* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveOffset(const A3DCrvOffset* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveOnSurf(const A3DCrvOnSurf* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<CADKernel::FCurve> AddCurveTransform(const A3DCrvTransform* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	TSharedPtr<CADKernel::FCurve> AddCurveAsNurbs(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	void AddMetadata(FArchiveCADObject& EntityData, CADKernel::FTopologicalShapeEntity& Entity);
};
}



#endif // USE_TECHSOFT_SDK
