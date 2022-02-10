// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CADKernel/Core/Types.h"

#ifdef USE_TECHSOFT_SDK

#include "CADEnum.h"
#include "CADFileReport.h"

#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Math/MatrixH.h"

#include "TechSoftInterface.h"

typedef void A3DAsmProductOccurrence;
typedef void A3DCrvBase;
typedef void A3DCrvBase;
typedef void A3DCrvBlend02Boundary;
typedef void A3DCrvCircle;
typedef void A3DCrvComposite;
typedef void A3DCrvEllipse;
typedef void A3DCrvEquation;
typedef void A3DCrvHelix;
typedef void A3DCrvHyperbola;
typedef void A3DCrvIntersection;
typedef void A3DCrvLine;
typedef void A3DCrvNurbs;
typedef void A3DCrvOffset;
typedef void A3DCrvOnSurf;
typedef void A3DCrvParabola;
typedef void A3DCrvPolyLine;
typedef void A3DCrvTransform;
typedef void A3DEntity;
typedef void A3DGraphics;
typedef void A3DMiscCartesianTransformation;
typedef void A3DMiscCartesianTransformation;
typedef void A3DMiscGeneralTransformation;
typedef void A3DRiBrepModel;
typedef void A3DRiCoordinateSystem;
typedef void A3DRiRepresentationItem;
typedef void A3DSurfBase;
typedef void A3DSurfBase;
typedef void A3DSurfBase;
typedef void A3DSurfBase;
typedef void A3DSurfBlend01;
typedef void A3DSurfBlend02;
typedef void A3DSurfBlend03;
typedef void A3DSurfCone;
typedef void A3DSurfCone;
typedef void A3DSurfCylinder;
typedef void A3DSurfCylinder;
typedef void A3DSurfCylindrical;
typedef void A3DSurfExtrusion;
typedef void A3DSurfFromCurves;
typedef void A3DSurfNurbs;
typedef void A3DSurfOffset;
typedef void A3DSurfPipe;
typedef void A3DSurfPlane;
typedef void A3DSurfRevolution;
typedef void A3DSurfRuled;
typedef void A3DSurfSphere;
typedef void A3DSurfTorus;
typedef void A3DSurfTransform;
typedef void A3DTopoBrepData;
typedef void A3DTopoCoEdge;
typedef void A3DTopoConnex;
typedef void A3DTopoFace;
typedef void A3DTopoLoop;
typedef void A3DTopoShell;

namespace CADKernel
{

enum class EEntityType
{
	Instance,
	Component,
	Part,
	Assembly,
	UnloadedComponent,
	Body,
	BRepModel,
	Undefined
};

struct FEntityData
{
	unsigned int Uuid = 0;
	EEntityType EntityType;
	FString Name;
	TMap<FString, FString> MetaData;
	TArray<A3DAsmProductOccurrence*> Childs;
};

struct FBodyGeometricData
{
	unsigned int Uuid = 0;
	FString Name;
	//int FaceCount = 0;
	//int MeshTriangleCount = 0;
	//double MeshArea = 0.;
	//FBBox MeshBBox;
	//FBBox CADBBox;
	//int MaxFaceTriangleCount = 0;
	//int MaxCtrlHullSize = 0;
	//bool bTestBBox = true;
	//bool bTestArea = true;
	//bool bTestVertexCount = true;
	//bool bMeshIsExtract = false;

	A3DRiRepresentationItem* RepresentationItem = nullptr;
};

namespace TechSoftUtils
{
int32 TraverseSource(const A3DEntity* Entity, FEntityData& OutEntityData, bool bIsOccurrence = false, const ECADType FileType = ECADType::OTHER);
int32 TraverseSpecificMetaData(A3DEModellerType ModellerType, const A3DAsmProductOccurrence* Occurrence, FEntityData& MetaData);

int32 TraverseGraphics(const A3DGraphics* Graphics);
FMatrixH TraverseCoordinateSystem(const A3DRiCoordinateSystem* CoordinateSystem, double UnitScale);
FMatrixH TraverseTransformation(const A3DMiscCartesianTransformation* Transformation3d, double UnitScale);
FMatrixH TraverseGeneralTransformation(const A3DMiscGeneralTransformation* GeneralTransformation, double UnitScale);
FMatrixH TraverseTransformation3D(const A3DMiscCartesianTransformation* CartesianTransformation, double UnitScale);

FString GetFileName(const FString& InFilePath);

class FUVReparameterization
{
private:
	double Scale[2] = { 1., 1. };
	double Offset[2] = { 0., 0. };
	bool bNeedApply = false;

public:
	static const FUVReparameterization Identity;

	FUVReparameterization(const double InUScale = 1., const double InUOffset = 0., const double InVScale = 1., const double InVOffset = 0.)
	{
		SetCoef(InUScale, InUOffset, InVScale, InVOffset);
		SetNeedApply();
	}

	void SetScale(const double InUScale, const double InVScale)
	{
		Scale[EIso::IsoU] = InUScale;
		Scale[EIso::IsoV] = InVScale;
		Offset[EIso::IsoU] = 0.;
		Offset[EIso::IsoV] = 0.;

		SetNeedApply();
	}

	void SetCoef(const double InUScale, const double InUOffset, const double InVScale, const double InVOffset)
	{
		Scale[EIso::IsoU] = InUScale;
		Scale[EIso::IsoV] = InVScale;
		Offset[EIso::IsoU] = InUOffset;
		Offset[EIso::IsoV] = InVOffset;
		SetNeedApply();
	}

	bool NeedApply() const
	{
		return bNeedApply;
	}

	void SetNeedApply()
	{
		if (!FMath::IsNearlyEqual(Scale[EIso::IsoU], 1.) || !FMath::IsNearlyEqual(Scale[EIso::IsoV], 1.) || !FMath::IsNearlyEqual(Offset[EIso::IsoU], 0.) || !FMath::IsNearlyEqual(Offset[EIso::IsoV], 0.))
		{
			bNeedApply = true;
		}
		else
		{
			bNeedApply = false;
		}
	}

	void ScaleTransform(double InUScale, double InVScale)
	{
		Scale[EIso::IsoU] *= InUScale;
		Scale[EIso::IsoV] *= InVScale;
		Offset[EIso::IsoU] *= InUScale;
		Offset[EIso::IsoV] *= InVScale;
		SetNeedApply();
	}

	void Apply(FPoint& Point) const
	{
		Point.X = Scale[EIso::IsoU] * Point.X + Offset[EIso::IsoU];
		Point.Y = Scale[EIso::IsoV] * Point.Y + Offset[EIso::IsoV];
	}

	void AddUVTransform(A3DUVParameterizationData& Transform, bool bSwapUV = false)
	{
		int32 UIndex = bSwapUV ? 1 : 0;
		int32 VIndex = bSwapUV ? 0 : 1;

		Scale[UIndex] = Scale[UIndex] * Transform.m_dUCoeffA;
		Scale[VIndex] = Scale[VIndex] * Transform.m_dVCoeffA;
		Offset[UIndex] = Offset[UIndex] * Transform.m_dUCoeffA + Transform.m_dUCoeffB;
		Offset[VIndex] = Offset[VIndex] * Transform.m_dVCoeffA + Transform.m_dVCoeffB;
		SetNeedApply();
	}
};
}


class FSurfacicBoundary;

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
class FTopologicalEdge;
class FTopologicalLoop;
class FTopologicalShapeEntity;

class FTechSoftBridge
{
private:

	FSession& Session;
	FCADFileReport& Report;

	const double GeometricTolerance;
	const double SquareGeometricTolerance;
	const double SquareJoiningVertexTolerance;

	TMap<const A3DEntity*, TSharedPtr<FEntity>> TechSoftToEntity;
	TMap<const A3DTopoCoEdge*, TSharedPtr<FTopologicalEdge>> A3DEdgeToEdge;

	double UnitScale = 1;

public:
	FTechSoftBridge(FSession& InSession, FCADFileReport& InReport);

	TSharedRef<FBody> AddBody(A3DRiBrepModel* A3DBrepModel, FEntityData& EntityData, double FileUnit);

private:

	void TraverseBrepData(const A3DTopoBrepData* A3DBrepData, TSharedRef<FBody>& Body);

	void TraverseConnex(const A3DTopoConnex* A3DConnex, TSharedRef<FBody>& Body);
	void TraverseShell(const A3DTopoShell* A3DShell, TSharedRef<FBody>& Body);
	void AddFace(const A3DTopoFace* A3DFace, TSharedRef<FShell>& Body);

	TSharedPtr<FTopologicalLoop> AddLoop(const A3DTopoLoop* A3DLoop, const TSharedRef<FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FTopologicalEdge> AddEdge(const A3DTopoCoEdge* A3DCoedge, const TSharedRef<FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization, EOrientation& OutOrientation);

	TSharedPtr<FSurface> AddSurface(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddConeSurface(const A3DSurfCone* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddCylinderSurface(const A3DSurfCylinder* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddLinearTransfoSurface(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddNurbsSurface(const A3DSurfNurbs* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddOffsetSurface(const A3DSurfOffset* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddPlaneSurface(const A3DSurfPlane* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddRevolutionSurface(const A3DSurfRevolution* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddRuledSurface(const A3DSurfRuled* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddSphereSurface(const A3DSurfSphere* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddTorusSurface(const A3DSurfTorus* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);

	TSharedPtr<FSurface> AddBlend01Surface(const A3DSurfBlend01* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddBlend02Surface(const A3DSurfBlend02* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddBlend03Surface(const A3DSurfBlend03* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddCylindricalSurface(const A3DSurfCylindrical* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddPipeSurface(const A3DSurfPipe* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddExtrusionSurface(const A3DSurfExtrusion* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddSurfaceFromCurves(const A3DSurfFromCurves* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddTransformSurface(const A3DSurfTransform* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);

	TSharedPtr<FSurface> AddSurfaceAsNurbs(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization);
	TSharedPtr<FSurface> AddSurfaceNurbs(const A3DSurfNurbsData& A3DNurbsData, TechSoftUtils::FUVReparameterization& OutUVReparameterization);

	TSharedPtr<FRestrictionCurve> AddRestrictionCurve(const A3DCrvBase* A3DCurve, const TSharedRef<FSurface>& Surface);

	TSharedPtr<FCurve> AddCurve(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization = TechSoftUtils::FUVReparameterization::Identity);

	TSharedPtr<FCurve> AddCurveCircle(const A3DCrvCircle* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveComposite(const A3DCrvComposite* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveEllipse(const A3DCrvEllipse* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveHelix(const A3DCrvHelix* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveHyperbola(const A3DCrvHyperbola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveLine(const A3DCrvLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveNurbs(const A3DCrvNurbs* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveParabola(const A3DCrvParabola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurvePolyLine(const A3DCrvPolyLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	TSharedPtr<FCurve> AddCurveBlend02Boundary(const A3DCrvBlend02Boundary* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveEquation(const A3DCrvEquation* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveIntersection(const A3DCrvIntersection* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveOffset(const A3DCrvOffset* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveOnSurf(const A3DCrvOnSurf* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);
	TSharedPtr<FCurve> AddCurveTransform(const A3DCrvTransform* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	TSharedPtr<FCurve> AddCurveAsNurbs(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization);

	void AddMetadata(FEntityData& EntityData, FTopologicalShapeEntity& Entity);
};
}



#endif // USE_TECHSOFT_SDK
