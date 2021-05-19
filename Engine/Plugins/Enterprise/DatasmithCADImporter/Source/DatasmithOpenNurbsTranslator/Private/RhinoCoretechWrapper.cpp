// Copyright Epic Games, Inc. All Rights Reserved.

#include "RhinoCoretechWrapper.h"


#ifdef USE_OPENNURBS
#include "CoreTechHelper.h"

#pragma warning(push)
#pragma warning(disable:4265)
#pragma warning(disable:4005) // TEXT macro redefinition
#include "opennurbs.h"
#pragma warning(pop)

#include <vector>

// As of 2018 SP2, CoreTech tessellation for a face is broken when the outer loop is the whole uv range, and some edges are singularities.
// In those cases, inner loops behave like outers, outer is ignored.
// current fix will split the uv plane in two parts.
#define FIX_HOLE_IN_WHOLE_FACE 1

TWeakPtr<FRhinoCoretechWrapper> FRhinoCoretechWrapper::SharedSession;

template<class T>
void MarkUninitializedMemory(std::vector<T>& VectorOfT)
{
#if MARK_UNINITIALIZED_MEMORY
	for (auto& Value : VectorOfT)
		Value = T(-1);
#endif // MARK_UNINITIALIZED_MEMORY
};

// Handle surface state without u/v duplication
// #ue_opennurbs : Remove SurfaceInfo and use CADLibrary::FNurbsSurface directly 
struct SurfaceInfo
{
	ON_NurbsSurface& surf;

	enum Axis { U, V };
	struct PerAxisInfo
	{
		uint32 Order; // Degree + 1
		uint32 CtrlVertCount; // number of control points
		uint32 KnotSize;  // ON knots
		uint32 KnotCount; // CT knots

		TArray<uint32> KnotsMult; // from ON, not relevant as we send n-plicated knots to CT (dbg only)
		TArray<uint32> KnotMul; // array of '1' for CT...
		TArray<double> Knots; // t values with superflux values

		PerAxisInfo(Axis InA, ON_NurbsSurface& InSurf)
			: surf(InSurf)
			, a(InA)
		{
			Populate();
		}

		// increasing a nurbs degree doesn't change the shape, but this operation generates a new hull with new weights.
		// This can fix exotic nurbs
		void IncreaseDegree()
		{
			surf.IncreaseDegree(a, surf.Degree(a) + 1);
			Populate();
		}

		// detect cases not handled by CT, that is knot vectors with multiplicity < order on either end
		bool FixMultiplicity()
		{
			if (KnotsMult[0] + 1 < Order || KnotsMult.Last() + 1 < Order)
			{
				IncreaseDegree();
				return true;
			}
			return false;
		}

	private:
		void Populate()
		{
			Order = surf.Order(a);
			CtrlVertCount = surf.CVCount(a);
			KnotSize = Order + CtrlVertCount;
			KnotCount = surf.KnotCount(a);

			KnotsMult.Init(-1, KnotSize - 2);
			//MarkUninitializedMemory(KnotsMult);
			for (uint32 i = 0; i < KnotSize - 2; ++i)
				KnotsMult[i] = surf.KnotMultiplicity(a, i); // 0 and < Order + CV_count - 2

			Knots.Init(-1, KnotSize);
			//MarkUninitializedMemory(Knots);
			Knots[0] = surf.SuperfluousKnot(a, 0);
			for (uint32 i = 0; i < KnotCount; ++i)
				Knots[i + 1] = surf.Knot(a, i);
			Knots.Last() = surf.SuperfluousKnot(a, 1);

			KnotMul.Init(1, KnotSize);
		}

		ON_NurbsSurface& surf;
		Axis a;
	};

	PerAxisInfo u;
	PerAxisInfo v;
	int CtrlVertDim; // Number of doubles per ctrl vertex
	TArray<double> CtrlHull; // [xyzw...]

	SurfaceInfo(ON_NurbsSurface& inSurf)
		: surf(inSurf)
		, u(U, surf)
		, v(V, surf)
		, CtrlVertDim(surf.CVSize())
	{
		BuildHull();
	}

	void BuildHull()
	{
		CtrlHull.Init(-1, u.CtrlVertCount * v.CtrlVertCount * CtrlVertDim);
		double* CtrlHullPtr = CtrlHull.GetData();
		ON::point_style pt_style = surf.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational;
		for (uint32 UIndex = 0; UIndex < u.CtrlVertCount; ++UIndex)
		{
			for (uint32 VIndex = 0; VIndex < v.CtrlVertCount; ++VIndex, CtrlHullPtr += CtrlVertDim)
			{
				surf.GetCV(UIndex, VIndex, pt_style, CtrlHullPtr);
			}
		}
	}

	// CT doesn't allow weights < 0
	void FixNegativeWeights()
	{
		if (surf.IsRational())
		{
			bool hasNegativeWeight = false;
			for (uint32 cvPointIndex = 0; cvPointIndex < u.CtrlVertCount * v.CtrlVertCount; ++cvPointIndex)
			{
				double pointDataOffset = cvPointIndex * CtrlVertDim;
				double weight = CtrlHull[pointDataOffset + CtrlVertDim - 1];
				if (weight < 0.)
				{
					hasNegativeWeight = true;
					break;
				}
			}

			if (hasNegativeWeight)
			{
				u.IncreaseDegree();
				v.IncreaseDegree();
				BuildHull();
			}
		}
	}

	// CT doesn't allow multiplicity < order
	void FixUnsupportedMultiplicity()
	{
		bool uOrderIncreased = u.FixMultiplicity();
		bool vOrderIncreased = v.FixMultiplicity();
		if (uOrderIncreased || vOrderIncreased)
			BuildHull();
	}

	void FixUnsupportedParameters()
	{
		FixNegativeWeights();
		FixUnsupportedMultiplicity();
	}
};


uint64 BRepToKernelIOBodyTranslator::CreateCTSurface(ON_NurbsSurface& Surface)
{
	if (Surface.Dimension() < 3)
		return 0;

	SurfaceInfo si(Surface);
	si.FixUnsupportedParameters();

	CADLibrary::FNurbsSurface CTSurface;

	CTSurface.OrderU = si.u.Order;
	CTSurface.OrderV = si.v.Order;
	CTSurface.KnotSizeU = si.u.KnotSize;
	CTSurface.KnotSizeV = si.v.KnotSize;
	CTSurface.ControlPointDimension = si.CtrlVertDim;
	CTSurface.ControlPointSizeU = si.u.CtrlVertCount;
	CTSurface.ControlPointSizeV = si.v.CtrlVertCount;

	CTSurface.KnotValuesU = MoveTemp(si.u.Knots);
	CTSurface.KnotValuesV = MoveTemp(si.v.Knots);
	CTSurface.KnotMultiplicityU = MoveTemp(si.u.KnotMul);
	CTSurface.KnotMultiplicityV = MoveTemp(si.v.KnotMul);
	CTSurface.ControlPoints = MoveTemp(si.CtrlHull);

	uint64 CTSurfaceID = 0;
	return CADLibrary::CTKIO_CreateNurbsSurface(CTSurface, CTSurfaceID) ? CTSurfaceID : 0;
}

void BRepToKernelIOBodyTranslator::CreateCTFace_internal(const ON_BrepFace& Face, TArray<uint64>& dest, ON_BoundingBox& outerBBox, ON_NurbsSurface& Surface, bool ignoreInner)
{
	uint64 SurfaceID = CreateCTSurface(Surface);
	if (SurfaceID == 0)
		return;

	ON_BoundingBox realOuterBBox = Face.OuterLoop()->BoundingBox();
	bool outerRedefined = realOuterBBox != outerBBox;

	int LoopCount = ignoreInner ? 1 : Face.LoopCount();
	TArray<uint64> Loops;
	Loops.Reserve(LoopCount);
	for (int LoopIndex = 0; LoopIndex < LoopCount; ++LoopIndex)
	{
		const ON_BrepLoop& on_Loop = *Face.Loop(LoopIndex);
		if (!on_Loop.IsValid())
			continue;

		ON_BrepLoop::TYPE onLoopType = on_Loop.m_type;
		bool bIsOuter = (onLoopType == ON_BrepLoop::TYPE::outer);
		//assert(bIsOuter == (LoopIndex == 0));

		int TrimCount = on_Loop.TrimCount();
		TArray<uint64> Coedges;
		Coedges.Reserve(TrimCount);
		for (int i = 0; i < TrimCount; ++i)
		{
			ON_BrepTrim& Trim = *on_Loop.Trim(i);

			ON_BrepEdge* on_edge = Trim.Edge();
			if (on_edge == nullptr)
				continue;

			ON_NurbsCurve nurbs_curve;
			int nurbFormSuccess = Trim.GetNurbForm(nurbs_curve); // 0:Nok 1:Ok 2:OkBut
			if (nurbFormSuccess == 0)
				continue;

			uint64 NewCoedge;
			if (!CADLibrary::CTKIO_CreateCoedge(Trim.m_bRev3d, NewCoedge))
			{
				continue;
			}

			BrepTrimToCoedge[Trim.m_trim_index] = NewCoedge;

			// Find a Trim that use this edge, that is not current Trim
			// If the Trim has been converted into a coedge, link both Trims
			for (int32 Index = 0; Index < on_edge->m_ti.Count(); ++Index)
			{
				int32 LinkedEdgeIndex = on_edge->m_ti[Index];
				if (LinkedEdgeIndex == Trim.m_trim_index)
				{
					continue;
				}

				uint64 LinkedCoedgeId = BrepTrimToCoedge[LinkedEdgeIndex];
				if (LinkedCoedgeId)
				{
					CADLibrary::CTKIO_MatchCoedges(LinkedCoedgeId, NewCoedge);
					break;
				}
			}

			CADLibrary::FNurbsCurve CTCurve;
			CTCurve.Order = nurbs_curve.Order();

			int KnotCount = nurbs_curve.KnotCount();
			int CtrlVertCount = nurbs_curve.CVCount();
			CTCurve.KnotSize = CTCurve.Order + CtrlVertCount; // cvCount + degree - 1 for OpenNurb, cvCount + degree + 1 for OpenNurb,

			// knot data
			CTCurve.KnotValues.SetNum(CTCurve.KnotSize);
			CTCurve.KnotValues[0] = nurbs_curve.SuperfluousKnot(0);
			for (int j = 0; j < KnotCount; ++j)
			{
				CTCurve.KnotValues[j + 1] = nurbs_curve.Knot(j);
			}
			CTCurve.KnotValues.Last() = nurbs_curve.SuperfluousKnot(1);

			// Control hull
			CTCurve.ControlPointDimension = nurbs_curve.CVSize(); // = IsRational() ? Dim()+1 : Dim()
			CTCurve.ControlPointSize = nurbs_curve.CVCount();
			CTCurve.ControlPoints.SetNum(CTCurve.ControlPointSize * CTCurve.ControlPointDimension);
			double* ControlPoints = CTCurve.ControlPoints.GetData();
			for (uint32 j = 0; j < CTCurve.ControlPointSize; ++j, ControlPoints += CTCurve.ControlPointDimension)
			{
				nurbs_curve.GetCV(j, nurbs_curve.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational, ControlPoints);
			}
			if (outerRedefined && bIsOuter)
			{
				for (int32 j = 0; j < CTCurve.ControlPoints.Num(); j += CTCurve.ControlPointSize)
				{
					CTCurve.ControlPoints[j] = min(max(CTCurve.ControlPoints[j], outerBBox.m_min.x), outerBBox.m_max.x);
				}
			}

			// knot multiplicity (ignored as knots are stored multiple times already)
			CTCurve.KnotMultiplicity.Init(1, CTCurve.KnotSize);

			ON_Interval dom = nurbs_curve.Domain();
			if (!CADLibrary::CTKIO_SetUVCurve(CTCurve, dom.m_t[0], dom.m_t[1], NewCoedge))
			{
				continue;
			}

			Coedges.Add(NewCoedge);
		}

		uint64 Loop;
		if (!CADLibrary::CTKIO_CreateLoop(Coedges, Loop))
		{
			continue;
		}

		Loops.Add(Loop);
	}

	uint64 FaceID;
	if (CADLibrary::CTKIO_CreateFace(SurfaceID, true, Loops, FaceID))
	{
		dest.Add(FaceID);
	}
}

void BRepToKernelIOBodyTranslator::CreateCTFace(const ON_BrepFace& Face, TArray<uint64>& dest)
{
	const ON_BrepLoop* OuterLoop = Face.OuterLoop();
	if (OuterLoop == nullptr)
	{
		return;
	}

	ON_NurbsSurface Surface;
	Face.NurbsSurface(&Surface);

	ON_BoundingBox outerBBox = Face.OuterLoop()->BoundingBox();

#if FIX_HOLE_IN_WHOLE_FACE
	int LoopCount = Face.LoopCount();
	bool bBadLoopHack = BRep.LoopIsSurfaceBoundary(OuterLoop->m_loop_index) && LoopCount >= 2;
	if (bBadLoopHack)
	{
		TArray<uint64> Coedges;
		int TrimCount = OuterLoop->TrimCount();
		bool bHasSingularTrim = false;
		for (int i = 0; i < TrimCount; ++i)
		{
			ON_BrepTrim& Trim = *OuterLoop->Trim(i);
			bHasSingularTrim |= (Trim.m_type == ON_BrepTrim::TYPE::singular);
		}
		bBadLoopHack &= bHasSingularTrim;
	}

	if (bBadLoopHack)
	{
		// we try to split in 2 faces: one west one east.
		double span = outerBBox.Diagonal().x;

		ON_BoundingBox innerBBox = Face.Loop(1)->BoundingBox();
		for (int LoopIndex = 2; LoopIndex < LoopCount; ++LoopIndex)
		{
			const ON_BrepLoop& innerLoop = *Face.Loop(LoopIndex);
			innerBBox.Union(innerLoop.BoundingBox());
		}
		double spaceWest = innerBBox.Min().x - outerBBox.Min().x;
		double spaceEast = outerBBox.Max().x - innerBBox.Max().x;
		double e = 0.01;
		if (spaceEast < e*span && spaceWest < e*span)
		{
			// can't split... We could ignore inner loops:
			CreateCTFace_internal(Face, dest, outerBBox, Surface, true);
			return;
		}

		// split the outer bbox in a normal part (with outer) and a 'rest' (with no iner)
		ON_BoundingBox outerTrimedPartBBox = outerBBox;
		if (spaceEast > spaceWest)
		{
			outerBBox.m_max.x -= 0.5 * spaceEast;
			outerTrimedPartBBox.m_min.x = outerBBox.m_max.x;
		}
		else
		{
			outerBBox.m_min.x += 0.5 * spaceWest;
			outerTrimedPartBBox.m_max.x = outerBBox.m_min.x;
		}

		// We now need to create a bonus face to handle the hole we just created
		CreateCTFace_internal(Face, dest, outerTrimedPartBBox, Surface, true);
	}
#endif // FIX_HOLE_IN_WHOLE_FACE

	CreateCTFace_internal(Face, dest, outerBBox, Surface, false);
}


bool FRhinoCoretechWrapper::Tessellate(FMeshDescription& Mesh, CADLibrary::FMeshParameters& MeshParameters)
{
	return CADLibrary::Tessellate(MainObjectId, ImportParams, Mesh, MeshParameters);
}

uint64 BRepToKernelIOBodyTranslator::CreateBody(const ON_3dVector& Offset)
{
	BrepTrimToCoedge.SetNumZeroed(BRep.m_T.Count());

	double boxmin[3];
	double boxmax[3];
	BRep.GetBBox(boxmin, boxmax);

	BRep.Translate(Offset);
	BRep.GetBBox(boxmin, boxmax);

	// Create ct faces
	BRep.FlipReversedSurfaces();
	int FaceCount = BRep.m_F.Count();
	TArray<uint64> FaceList;
	FaceList.Reserve(FaceCount);
	for (int index = 0; index < FaceCount; index++)
	{
		const ON_BrepFace& On_face = BRep.m_F[index];
		CreateCTFace(On_face, FaceList);
	}
	BRep.Translate(-Offset);

	if (FaceList.Num() == 0)
	{
		return 0;
	}

	// Create body from faces
	uint64 BodyID;
	return CADLibrary::CTKIO_CreateBody(FaceList, BodyID) ? BodyID : 0;
}

bool FRhinoCoretechWrapper::AddBRep(ON_Brep& Brep, const ON_3dVector& Offset)
{
	if (!IsSessionValid())
	{
		ensureMsgf(false, TEXT("bad session init"));
		return false;
	}

	BRepToKernelIOBodyTranslator BodyTranslator(Brep);
	uint64 BodyID = BodyTranslator.CreateBody(Offset);

	return BodyID ? CADLibrary::CTKIO_AddBodies({ BodyID }, MainObjectId) : false;
}



TSharedPtr<FRhinoCoretechWrapper> FRhinoCoretechWrapper::GetSharedSession()
{
	TSharedPtr<FRhinoCoretechWrapper> Session = SharedSession.Pin();
	if (!Session.IsValid())
	{
		Session = MakeShared<FRhinoCoretechWrapper>(TEXT("Rh2CTSharedSession"));
		SharedSession = Session;
	}
	return Session;
}

#endif // defined(USE_OPENNURBS)
