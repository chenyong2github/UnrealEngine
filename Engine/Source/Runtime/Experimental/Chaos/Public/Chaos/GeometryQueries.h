// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/HeightField.h"
#include "Chaos/Convex.h"
#include "ImplicitObjectScaled.h"

#include "ChaosArchive.h"
#include <algorithm>
#include <utility>
#include "GJK.h"

namespace Chaos
{
	template <typename T, int d>
	bool OverlapQuery(const TImplicitObject<T, d>& A, const TRigidTransform<T,d>& ATM, const TImplicitObject<T, d>& B, const TRigidTransform<T,d>& BTM, const T Thickness = 0)
	{
		const ImplicitObjectType AType = A.GetType(true);
		const ImplicitObjectType BType = B.GetType(true);
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedA = static_cast<const TImplicitObjectTransformed<T, d>&>(A);
			const TRigidTransform<T, d> NewATM = TransformedA.GetTransform() * ATM;
			return OverlapQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Thickness);
		}

		if (BType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedB = static_cast<const TImplicitObjectTransformed<T, d>&>(B);
			const TRigidTransform<T, d> NewBTM = TransformedB.GetTransform() * BTM;
			return OverlapQuery(A, ATM, *TransformedB.GetTransformedObject(), NewBTM, Thickness);
		}

		check(B.IsConvex());	//Query object must be convex
		const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);

		if(BType == ImplicitObjectType::Sphere)
		{
			const TSphere<T, d>& BSphere = static_cast<const TSphere<T, d>&>(B);
			const TVector<T, d> PtInA = BToATM.TransformPositionNoScale(BSphere.GetCenter());
			return A.Overlap(PtInA, Thickness + BSphere.GetRadius());
		}
		//todo: A is a sphere
		else if (A.IsConvex())
		{
			const TVector<T, d> Offset = ATM.GetLocation() - BTM.GetLocation();
			return GJKIntersection(A, B, BToATM, Thickness, Offset.SizeSquared() < 1e-4 ? TVector<T, d>(1, 0, 0) : Offset);
		}
		else
		{
			switch (AType)
			{
			case ImplicitObjectType::HeightField:
			{
				const THeightField<T>& AHeightField = static_cast<const THeightField<T>&>(A);
				return AHeightField.OverlapGeom(B, BToATM, Thickness);
			}
			case ImplicitObjectType::TriangleMesh:
			{
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(A);
				return ATriangleMesh.OverlapGeom(B, BToATM, Thickness);
			}
			case ImplicitObjectType::Scaled:
			{
				const TImplicitObjectScaled<T, d>& AScaled = static_cast<const TImplicitObjectScaled<T, d>&>(A);
				const TImplicitObject<T, d>& UnscaledObj = *AScaled.GetUnscaledObject();
				check(UnscaledObj.GetType(true) == ImplicitObjectType::TriangleMesh);
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(UnscaledObj);
				return TImplicitObjectScaled<T,d>::LowLevelOverlapGeom(AScaled, ATriangleMesh, B, BToATM, Thickness);
			}
			default:
				check(false);	//unsupported query type

			}
		}

		return false;
	}

	template <typename T, int d>
	bool SweepQuery(const TImplicitObject<T, d>& A, const TRigidTransform<T,d>& ATM, const TImplicitObject<T, d>& B, const TRigidTransform<T, d>& BTM, const TVector<T,d>& Dir, const T Length, T& OutTime, TVector<T,d>& OutPosition, TVector<T,d>& OutNormal, int32& OutFaceIndex, const T Thickness = 0)
	{
		const ImplicitObjectType AType = A.GetType(true);
		const ImplicitObjectType BType = B.GetType(true);

		bool bResult = false;
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedA = static_cast<const TImplicitObjectTransformed<T, d>&>(A);
			const TRigidTransform<T, d> NewATM = TransformedA.GetTransform() * ATM;
			return SweepQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness);
		}

		if (BType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedB = static_cast<const TImplicitObjectTransformed<T, d>&>(B);
			const TRigidTransform<T, d> NewBTM = TransformedB.GetTransform() * BTM;
			return SweepQuery(A, ATM, *TransformedB.GetTransformedObject(), NewBTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness);
		}

		check(B.IsConvex());	//Object being swept must be convex
		OutFaceIndex = INDEX_NONE;
		
		TVector<T, d> LocalPosition(-TNumericLimits<float>::Max()); // Make it obvious when things go wrong
		TVector<T, d> LocalNormal(0);

		const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);
		const TVector<T, d> LocalDir = ATM.InverseTransformVectorNoScale(Dir);

		bool bSweepAsRaycast = BType == ImplicitObjectType::Sphere;
		if (bSweepAsRaycast && AType == ImplicitObjectType::Scaled)
		{
			const auto& Scaled = A. template GetObjectChecked<TImplicitObjectScaled<T, d>>();
			const TVector<T, d>& Scale = Scaled.GetScale();
			bSweepAsRaycast = FMath::IsNearlyEqual(Scale[0], Scale[1]) && FMath::IsNearlyEqual(Scale[0], Scale[2]);
		}

		if (bSweepAsRaycast)
		{
			const TSphere<T, d>& BSphere = static_cast<const TSphere<T, d>&>(B);
			const TVector<T, d> Start = BToATM.TransformPositionNoScale(BSphere.GetCenter());
			bResult = A.Raycast(Start, LocalDir, Length, Thickness + BSphere.GetRadius(), OutTime, LocalPosition, LocalNormal, OutFaceIndex);
		}
		//todo: handle case where A is a sphere
		else if (A.IsConvex())
		{
			auto IsValidConvex = [](const TImplicitObject<T, 3>& InObject) -> bool
			{
				//todo: move this out of here
				const TImplicitObject<T, 3>* Obj = &InObject;
				if (const TImplicitObjectScaled<T, 3>* Scaled = InObject.template GetObject<TImplicitObjectScaled<T, 3>>())
				{
					Obj = Scaled->GetUnscaledObject();
				}
				if (const TConvex<T, 3>* Convex = Obj->template GetObject<TConvex<T, 3>>())
				{
					return Convex->GetSurfaceParticles().Size() > 0;
				}

				return true;
			};

			// Validate that the convexes we are about to test are actually valid geometries
			if(!ensureMsgf(IsValidConvex(A), TEXT("GJKRaycast - Convex A has no particles")) ||
				!ensureMsgf(IsValidConvex(B), TEXT("GJKRaycast - Convex B has no particles")))
			{
				return false;
			}

			const TVector<T, d> Offset = ATM.GetLocation() - BTM.GetLocation();

			bResult = GJKRaycast<T>(A, B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness, Offset);

			if (AType == ImplicitObjectType::Convex)
			{
				//todo: find face index
			}
			else if (AType == ImplicitObjectType::Scaled)
			{
				//todo: find face index if convex hull
			}
		}
		else
		{
			switch (AType)
			{
			case ImplicitObjectType::HeightField:
			{
				const THeightField<T>& AHeightField = static_cast<const THeightField<T>&>(A);
				bResult = AHeightField.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness);
				break;
			}
			case ImplicitObjectType::TriangleMesh:
			{
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(A);
				bResult = ATriangleMesh.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness);
				break;
			}
			case ImplicitObjectType::Scaled:
			{
				const TImplicitObjectScaled<T, d>& AScaled = static_cast<const TImplicitObjectScaled<T, d>&>(A);
				const TImplicitObject<T, d>& UnscaledObj = *AScaled.GetUnscaledObject();
				check(UnscaledObj.GetType(true) == ImplicitObjectType::TriangleMesh);
				const TTriangleMeshImplicitObject<T>& ATriangleMesh = static_cast<const TTriangleMeshImplicitObject<T>&>(UnscaledObj);
				bResult = TImplicitObjectScaled<T,d>::LowLevelSweepGeom(AScaled, ATriangleMesh, B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness);
				break;
			}
			default:
				check(false);	//unsupported query type

			}
		}

		//put back into world space
		if (OutTime > 0)
		{
			OutNormal = ATM.TransformVectorNoScale(LocalNormal);
			OutPosition = ATM.TransformPositionNoScale(LocalPosition);
		}

		return bResult;
	}
}
