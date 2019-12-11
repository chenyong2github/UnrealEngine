// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RhinoCoretechWrapper.h"


#ifdef CAD_LIBRARY
#include "CoreTechHelper.h"
//#include "TessellationHelper.h"

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
struct SurfaceInfo
{
	ON_NurbsSurface& surf;

	enum Axis { U, V };
	struct PerAxisInfo
	{
		int Order; // Degree + 1
		int CtrlVertCount; // number of control points
		int KnotSize;  // ON knots
		int KnotCount; // CT knots

		std::vector<int> KnotsMult; // from ON, not relevant as we send n-plicated knots to CT (dbg only)
		std::vector<CT_UINT32> KnotMul; // array of '1' for CT...
		std::vector<double> Knots; // t values with superflux values

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
			if (KnotsMult.front() + 1 < Order || KnotsMult.back() + 1 < Order)
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

			KnotsMult.resize(KnotSize - 2);
			MarkUninitializedMemory(KnotsMult);
			for (int i = 0; i < KnotSize - 2; ++i)
				KnotsMult[i] = surf.KnotMultiplicity(a, i); // 0 and < Order + CV_count - 2

			Knots.resize(KnotSize);
			MarkUninitializedMemory(Knots);
			Knots.front() = surf.SuperfluousKnot(a, 0);
			for (int i = 0; i < KnotCount; ++i)
				Knots[i + 1] = surf.Knot(a, i);
			Knots.back() = surf.SuperfluousKnot(a, 1);

			KnotMul.resize(KnotSize, 1);
		}

		ON_NurbsSurface& surf;
		Axis a;
	};

	PerAxisInfo u;
	PerAxisInfo v;
	int CtrlVertDim; // Number of doubles per ctrl vertex
	std::vector<double> CtrlHull; // [xyzw...]

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
		CtrlHull.resize(u.CtrlVertCount * v.CtrlVertCount * CtrlVertDim);
		MarkUninitializedMemory(CtrlHull);
		double* CtrlHullPtr = CtrlHull.data();
		ON::point_style pt_style = surf.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational;
		for (int UIndex = 0; UIndex < u.CtrlVertCount; ++UIndex)
		{
			for (int VIndex = 0; VIndex < v.CtrlVertCount; ++VIndex, CtrlHullPtr += CtrlVertDim)
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
			for (int cvPointIndex = 0; cvPointIndex < u.CtrlVertCount * v.CtrlVertCount; ++cvPointIndex)
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

CT_OBJECT_ID CreateCTSurface(ON_NurbsSurface& Surface)
{
	if (Surface.Dimension() < 3)
		return 0;

	SurfaceInfo si(Surface);
	si.FixUnsupportedParameters();

	CT_OBJECT_ID CTSurfaceID = 0;
	CADLibrary::CheckedCTError result = CT_SNURBS_IO::Create(CTSurfaceID,
		si.u.Order, si.v.Order,
		si.u.KnotSize, si.v.KnotSize,
		si.u.CtrlVertCount, si.v.CtrlVertCount,
		si.CtrlVertDim, si.CtrlHull.data(),
		si.u.Knots.data(), si.v.Knots.data(),
		si.u.KnotMul.data(), si.v.KnotMul.data()
	);

	return CTSurfaceID;
}

void CreateCTFace_internal(const ON_BrepFace& Face, CT_LIST_IO& dest, ON_BoundingBox& outerBBox, ON_NurbsSurface& Surface, bool ignoreInner)
{
	CT_OBJECT_ID SurfaceID = CreateCTSurface(Surface);
	if (SurfaceID == 0)
		return;

	ON_BoundingBox realOuterBBox = Face.OuterLoop()->BoundingBox();
	bool outerRedefined = realOuterBBox != outerBBox;

	int LoopCount = ignoreInner ? 1 : Face.LoopCount();
	CT_LIST_IO Loops;
	for (int LoopIndex = 0; LoopIndex < LoopCount; ++LoopIndex)
	{
		const ON_BrepLoop& on_Loop = *Face.Loop(LoopIndex);
		if (!on_Loop.IsValid())
			continue;

		ON_BrepLoop::TYPE onLoopType = on_Loop.m_type;
		bool bIsOuter = (onLoopType == ON_BrepLoop::TYPE::outer);
		//assert(bIsOuter == (LoopIndex == 0));

		CT_LIST_IO Coedges;
		int TrimCount = on_Loop.TrimCount();
		for (int i = 0; i < TrimCount; ++i)
		{
			CADLibrary::CheckedCTError err = IO_OK;
			ON_BrepTrim& Trim = *on_Loop.Trim(i);

			ON_BrepEdge* on_edge = Trim.Edge();
			if (on_edge == nullptr)
				continue;

			ON_NurbsCurve nurbs_curve;
			int nurbFormSuccess = Trim.GetNurbForm(nurbs_curve); // 0:Nok 1:Ok 2:OkBut
			if (nurbFormSuccess == 0)
				continue;

			CT_OBJECT_ID Coedge;
			err = CT_COEDGE_IO::Create(Coedge, Trim.m_bRev3d ? CT_ORIENTATION::CT_REVERSE : CT_ORIENTATION::CT_FORWARD);
			if (err != IO_OK)
				continue;

			// fill edge data
			CT_UINT32 order = nurbs_curve.Order();

			int KnotCount = nurbs_curve.KnotCount();
			int CtrlVertCount = nurbs_curve.CVCount();
			CT_UINT32 KnotSize = order + CtrlVertCount; // cvCount + degree - 1 for OpenNurb, cvCount + degree + 1 for OpenNurb,

			// knot data
			std::vector<double> Knots;
			Knots.resize(KnotSize);
			Knots.front() = nurbs_curve.SuperfluousKnot(0);
			for (int j = 0; j < KnotCount; ++j)
			{
				Knots[j + 1] = nurbs_curve.Knot(j);
			}
			Knots.back() = nurbs_curve.SuperfluousKnot(1);

			// Control hull
			int ctrl_hull_dim = nurbs_curve.CVSize(); // = IsRational() ? Dim()+1 : Dim()
			int ctrl_hull_size = nurbs_curve.CVCount();
			std::vector<double> cv_data;
			cv_data.resize(ctrl_hull_size * ctrl_hull_dim);
			double* wp = cv_data.data();
			for (int j = 0; j < ctrl_hull_size; ++j, wp += ctrl_hull_dim)
			{
				nurbs_curve.GetCV(j, nurbs_curve.IsRational() ? ON::point_style::euclidean_rational : ON::point_style::not_rational, wp);
			}
			if (outerRedefined && bIsOuter)
			{
				for (int j = 0; j < cv_data.size(); j += ctrl_hull_dim)
				{
					cv_data[j] = min(max(cv_data[j], outerBBox.m_min.x), outerBBox.m_max.x);
				}
			}

			// knot multiplicity (ignored as knots are stored multiple times already)
			std::vector<CT_UINT32> knotMult;
			knotMult.resize(KnotSize, 1);

			ON_Interval dom = nurbs_curve.Domain();
			CADLibrary::CheckedCTError setUvCurveError = CT_COEDGE_IO::SetUVCurve(
				Coedge,          /*!< [out] Id of created coedge */
				order,           /*!< [in] Order of curve */
				KnotSize,        /*!< [in] Knot vector size */
				ctrl_hull_size,  /*!< [in] Control Hull Size */
				ctrl_hull_dim,   /*!< [in] Control Hull Dimension (2 for non-rational or 3 for rational) */
				cv_data.data(),  /*!< [in] Control hull array */
				Knots.data(),    /*!< [in] Value knot array */
				knotMult.data(), /*!< [in] Multiplicity Knot array */
				dom.m_t[0],      /*!< [in] start parameter of coedge on the uv curve (t range=[knot[0], knot[knot_size-1]]) */
				dom.m_t[1]       /*!< [in] end parameter of coedge on the uv curve (t range=[knot[0], knot[knot_size-1]]) */
			);

			if (!setUvCurveError)
				continue;

			Coedges.PushBack(Coedge);
		}

		CT_OBJECT_ID Loop;
		CADLibrary::CheckedCTError LoopCreationError = CT_LOOP_IO::Create(Loop, Coedges);
		if (!LoopCreationError)
			continue;

		Loops.PushBack(Loop);
	}

	CT_OBJECT_ID FaceID;
	CT_ORIENTATION faceOrient = CT_ORIENTATION::CT_FORWARD;
	CADLibrary::CheckedCTError result = CT_FACE_IO::Create(FaceID, SurfaceID, faceOrient, Loops);
	if (!result)
		return;
	dest.PushBack(FaceID);
}

void CreateCTFace(const ON_Brep& brep, const ON_BrepFace& Face, CT_LIST_IO& dest)
{
	const ON_BrepLoop* outerLoop = Face.OuterLoop();
	if (outerLoop == nullptr)
	{
		return;
	}

	ON_NurbsSurface Surface;
	Face.NurbsSurface(&Surface);

	ON_BoundingBox outerBBox = Face.OuterLoop()->BoundingBox();

#if FIX_HOLE_IN_WHOLE_FACE
	int LoopCount = Face.LoopCount();
	bool bBadLoopHack = brep.LoopIsSurfaceBoundary(outerLoop->m_loop_index) && LoopCount >= 2;
	if (bBadLoopHack)
	{
		CT_LIST_IO Coedges;
		int TrimCount = outerLoop->TrimCount();
		bool hasSingularTrim = false;
		for (int i = 0; i < TrimCount; ++i)
		{
			ON_BrepTrim& Trim = *outerLoop->Trim(i);
			hasSingularTrim |= (Trim.m_type == ON_BrepTrim::TYPE::singular);
		}
		bBadLoopHack &= hasSingularTrim;
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


CT_IO_ERROR FRhinoCoretechWrapper::Tessellate(FMeshDescription& Mesh, CADLibrary::FMeshParameters& MeshParameters)
{
	return CADLibrary::Tessellate(MainObjectId, ImportParams, Mesh, MeshParameters);
}

CADLibrary::CheckedCTError FRhinoCoretechWrapper::AddBRep(ON_Brep& Brep)
{
	CADLibrary::CheckedCTError Result;
	if (!IsSessionValid())
	{
		Result.RaiseOtherError("bad session init");
		return Result;
	}

	// Create ct faces
	Brep.FlipReversedSurfaces();
	CT_LIST_IO FaceList;
	int FaceCount = Brep.m_F.Count();
	for (int index = 0; index < FaceCount; index++)
	{
		const ON_BrepFace& on_face = Brep.m_F[index];
		CreateCTFace(Brep, on_face, FaceList);
	}

	if (FaceList.IsEmpty())
	{
		return Result;
	}

	// Create body from faces
	CT_OBJECT_ID BodyID;
	Result = CT_BODY_IO::CreateFromFaces(BodyID, CT_BODY_PROP::CT_BODY_PROP_EXACT | CT_BODY_PROP::CT_BODY_PROP_CLOSE, FaceList);
	if (!Result)
		return Result;

	CT_LIST_IO Bodies;
	Bodies.PushBack(BodyID);

	// Setup parenting
	Result = CT_COMPONENT_IO::AddChildren(MainObjectId, Bodies);
	return Result;
}



TSharedPtr<FRhinoCoretechWrapper> FRhinoCoretechWrapper::GetSharedSession(double SceneUnit, double ScaleFactor)
{
	TSharedPtr<FRhinoCoretechWrapper> Session = SharedSession.Pin();
	if (!Session.IsValid())
	{
		Session = MakeShared<FRhinoCoretechWrapper>(TEXT("Rh2CTSharedSession"), SceneUnit, ScaleFactor);
		SharedSession = Session;
	}
	return Session;
}

#endif
