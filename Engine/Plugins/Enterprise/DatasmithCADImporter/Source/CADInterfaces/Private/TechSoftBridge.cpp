// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechSoftBridge.h"

#ifdef USE_TECHSOFT_SDK

#include "TechSoftInterface.h"
#include "TechSoftFileParser.h"
#include "TechSoftUtils.h"
#include "TUniqueTechSoftObj.h"
#include "CADFileReport.h"

#include "CADKernel/Core/Session.h"

#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Curves/NURBSCurveData.h"

#include "CADKernel/Geo/Surfaces/NurbsSurfaceData.h"
#include "CADKernel/Geo/Surfaces/Surface.h"

#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalVertex.h"

#include "CADKernel/Utils/StringUtil.h"

#include "CADKernel/UI/Display.h"

namespace CADLibrary
{
namespace TechSoftUtils
{

template<typename... InArgTypes>
A3DStatus GetCurveAsNurbs(const A3DCrvBase* A3DCurve, A3DCrvNurbsData* DataPtr, InArgTypes&&... Args)
{
	return TechSoftInterface::GetCurveAsNurbs(A3DCurve, DataPtr, Forward<InArgTypes>(Args)...);
};

template<typename... InArgTypes>
A3DStatus GetSurfaceAsNurbs(const A3DSurfBase* A3DSurface, A3DSurfNurbsData* DataPtr, InArgTypes&&... Args)
{
	return TechSoftInterface::GetSurfaceAsNurbs(A3DSurface, DataPtr, Forward<InArgTypes>(Args)...);
}; 

CADKernel::FMatrixH CreateCoordinateSystem(const A3DMiscCartesianTransformationData& Transformation, double UnitScale = 1.0)
{
	CADKernel::FPoint Origin(&Transformation.m_sOrigin.m_dX);
	CADKernel::FPoint Ox(&Transformation.m_sXVector.m_dX);
	CADKernel::FPoint Oy(&Transformation.m_sYVector.m_dX);

	Ox.Normalize();
	Oy.Normalize();

	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		Origin *= UnitScale;
	}
	CADKernel::FPoint Oz = Ox ^ Oy;

	CADKernel::FMatrixH Matrix = CADKernel::FMatrixH(Origin, Ox, Oy, Oz);

	if (!FMath::IsNearlyEqual(Transformation.m_sScale.m_dX, 1.) || !FMath::IsNearlyEqual(Transformation.m_sScale.m_dY, 1.) || !FMath::IsNearlyEqual(Transformation.m_sScale.m_dZ, 1.))
	{
		CADKernel::FMatrixH Scale = CADKernel::FMatrixH::MakeScaleMatrix(Transformation.m_sScale.m_dX, Transformation.m_sScale.m_dY, Transformation.m_sScale.m_dZ);
		Matrix *= Scale;
	}
	return Matrix;
}

void FillInt32Array(const int32 Count, const A3DInt32* Values, TArray<int32>& OutInt32Array)
{
	OutInt32Array.Reserve(Count);
	for (int32 Index = 0; Index < Count; Index++)
	{
		OutInt32Array.Add(Values[Index]);
	}
};

void FillDoubleArray(const int32 Count, const double* Values, TArray<double>& OutDoubleArray)
{
	OutDoubleArray.Reserve(Count);
	for (int32 Index = 0; Index < Count; Index++)
	{
		OutDoubleArray.Add(Values[Index]);
	}
};

void FillDoubleArray(const int32 UCount, const int32 VCount, const double* Values, TArray<double>& OutDoubleArray)
{
	OutDoubleArray.SetNum(UCount * VCount);
	for (int32 Undex = 0, ValueIndex = 0; Undex < UCount; ++Undex)
	{
		int32 Index = Undex;
		for (int32 Vndex = 0; Vndex < VCount; ++Vndex, Index += UCount, ++ValueIndex)
		{
			OutDoubleArray[Index] = Values[ValueIndex];
		}
	}
}

void FillPointArray(const int32 Count, const A3DVector3dData* Points, TArray<CADKernel::FPoint>& OutPointsArray, double UnitScale = 1.0)
{
	OutPointsArray.Reserve(Count);
	for (int32 Index = 0; Index < Count; Index++)
	{
		OutPointsArray.Emplace(&Points[Index].m_dX);
	}

	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		for (CADKernel::FPoint& Point : OutPointsArray)
		{
			Point *= UnitScale;
		}
	}
};

void FillPointArray(const int32 UCount, const int32 VCount, const A3DVector3dData* Points, TArray<CADKernel::FPoint>& OutPointsArray, double UnitScale = 1.0)
{
	OutPointsArray.SetNum(UCount * VCount);
	for (int32 Undex = 0, PointIndex = 0; Undex < UCount; ++Undex)
	{
		int32 Index = Undex;
		for (int32 Vndex = 0; Vndex < VCount; ++Vndex, Index += UCount, ++PointIndex)
		{
			OutPointsArray[Index].Set((double*)&(Points[PointIndex].m_dX));
		}
	}

	if (!FMath::IsNearlyEqual(UnitScale, 1.))
	{
		for (CADKernel::FPoint& Point : OutPointsArray)
		{
			Point *= UnitScale;
		}
	}
};

CADKernel::FSurfacicBoundary GetSurfacicBoundary(A3DDomainData& Domain, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{

	CADKernel::FPoint2D Min(Domain.m_sMin.m_dX, Domain.m_sMin.m_dY);
	CADKernel::FPoint2D Max(Domain.m_sMax.m_dX, Domain.m_sMax.m_dY);

	if(UVReparameterization.GetNeedApply())
	{
		UVReparameterization.Apply(Min);
		UVReparameterization.Apply(Max);
	}

	CADKernel::EIso UIndex = UVReparameterization.GetSwapUV() ? CADKernel::EIso::IsoV : CADKernel::EIso::IsoU;
	CADKernel::EIso VIndex = UVReparameterization.GetSwapUV() ? CADKernel::EIso::IsoU : CADKernel::EIso::IsoV;

	CADKernel::FSurfacicBoundary Boundary;
	Boundary[UIndex].Min = Min.U;
	Boundary[VIndex].Min = Min.V;
	Boundary[UIndex].Max = Max.U;
	Boundary[VIndex].Max = Max.V;

	return Boundary;
}

CADKernel::FLinearBoundary GetLinearBoundary(A3DIntervalData& A3DDomain)
{
	CADKernel::FLinearBoundary Domain(A3DDomain.m_dMin, A3DDomain.m_dMax);
	return Domain;
}

CADKernel::FLinearBoundary GetLinearBoundary(const A3DCrvBase* A3DCurve)
{
	TUniqueTSObj<A3DIntervalData> A3DDomain(A3DCurve);
	CADKernel::FLinearBoundary Domain(A3DDomain->m_dMin, A3DDomain->m_dMax);
	return Domain;
}

} // ns TechSoftUtils

FTechSoftBridge::FTechSoftBridge(FTechSoftFileParser& InParser, CADKernel::FSession& InSession, CADKernel::FCADFileReport& InReport)
	: Parser(InParser)
	, Session(InSession)
	, Model(InSession.GetModel())
	, Report(InReport)
	, GeometricTolerance(Session.GetGeometricTolerance())
	, SquareGeometricTolerance(FMath::Square(Session.GetGeometricTolerance()))
	, SquareJoiningVertexTolerance(SquareGeometricTolerance * 2)
{
}

const A3DRiBrepModel* FTechSoftBridge::GetA3DBody(CADKernel::FBody* BRepModel)
{
	const A3DEntity** BodyPtr = CADKernelToTechSoft.Find(BRepModel);
	if (BodyPtr)
	{
		return (A3DRiBrepModel*) *BodyPtr;
	}
	return nullptr;
}

CADKernel::FBody* FTechSoftBridge::GetBody(A3DRiBrepModel* A3DBRepModel)
{
	CADKernel::FBody** BodyPtr = TechSoftToCADKernel.Find(A3DBRepModel);
	if (BodyPtr &&!(*BodyPtr)->IsDeleted())
	{
		return *BodyPtr;
	}
	return nullptr;
}

CADKernel::FBody* FTechSoftBridge::AddBody(A3DRiBrepModel* A3DBRepModel, TMap<FString, FString> MetaData, const double InBodyScale)
{
	Report.BodyCount++;

	//CADKernel working unit is mm
	BodyScale = InBodyScale * 10.;

	FEntityMetaData BRepMetaData;
	Parser.ExtractMetaData(A3DBRepModel, BRepMetaData);
	FString* Name = MetaData.Find(TEXT("Name"));
	if(Name != nullptr)
	{
		BRepMetaData.MetaData.FindOrAdd(TEXT("Name")) = *Name;
	}
	
	CADKernel::FBody** BodyPtr = TechSoftToCADKernel.Find(A3DBRepModel);
	if (BodyPtr)
	{
		if((*BodyPtr)->IsDeleted())
		{
			return nullptr;
		}
		return *BodyPtr;
	}

	TSharedRef<CADKernel::FBody> Body = CADKernel::FEntity::MakeShared<CADKernel::FBody>();
	AddMetadata(BRepMetaData, *Body);

	TUniqueTSObj<A3DRiBrepModelData> BRepModelData(A3DBRepModel);
	if (BRepModelData.IsValid())
	{
		TraverseBrepData(BRepModelData->m_pBrepData, Body);
	}

	if(Body->FaceCount() == 0)
	{
		Body->SetDeleted();
		return nullptr;
	}

	Model.Add(Body);
	TechSoftToCADKernel.Add(A3DBRepModel, &*Body);
	CADKernelToTechSoft.Add(&*Body, A3DBRepModel);

	return &*Body;
}

void FTechSoftBridge::TraverseBrepData(const A3DTopoBrepData* A3DBrepData, TSharedRef<CADKernel::FBody>& Body)
{
	FEntityMetaData MetaData;
	Parser.ExtractMetaData(A3DBrepData, MetaData);

	{
		TUniqueTSObj<A3DTopoBodyData> TopoBodyData(A3DBrepData);
		if (TopoBodyData.IsValid())
		{
			if (TopoBodyData->m_pContext)
			{
				TUniqueTSObj<A3DTopoContextData> TopoContextData(TopoBodyData->m_pContext);
				if (TopoContextData.IsValid())
				{
					if (TopoContextData->m_bHaveScale)
					{
						BodyScale *= TopoContextData->m_dScale;
					}
				}
			}
		}
	}

	TUniqueTSObj<A3DTopoBrepDataData> TopoBrepData(A3DBrepData);
	if (TopoBrepData.IsValid())
	{
		for (A3DUns32 Index = 0; Index < TopoBrepData->m_uiConnexSize; ++Index)
		{
			TraverseConnex(TopoBrepData->m_ppConnexes[Index], Body);
		}
	}
}

void FTechSoftBridge::TraverseConnex(const A3DTopoConnex* A3DTopoConnex, TSharedRef<CADKernel::FBody>& Body)
{
	FEntityMetaData MetaData;
	Parser.ExtractMetaData(A3DTopoConnex, MetaData);

	TUniqueTSObj<A3DTopoConnexData> TopoConnexData(A3DTopoConnex);
	if (TopoConnexData.IsValid())
	{
		for (A3DUns32 Index = 0; Index < TopoConnexData->m_uiShellSize; ++Index)
		{
			TraverseShell(TopoConnexData->m_ppShells[Index], Body);
		}
	}
}

void FTechSoftBridge::TraverseShell(const A3DTopoShell* A3DShell, TSharedRef<CADKernel::FBody>& Body)
{
	FEntityMetaData MetaData;
	Parser.ExtractMetaData(A3DShell, MetaData);

	TSharedRef<CADKernel::FShell> Shell = CADKernel::FEntity::MakeShared<CADKernel::FShell>();
	Body->AddShell(Shell);
	Report.ShellCount++;

	AddMetadata(MetaData, *Shell);

	TUniqueTSObj<A3DTopoShellData> ShellData(A3DShell);

	if (ShellData.IsValid())
	{
		A3DEdgeToEdge.Empty();
		for (A3DUns32 Index = 0; Index < ShellData->m_uiFaceSize; ++Index)
		{
			AddFace(ShellData->m_ppFaces[Index], ShellData->m_pucOrientationWithShell[Index] == 1 ? CADKernel::Front : CADKernel::Back, Shell, Index);
		}
	}
}

static bool bUseCurveAsNurbs = true;
TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurve(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	TSharedPtr<CADKernel::FCurve> Curve = TSharedPtr<CADKernel::FCurve>();
	A3DEEntityType eType;
	A3DInt32 Ret = TechSoftInterface::GetEntityType(A3DCurve, &eType);
	if (Ret == A3D_SUCCESS)
	{
		Report.CurveCount++;

		switch (eType)
		{
		case kA3DTypeCrvNurbs:
			Curve = AddCurveNurbs(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvLine:
			Curve = AddCurveLine(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvCircle:
			Curve = AddCurveCircle(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvEllipse:
			Curve = AddCurveEllipse(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvParabola:
			Curve = AddCurveParabola(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvHyperbola:
			Curve = AddCurveHyperbola(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvHelix:
			Curve = AddCurveHelix(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvPolyLine:
			Curve = AddCurvePolyLine(A3DCurve, UVReparameterization);
			break;
		case kA3DTypeCrvComposite:
			Curve = AddCurveComposite(A3DCurve, UVReparameterization);
			break;
		default:
			Curve = AddCurveAsNurbs(A3DCurve, UVReparameterization);
			break;
		}
	}

	CADKernel::FLinearBoundary Boundary = TechSoftUtils::GetLinearBoundary(A3DCurve);

	return Curve;
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveLine(const A3DCrvLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveLineCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}


	TUniqueTSObj<A3DCrvLineData> CrvLineData(A3DCurve);
	if (!CrvLineData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvLineData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveCircle(const A3DCrvCircle* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveCircleCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvCircleData> CrvCircleData(A3DCurve);
	if (!CrvCircleData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvCircleData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveEllipse(const A3DCrvEllipse* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveEllipseCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvEllipseData> CrvEllipseData(A3DCurve);
	if (!CrvEllipseData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvEllipseData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveParabola(const A3DCrvParabola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveParabolaCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvParabolaData> CrvParabolaData(A3DCurve);
	if (!CrvParabolaData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvParabolaData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveHyperbola(const A3DCrvHyperbola* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveHyperbolaCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvHyperbolaData> CrvHyperbolaData(A3DCurve);
	if (!CrvHyperbolaData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvHyperbolaData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveHelix(const A3DCrvHelix* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveHelixCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvHelixData> CrvHelixData(A3DCurve);
	if (!CrvHelixData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvHelixData->m_bIs2D;

	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurvePolyLine(const A3DCrvPolyLine* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurvePolyLineCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvPolyLineData> CrvPolyLineData(A3DCurve);
	if (!CrvPolyLineData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvPolyLineData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveComposite(const A3DCrvComposite* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveCompositeCount++;

	if (bUseCurveAsNurbs)
	{
		return AddCurveAsNurbs(A3DCurve, UVReparameterization);
	}

	TUniqueTSObj<A3DCrvCompositeData> CrvCompositeData(A3DCurve);
	if (!CrvCompositeData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	bool bIs2D = (bool)CrvCompositeData->m_bIs2D;
	// Todo
	return TSharedPtr<CADKernel::FCurve>();
}

TSharedPtr<CADKernel::FCurve> AddCurveNurbsFromData(A3DCrvNurbsData& A3DNurbs, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	CADKernel::FNurbsCurveData Nurbs;
	Nurbs.Dimension = A3DNurbs.m_bIs2D ? 2 : 3;
	Nurbs.bIsRational = (bool)A3DNurbs.m_bRational;
	Nurbs.Degree = A3DNurbs.m_uiDegree;

	TechSoftUtils::FillPointArray(A3DNurbs.m_uiCtrlSize, A3DNurbs.m_pCtrlPts, Nurbs.Poles);
	if(Nurbs.Dimension == 2)
	{
		UVReparameterization.Process(Nurbs.Poles);
	}

	TechSoftUtils::FillDoubleArray(A3DNurbs.m_uiKnotSize, A3DNurbs.m_pdKnots, Nurbs.NodalVector);
	if (Nurbs.bIsRational)
	{
		TechSoftUtils::FillDoubleArray(A3DNurbs.m_uiCtrlSize, A3DNurbs.m_pdWeights, Nurbs.Weights);
	}

	A3DCrvNurbsGet(NULL, &A3DNurbs);

	return CADKernel::FCurve::MakeNurbsCurve(Nurbs);
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveNurbs(const A3DCrvNurbs* A3DNurbs, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveLineCount++;

	TUniqueTSObj<A3DCrvNurbsData> CrvNurbsData(A3DNurbs);
	if (!CrvNurbsData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	return AddCurveNurbsFromData(*CrvNurbsData, UVReparameterization);
}

TSharedPtr<CADKernel::FCurve> FTechSoftBridge::AddCurveAsNurbs(const A3DCrvBase* A3DCurve, const TechSoftUtils::FUVReparameterization& UVReparameterization)
{
	Report.CurveAsNurbsCount++;

	TUniqueTSObj<A3DCrvNurbsData> NurbsData;

	A3DDouble Tolerance = 1e-3;
	A3DBool bUseSameParameterization = true;
	NurbsData.FillWith(&TechSoftUtils::GetCurveAsNurbs, A3DCurve, Tolerance, bUseSameParameterization);

	if (!NurbsData.IsValid())
	{
		return TSharedPtr<CADKernel::FCurve>();
	}

	return AddCurveNurbsFromData(*NurbsData, UVReparameterization);
}

TSharedPtr<CADKernel::FTopologicalEdge> FTechSoftBridge::AddEdge(const A3DTopoCoEdge* A3DCoedge, const TSharedRef<CADKernel::FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization, CADKernel::EOrientation& OutOrientation)
{
	Report.EdgeCount++;

	TUniqueTSObj<A3DTopoCoEdgeData> CoEdgeData(A3DCoedge);
	if (!CoEdgeData.IsValid())
	{
		return TSharedPtr<CADKernel::FTopologicalEdge>();
	}

	if (CoEdgeData->m_pUVCurve == nullptr)
	{
		return TSharedPtr<CADKernel::FTopologicalEdge>();
	}

	TSharedPtr<CADKernel::FCurve> Curve = AddCurve(CoEdgeData->m_pUVCurve, UVReparameterization);
	if (!Curve.IsValid())
	{
		return TSharedPtr<CADKernel::FTopologicalEdge>();
	}

	TSharedRef<CADKernel::FRestrictionCurve> RestrictionCurve = CADKernel::FEntity::MakeShared<CADKernel::FRestrictionCurve>(Surface, Curve.ToSharedRef());

	TSharedPtr<CADKernel::FTopologicalEdge> Edge = CADKernel::FTopologicalEdge::Make(RestrictionCurve);
	if (!Edge.IsValid())
	{
		return TSharedPtr<CADKernel::FTopologicalEdge>();
	}

	A3DEdgeToEdge.Emplace(A3DCoedge, Edge);

	OutOrientation = CoEdgeData->m_ucOrientationUVWithLoop > 0 ? CADKernel::EOrientation::Front : CADKernel::EOrientation::Back;

	// Link edges
	if (CoEdgeData->m_pNeighbor)
	{
		const A3DTopoCoEdge* Neighbor = CoEdgeData->m_pNeighbor;
		while (Neighbor && Neighbor != A3DCoedge)
		{
			TSharedPtr<CADKernel::FTopologicalEdge>* TwinEdge = A3DEdgeToEdge.Find(Neighbor);
			if (TwinEdge != nullptr)
			{
				Edge->Link(*TwinEdge->Get(), SquareJoiningVertexTolerance);
			}

			TUniqueTSObj<A3DTopoCoEdgeData> NeighborData(Neighbor);
			if (NeighborData.IsValid())
			{
				Neighbor = NeighborData->m_pNeighbor;
			}
			else
			{
				break;
			}
		}
	}

	return Edge;
}

TSharedPtr<CADKernel::FTopologicalLoop> FTechSoftBridge::AddLoop(const A3DTopoLoop* A3DLoop, const TSharedRef<CADKernel::FSurface>& Surface, const TechSoftUtils::FUVReparameterization& UVReparameterization, const bool bIsExternalLoop)
{
	Report.LoopCount++;

	TArray<TSharedPtr<CADKernel::FTopologicalEdge>> Edges;
	TArray<CADKernel::EOrientation> Directions;

	TUniqueTSObj<A3DTopoLoopData> TopoLoopData(A3DLoop);
	if (!TopoLoopData.IsValid())
	{
		Report.DegeneratedLoopCount++;
		return TSharedPtr<CADKernel::FTopologicalLoop>();
	}

	bool bLoopOrientation = (bool)TopoLoopData->m_ucOrientationWithSurface;
	for (A3DUns32 Index = 0; Index < TopoLoopData->m_uiCoEdgeSize; ++Index)
	{
		CADKernel::EOrientation Orientation;
		TSharedPtr<CADKernel::FTopologicalEdge> Edge = AddEdge(TopoLoopData->m_ppCoEdges[Index], Surface, UVReparameterization, Orientation);
		if (!Edge.IsValid())
		{
			Report.DegeneratedEdgeCount++;
			continue;
		}

		Edges.Emplace(Edge);
		Directions.Emplace(Orientation);
	}

	if (Edges.Num() == 0)
	{
		Report.DegeneratedLoopCount++;
		return TSharedPtr<CADKernel::FTopologicalLoop>();
	}

	return CADKernel::FTopologicalLoop::Make(Edges, Directions, bIsExternalLoop, GeometricTolerance);
}

void FTechSoftBridge::AddFace(const A3DTopoFace* A3DFace, CADKernel::EOrientation Orientation, TSharedRef<CADKernel::FShell>& Shell, uint32 ShellIndex)
{
	Report.FaceCount++;

	FEntityMetaData MetaData;
	Parser.ExtractMetaData(A3DFace, MetaData);

	TUniqueTSObj<A3DTopoFaceData> TopoFaceData(A3DFace);
	if (!TopoFaceData.IsValid())
	{
		Report.FailedFaceCount++;
		return;
	}

	const A3DSurfBase* A3DSurface = TopoFaceData->m_pSurface;
	TechSoftUtils::FUVReparameterization UVReparameterization;
	TSharedPtr<CADKernel::FSurface> SurfacePtr = AddSurface(A3DSurface, UVReparameterization);
	if (!SurfacePtr.IsValid())
	{
		Report.DegeneratedSurfaceCount++;
		Report.FailedFaceCount++;
		return;
	}

	if (UVReparameterization.GetNeedSwapOrientation())
	{
		SwapOrientation(Orientation);
	}

	TSharedRef<CADKernel::FSurface> Surface = SurfacePtr.ToSharedRef();
	TSharedRef<CADKernel::FTopologicalFace> Face = CADKernel::FEntity::MakeShared<CADKernel::FTopologicalFace>(Surface);

	if (TopoFaceData->m_bHasTrimDomain)
	{
		CADKernel::FSurfacicBoundary SurfaceBoundary = TechSoftUtils::GetSurfacicBoundary(TopoFaceData->m_sSurfaceDomain, UVReparameterization);
		Surface->TrimBoundaryTo(SurfaceBoundary);
	}

	if (!TopoFaceData->m_uiLoopSize)
	{
		Face->ApplyNaturalLoops();
	}
	else
	{
		TArray<TSharedPtr<CADKernel::FTopologicalLoop>> Loops;

		for (A3DUns32 Index = 0; Index < TopoFaceData->m_uiLoopSize; ++Index)
		{
			const bool bIsExternalLoop = (Index == TopoFaceData->m_uiOuterLoopIndex);
			TSharedPtr<CADKernel::FTopologicalLoop> Loop = AddLoop(TopoFaceData->m_ppLoops[Index], Surface, UVReparameterization, bIsExternalLoop);
			if (!Loop.IsValid())
			{
				continue;
			}

			TArray<CADKernel::FPoint2D> LoopSampling;
			Loop->Get2DSampling(LoopSampling);
			CADKernel::FAABB2D Boundary;
			Boundary += LoopSampling;
			Loop->Boundary.Set(Boundary.GetMin(), Boundary.GetMax());

			// Check if the loop is not composed with only degenerated edge
			bool bDegeneratedLoop = true;
			for (const CADKernel::FOrientedEdge& Edge : Loop->GetEdges())
			{
				if (!Edge.Entity->IsDegenerated())
				{
					bDegeneratedLoop = false;
					break;
				}
			}
			if (bDegeneratedLoop)
			{
				continue;
			}

			Loops.Add(Loop);
		}

		if (Loops.Num() == 0)
		{
			Report.FailedFaceCount++;
			Face->SetAsDegenerated();
			Face->SetDeleted();
			return;
		}
		else
		{
			Face->AddLoops(Loops, Report.DoubtfulLoopOrientationCount);
		}
	}

	AddMetadata(MetaData, *Face);
	Face->SetHostId(ShellIndex);
	Shell->Add(Face, Orientation);
}

static bool bUseSurfaceAsNurbs = true;

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddSurface(const A3DSurfBase* A3DSurface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SurfaceCount++;

	FEntityMetaData MetaData;
	Parser.ExtractMetaData(A3DSurface, MetaData);

	A3DEEntityType Type;
	int32 Ret = TechSoftInterface::GetEntityType(A3DSurface, &Type);
	if (Ret == A3D_SUCCESS)
	{
		switch (Type)
		{
		case kA3DTypeSurfBlend01:
			return AddBlend01Surface(A3DSurface, OutUVReparameterization);
			
		case kA3DTypeSurfBlend02:
			return AddBlend02Surface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfBlend03:
			return AddBlend03Surface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfNurbs:
			return AddNurbsSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfCone:
			return AddConeSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfCylinder:
			return AddCylinderSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfCylindrical:
			return AddCylindricalSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfOffset:
			return AddOffsetSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfPipe:
			return AddPipeSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfPlane:
			return AddPlaneSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfRuled:
			return AddRuledSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfSphere:
			return AddSphereSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfRevolution:
			return AddRevolutionSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfExtrusion:
			return AddExtrusionSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfFromCurves:
			return AddSurfaceFromCurves(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfTorus:
			return AddTorusSurface(A3DSurface, OutUVReparameterization);

		case kA3DTypeSurfTransform:
			return AddTransformSurface(A3DSurface, OutUVReparameterization);

		default:
			return AddSurfaceAsNurbs(A3DSurface, OutUVReparameterization);
		}
	}
	else if (Ret == A3D_NOT_IMPLEMENTED)
	{
		return AddSurfaceAsNurbs(A3DSurface, OutUVReparameterization);
	}
	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddConeSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.ConeSurfaceCount++;

	TUniqueTSObj<A3DSurfConeData> A3DConeData(Surface);
	if (!A3DConeData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DConeData->m_sParam);
	OutUVReparameterization.ScaleUVTransform(1, BodyScale);
	if (A3DConeData->m_dSemiAngle < 0)
	{
		OutUVReparameterization.SetNeedSwapOrientation();
	}

	CADKernel::FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DConeData->m_sTrsf, BodyScale);
	CADKernel::FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DConeData->m_sParam.m_sUVDomain, OutUVReparameterization);
	return CADKernel::FSurface::MakeConeSurface(GeometricTolerance, CoordinateSystem, A3DConeData->m_dRadius * BodyScale, A3DConeData->m_dSemiAngle, Boundary);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddCylinderSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.CylinderSurfaceCount++;

	TUniqueTSObj<A3DSurfCylinderData> A3DCylinderData(Surface);
	if (!A3DCylinderData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DCylinderData->m_sParam);
	OutUVReparameterization.ScaleUVTransform(1, BodyScale);

	CADKernel::FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DCylinderData->m_sTrsf, BodyScale);
	CADKernel::FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DCylinderData->m_sParam.m_sUVDomain, OutUVReparameterization);
	return CADKernel::FSurface::MakeCylinderSurface(GeometricTolerance, CoordinateSystem, A3DCylinderData->m_dRadius * BodyScale, Boundary);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddLinearTransfoSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.LinearTransfoSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}
	// Todo
	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddNurbsSurface(const A3DSurfNurbs* Nurbs, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.NurbsSurfaceCount++;

	TUniqueTSObj<A3DSurfNurbsData> A3DNurbsData(Nurbs);
	if (!A3DNurbsData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return AddSurfaceNurbs(*A3DNurbsData, OutUVReparameterization);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddOffsetSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.OffsetSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddPlaneSurface(const A3DSurfPlane* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.PlaneSurfaceCount++;

	TUniqueTSObj<A3DSurfPlaneData> A3DPlaneData(Surface);
	if (!A3DPlaneData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DPlaneData->m_sParam);
	OutUVReparameterization.ScaleUVTransform(BodyScale, BodyScale);
	
	CADKernel::FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DPlaneData->m_sTrsf, BodyScale);
	CADKernel::FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DPlaneData->m_sParam.m_sUVDomain, OutUVReparameterization);
	return CADKernel::FSurface::MakePlaneSurface(GeometricTolerance, CoordinateSystem, Boundary);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddRevolutionSurface(const A3DSurfRevolution* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.RevolutionSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfRevolutionData> A3DRevolutionData(Surface);
	if (!A3DRevolutionData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddRuledSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.RuledSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfRuledData> A3DRuledData(Surface);
	if (!A3DRuledData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddSphereSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SphereSurfaceCount++;

	TUniqueTSObj<A3DSurfSphereData> A3DSphereData(Surface);
	if (!A3DSphereData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DSphereData->m_sParam);

	CADKernel::FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DSphereData->m_sTrsf, BodyScale);
	CADKernel::FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DSphereData->m_sParam.m_sUVDomain, OutUVReparameterization);
	return CADKernel::FSurface::MakeSphericalSurface(GeometricTolerance, CoordinateSystem, A3DSphereData->m_dRadius * BodyScale, Boundary);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddTorusSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.TorusSurfaceCount++;

	TUniqueTSObj<A3DSurfTorusData> A3DTorusData(Surface);
	if (!A3DTorusData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	OutUVReparameterization.AddUVTransform(A3DTorusData->m_sParam);
	CADKernel::FMatrixH CoordinateSystem = TechSoftUtils::CreateCoordinateSystem(A3DTorusData->m_sTrsf, BodyScale);
	CADKernel::FSurfacicBoundary Boundary = TechSoftUtils::GetSurfacicBoundary(A3DTorusData->m_sParam.m_sUVDomain, OutUVReparameterization);
	return CADKernel::FSurface::MakeTorusSurface(GeometricTolerance, CoordinateSystem, A3DTorusData->m_dMajorRadius * BodyScale, A3DTorusData->m_dMinorRadius * BodyScale, Boundary);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddBlend01Surface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.Blend01SurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfBlend01Data> A3DBlend01Data(Surface);
	if (!A3DBlend01Data.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddBlend02Surface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.Blend02SurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfBlend02Data> A3DBlend02Data(Surface);
	if (!A3DBlend02Data.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddBlend03Surface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.Blend03SurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddCylindricalSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.CylindricalSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfCylindricalData> A3DCylindricalData(Surface);
	if (!A3DCylindricalData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddPipeSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.PipeSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfPipeData> A3DPipeData(Surface);
	if (!A3DPipeData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddExtrusionSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.ExtrusionSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfExtrusionData> A3DExtrusionData(Surface);
	if (!A3DExtrusionData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddSurfaceFromCurves(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SurfaceFromCurvesCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfFromCurvesData> A3DFromCurvesData(Surface);
	if (!A3DFromCurvesData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddTransformSurface(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.TransformSurfaceCount++;

	if (bUseSurfaceAsNurbs)
	{
		return AddSurfaceAsNurbs(Surface, OutUVReparameterization);
	}

	TUniqueTSObj<A3DSurfFromCurvesData> A3DTransformData(Surface);
	if (!A3DTransformData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return TSharedPtr<CADKernel::FSurface>();
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddSurfaceNurbs(const A3DSurfNurbsData& A3DNurbsData, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	CADKernel::FNurbsSurfaceData NurbsData;

	NurbsData.PoleUCount = A3DNurbsData.m_uiUCtrlSize;
	NurbsData.PoleVCount = A3DNurbsData.m_uiVCtrlSize;
	int32 PoleCount = A3DNurbsData.m_uiUCtrlSize * A3DNurbsData.m_uiVCtrlSize;

	NurbsData.UDegree = A3DNurbsData.m_uiUDegree;
	NurbsData.VDegree = A3DNurbsData.m_uiVDegree;

	TechSoftUtils::FillDoubleArray(A3DNurbsData.m_uiUKnotSize, A3DNurbsData.m_pdUKnots, NurbsData.UNodalVector);
	TechSoftUtils::FillDoubleArray(A3DNurbsData.m_uiVKnotSize, A3DNurbsData.m_pdVKnots, NurbsData.VNodalVector);

	TArray<CADKernel::FPoint> Poles;
	TechSoftUtils::FillPointArray(NurbsData.PoleUCount, NurbsData.PoleVCount, A3DNurbsData.m_pCtrlPts, NurbsData.Poles);
	if (!FMath::IsNearlyEqual(BodyScale, 1.))
	{
		for (CADKernel::FPoint& Point : NurbsData.Poles)
		{
			Point *= BodyScale;
		}
	}

	bool bIsRational = false;
	if (A3DNurbsData.m_pdWeights)
	{
		bIsRational = true;
		TechSoftUtils::FillDoubleArray(NurbsData.PoleUCount, NurbsData.PoleVCount, A3DNurbsData.m_pdWeights, NurbsData.Weights);
	}

	return CADKernel::FSurface::MakeNurbsSurface(GeometricTolerance, NurbsData);
}

TSharedPtr<CADKernel::FSurface> FTechSoftBridge::AddSurfaceAsNurbs(const A3DSurfBase* Surface, TechSoftUtils::FUVReparameterization& OutUVReparameterization)
{
	Report.SurfaceAsNurbsCount++;

	TUniqueTSObj<A3DSurfNurbsData> A3DNurbsData;

	A3DDouble Tolerance = 1e-3;
	A3DBool bUseSameParameterization = true;
	A3DNurbsData.FillWith(&TechSoftUtils::GetSurfaceAsNurbs, Surface, Tolerance, bUseSameParameterization);

	if (!A3DNurbsData.IsValid())
	{
		return TSharedPtr<CADKernel::FSurface>();
	}

	return AddSurfaceNurbs(*A3DNurbsData, OutUVReparameterization);

}

void FTechSoftBridge::AddMetadata(FEntityMetaData& MetaData, CADKernel::FTopologicalShapeEntity& Entity)
{
	FString* Name = MetaData.MetaData.Find(TEXT("Name"));
	if(Name != nullptr)
	{
		Entity.SetName(*Name);
	}
	Entity.SetColorId(MetaData.ColorName);
	Entity.SetMaterialId(MetaData.MaterialName);
}

}


#endif // USE_TECHSOFT_SDK
