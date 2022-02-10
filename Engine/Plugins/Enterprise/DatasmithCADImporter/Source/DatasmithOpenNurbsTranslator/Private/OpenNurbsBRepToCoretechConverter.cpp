// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenNurbsBRepToCoretechConverter.h"

#ifdef USE_OPENNURBS
#include "CoreTechSurfaceHelper.h"

#pragma warning(push)
#pragma warning(disable:4265)
#pragma warning(disable:4005) // TEXT macro redefinition
#include "opennurbs.h"
#pragma warning(pop)

// As of 2018 SP2, CoreTech tessellation for a face is broken when the outer loop is the whole uv range, and some edges are singularities.
// In those cases, inner loops behave like outers, outer is ignored.
// current fix will split the uv plane in two parts.
#define FIX_HOLE_IN_WHOLE_FACE 1

// Handle surface state without u/v duplication
// #ue_opennurbs : Remove SurfaceInfo and use CADLibrary::FNurbsSurface directly 
struct SurfaceInfo
{
	ON_NurbsSurface& OpenNurbssSurface;

	enum EAxis { U, V };
	struct PerAxisInfo
	{
		uint32 Order; // Degree + 1
		uint32 CtrlVertCount; // number of control points
		uint32 KnotSize;  // ON knots
		uint32 KnotCount; // CT knots

		TArray<uint32> KnotMultiplicities; // from ON, not relevant as we send n-plicated knots to CT (dbg only)
		TArray<uint32> CTKnotMultiplicityArray; // array of '1' for CT...
		TArray<double> Knots; // t values with superflux values

		PerAxisInfo(EAxis InA, ON_NurbsSurface& InSurf)
			: OpenNurbsSurface(InSurf)
			, Axis(InA)
		{
			Populate();
		}

		// increasing a nurbs degree doesn't change the shape, but this operation generates a new hull with new weights.
		// This can fix exotic nurbs
		void IncreaseDegree()
		{
			OpenNurbsSurface.IncreaseDegree(Axis, OpenNurbsSurface.Degree(Axis) + 1);
			Populate();
		}

		// detect cases not handled by CT, that is knot vectors with multiplicity < order on either end
		bool FixMultiplicity()
		{
			if (KnotMultiplicities[0] + 1 < Order || KnotMultiplicities.Last() + 1 < Order)
			{
				IncreaseDegree();
				return true;
			}
			return false;
		}

	private:
		void Populate()
		{
			Order = OpenNurbsSurface.Order(Axis);
			CtrlVertCount = OpenNurbsSurface.CVCount(Axis);
			KnotSize = Order + CtrlVertCount;
			KnotCount = OpenNurbsSurface.KnotCount(Axis);

			KnotMultiplicities.Init(-1, KnotSize - 2);
			for (uint32 Index = 0; Index < KnotSize - 2; ++Index)
			{
				KnotMultiplicities[Index] = OpenNurbsSurface.KnotMultiplicity(Axis, Index); // 0 and < Order + CV_count - 2
			}

			Knots.Init(-1, KnotSize);
			Knots[0] = OpenNurbsSurface.SuperfluousKnot(Axis, 0);
			for (uint32 Index = 0; Index < KnotCount; ++Index)
			{
				Knots[Index + 1] = OpenNurbsSurface.Knot(Axis, Index);
			}
			Knots.Last() = OpenNurbsSurface.SuperfluousKnot(Axis, 1);

			CTKnotMultiplicityArray.Init(1, KnotSize);
		}

		ON_NurbsSurface& OpenNurbsSurface;
		EAxis Axis;
	};

	PerAxisInfo UInfo;
	PerAxisInfo VInfo;
	int ControlPointDimension; // Number of doubles per ctrl vertex
	TArray<double> ControlPoints; // [xyzw...]

	SurfaceInfo(ON_NurbsSurface& inSurf)
		: OpenNurbssSurface(inSurf)
		, UInfo(U, OpenNurbssSurface)
		, VInfo(V, OpenNurbssSurface)
		, ControlPointDimension(OpenNurbssSurface.CVSize())
	{
		BuildHull();
	}

	void BuildHull()
	{
		ControlPoints.Init(-1, UInfo.CtrlVertCount * VInfo.CtrlVertCount * ControlPointDimension);
		double* CtrlHullPtr = ControlPoints.GetData();
		ON::point_style pt_style = OpenNurbssSurface.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational;
		for (uint32 UIndex = 0; UIndex < UInfo.CtrlVertCount; ++UIndex)
		{
			for (uint32 VIndex = 0; VIndex < VInfo.CtrlVertCount; ++VIndex, CtrlHullPtr += ControlPointDimension)
			{
				OpenNurbssSurface.GetCV(UIndex, VIndex, pt_style, CtrlHullPtr);
			}
		}
	}

	// CT doesn't allow weights < 0
	void FixNegativeWeights()
	{
		if (OpenNurbssSurface.IsRational())
		{
			bool bHasNegativeWeight = false;
			for (uint32 PointIndex = 0; PointIndex < UInfo.CtrlVertCount * VInfo.CtrlVertCount; ++PointIndex)
			{
				double PointDataOffset = PointIndex * ControlPointDimension;
				double Weight = ControlPoints[PointDataOffset + ControlPointDimension - 1];
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
	}

	// CT doesn't allow multiplicity < order
	void FixUnsupportedMultiplicity()
	{
		bool bUOrderIncreased = UInfo.FixMultiplicity();
		bool bVOrderIncreased = VInfo.FixMultiplicity();
		if (bUOrderIncreased || bVOrderIncreased)
		{
			BuildHull();
		}
	}

	void FixUnsupportedParameters()
	{
		FixNegativeWeights();
		FixUnsupportedMultiplicity();
	}
};


uint64 FBRepToKernelIOBodyTranslator::CreateCTSurface(ON_NurbsSurface& Surface)
{
	if (Surface.Dimension() < 3)
		return 0;

	SurfaceInfo SurfaceInfo(Surface);
	SurfaceInfo.FixUnsupportedParameters();

	CADLibrary::FNurbsSurface CTSurface;

	CTSurface.OrderU = SurfaceInfo.UInfo.Order;
	CTSurface.OrderV = SurfaceInfo.VInfo.Order;
	CTSurface.KnotSizeU = SurfaceInfo.UInfo.KnotSize;
	CTSurface.KnotSizeV = SurfaceInfo.VInfo.KnotSize;
	CTSurface.ControlPointDimension = SurfaceInfo.ControlPointDimension;
	CTSurface.ControlPointSizeU = SurfaceInfo.UInfo.CtrlVertCount;
	CTSurface.ControlPointSizeV = SurfaceInfo.VInfo.CtrlVertCount;

	CTSurface.KnotValuesU = MoveTemp(SurfaceInfo.UInfo.Knots);
	CTSurface.KnotValuesV = MoveTemp(SurfaceInfo.VInfo.Knots);
	CTSurface.KnotMultiplicityU = MoveTemp(SurfaceInfo.UInfo.CTKnotMultiplicityArray);
	CTSurface.KnotMultiplicityV = MoveTemp(SurfaceInfo.VInfo.CTKnotMultiplicityArray);
	CTSurface.ControlPoints = MoveTemp(SurfaceInfo.ControlPoints);

	uint64 CTSurfaceID = 0;
	return CADLibrary::CTKIO_CreateNurbsSurface(CTSurface, CTSurfaceID) ? CTSurfaceID : 0;
}

void FBRepToKernelIOBodyTranslator::CreateCTFace_internal(const ON_BrepFace& Face, TArray<uint64>& dest, ON_BoundingBox& outerBBox, ON_NurbsSurface& Surface, bool ignoreInner)
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
					CTCurve.ControlPoints[j] = FMath::Min(FMath::Max(CTCurve.ControlPoints[j], outerBBox.m_min.x), outerBBox.m_max.x);
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

void FBRepToKernelIOBodyTranslator::CreateCTFace(const ON_BrepFace& Face, TArray<uint64>& dest)
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

uint64 FBRepToKernelIOBodyTranslator::CreateBody(const ON_3dVector& Offset)
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

bool FOpenNurbsBRepToCoretechConverter::AddBRep(ON_Brep& Brep, const ON_3dVector& Offset)
{
	if (!IsCoreTechSessionValid())
	{
		ensureMsgf(false, TEXT("bad session init"));
		return false;
	}

	FBRepToKernelIOBodyTranslator BodyTranslator(Brep);
	uint64 BodyID = BodyTranslator.CreateBody(Offset);

	return BodyID ? CADLibrary::CTKIO_AddBodies({ BodyID }, MainObjectId) : false;
}

#endif // defined(USE_OPENNURBS)
