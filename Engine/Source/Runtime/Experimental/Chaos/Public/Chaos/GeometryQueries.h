// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/HeightField.h"
#include "Chaos/Convex.h"
#include "Chaos/Capsule.h"
#include "ImplicitObjectScaled.h"

#include "ChaosArchive.h"
#include <algorithm>
#include <utility>
#include "GJK.h"

namespace Chaos
{
	template <typename T, int d>
	bool OverlapQuery(const FImplicitObject& A, const TRigidTransform<T,d>& ATM, const FImplicitObject& B, const TRigidTransform<T,d>& BTM, const T Thickness = 0, TVector<T,d>* OutMTD=nullptr)
	{
		const EImplicitObjectType AType = A.GetType(true);
		const EImplicitObjectType BType = B.GetType(true);
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedA = static_cast<const TImplicitObjectTransformed<T, d>&>(A);
			const TRigidTransform<T, d> NewATM = TransformedA.GetTransform() * ATM;
			return OverlapQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Thickness, OutMTD);
		}

		if (BType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedB = static_cast<const TImplicitObjectTransformed<T, d>&>(B);
			const TRigidTransform<T, d> NewBTM = TransformedB.GetTransform() * BTM;
			return OverlapQuery(A, ATM, *TransformedB.GetTransformedObject(), NewBTM, Thickness, OutMTD);
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
			default:
				if (IsScaled(AType))
				{
					const auto& AScaled = TImplicitObjectScaled<TTriangleMeshImplicitObject<T>>::AsScaledChecked(A);
					return AScaled.LowLevelOverlapGeom(B, BToATM, Thickness);
				}
				else
				{
					check(false);	//unsupported query type
				}

			}
		}

		return false;
	}

	template <typename T, int d>
	bool SweepQuery(const FImplicitObject& A, const TRigidTransform<T,d>& ATM, const FImplicitObject& B, const TRigidTransform<T, d>& BTM, const TVector<T,d>& Dir, const T Length, T& OutTime, TVector<T,d>& OutPosition, TVector<T,d>& OutNormal, int32& OutFaceIndex, const T Thickness, const bool bComputeMTD)
	{
		const EImplicitObjectType AType = A.GetType(true);
		const EImplicitObjectType BType = B.GetType(true);

		bool bResult = false;
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedA = static_cast<const TImplicitObjectTransformed<T, d>&>(A);
			const TRigidTransform<T, d> NewATM = TransformedA.GetTransform() * ATM;
			return SweepQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
		}

		if (BType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<T, d>& TransformedB = static_cast<const TImplicitObjectTransformed<T, d>&>(B);
			const TRigidTransform<T, d> NewBTM = TransformedB.GetTransform() * BTM;
			return SweepQuery(A, ATM, *TransformedB.GetTransformedObject(), NewBTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
		}

		check(B.IsConvex());	//Object being swept must be convex
		OutFaceIndex = INDEX_NONE;
		
		TVector<T, d> LocalPosition(-TNumericLimits<float>::Max()); // Make it obvious when things go wrong
		TVector<T, d> LocalNormal(0);

		const TRigidTransform<T, d> BToATM = BTM.GetRelativeTransform(ATM);
		const TVector<T, d> LocalDir = ATM.InverseTransformVectorNoScale(Dir);

		bool bSweepAsRaycast = BType == ImplicitObjectType::Sphere;
		if (bSweepAsRaycast && IsScaled(AType))
		{
			const auto& Scaled = TImplicitObjectScaledGeneric<T, d>::AsScaledChecked(A);
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
			auto IsValidConvex = [](const FImplicitObject& InObject) -> bool
			{
				//todo: move this out of here
				if (const auto Convex = TImplicitObjectScaled<TConvex<T,3>>::AsScaled(InObject))
				{
					return Convex->GetUnscaledObject()->GetSurfaceParticles().Size() > 0;
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

			bResult = GJKRaycast2<T>(A, B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness + A.GetMargin(), Offset, Thickness + B.GetMargin());
			//bResult = GJKRaycast<T>(A, B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness, Offset, Thickness);

			if (AType == ImplicitObjectType::Convex)
			{
				//todo: find face index
			}
			else if (AType == ImplicitObjectType::DEPRECATED_Scaled)
			{
				ensure(false);
				//todo: find face index if convex hull
			}

			// Compute MTD in the case of an initial overlap
			if (bResult && bComputeMTD && OutTime == 0.f)
			{
				TVector<T, d> LocalPositionOnB;
				if (BType == ImplicitObjectType::Capsule)
				{
					const TCapsule<T>& BCapsule = static_cast<const TCapsule<T>&>(B);
					GJKPenetrationTemp<T>(A, BCapsule, BToATM, LocalPosition, LocalPositionOnB, LocalNormal, OutTime, Thickness, Offset);
				}
				else
				{
					GJKPenetrationTemp<T>(A, B, BToATM, LocalPosition, LocalPositionOnB, LocalNormal, OutTime, Thickness, Offset);
				}
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
			default:
				if (IsScaled(AType))
				{
					const auto& AScaled = TImplicitObjectScaled<TTriangleMeshImplicitObject<T>>::AsScaledChecked(A);
					bResult = AScaled.LowLevelSweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness);
					break;
				}
				else
				{
					check(false);	//unsupported query type
				}

			}

			// Compute MTD in the case of an initial overlap
			if (bResult && bComputeMTD && OutTime == 0.f)
			{
				ensure(false); // We don't support MTD for non-convex types yet!
				OutNormal = TVector<float, 3>(0.f, 0.f, 1.f);
			}
		}

		//put back into world space
		if (OutTime > 0 || bComputeMTD)
		{
			OutNormal = ATM.TransformVectorNoScale(LocalNormal);
			OutPosition = ATM.TransformPositionNoScale(LocalPosition);
		}

		return bResult;
	}
}
