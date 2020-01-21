// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ImplicitObject.h"
#include "Chaos/Plane.h"
#include "Chaos/Transform.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#include "Chaos/HeightField.h"
#include "Chaos/Convex.h"
#include "Chaos/Capsule.h"
#include "ImplicitObjectScaled.h"
#include "Chaos/Box.h"
#include "Chaos/Sphere.h"

#include "ChaosArchive.h"
#include <algorithm>
#include <utility>
#include "GJK.h"

namespace Chaos
{
	template <typename Lambda>
	FORCEINLINE_DEBUGGABLE auto CastHelper(const FImplicitObject& Geom, const FRigidTransform3& TM, const Lambda& Func)
	{
		const EImplicitObjectType Type = Geom.GetType(true);
		switch (Type)
		{
		case ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>(), TM);
		case ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked<TBox<FReal, 3>>(), TM);
		case ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked<TCapsule<FReal>>(), TM);
		case ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked<FConvex>(), TM);
		case ImplicitObjectType::IsScaled | ImplicitObjectType::Sphere: return Func(Geom.template GetObjectChecked<TImplicitObjectScaled<TSphere<FReal, 3>>>(), TM);
		case ImplicitObjectType::IsScaled | ImplicitObjectType::Box: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TBox<FReal, 3>>>(), TM);
		case ImplicitObjectType::IsScaled | ImplicitObjectType::Capsule: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<TCapsule<FReal>>>(), TM);
		case ImplicitObjectType::IsScaled | ImplicitObjectType::Convex: return Func(Geom.template GetObjectChecked< TImplicitObjectScaled<FConvex>>(), TM);
		case ImplicitObjectType::IsInstanced | ImplicitObjectType::Sphere:
		{
			return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TSphere<FReal,3>>>().GetInstancedObject()->template GetObjectChecked<TSphere<FReal,3>>(),TM);
		}
		case ImplicitObjectType::IsInstanced | ImplicitObjectType::Box:
		{
			return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TBox<FReal,3>>>().GetInstancedObject()->template GetObjectChecked<TBox<FReal,3>>(),TM);
		}
		case ImplicitObjectType::IsInstanced | ImplicitObjectType::Capsule:
		{
			return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<TCapsule<FReal>>>().GetInstancedObject()->template GetObjectChecked<TCapsule<FReal>>(),TM);
		}
		case ImplicitObjectType::IsInstanced | ImplicitObjectType::Convex:
		{
			return Func(Geom.template GetObjectChecked< TImplicitObjectInstanced<FConvex>>().GetInstancedObject()->template GetObjectChecked<FConvex>(),TM);
		}
		case ImplicitObjectType::Transformed:
		{
			const auto& ImplicitObjectTransformed = (Geom.template GetObjectChecked<TImplicitObjectTransformed<FReal,3>>());
			return CastHelper(*ImplicitObjectTransformed.GetTransformedObject(), ImplicitObjectTransformed.GetTransform() * TM, Func);
		}

		default: check(false);
		}
		return Func(Geom.template GetObjectChecked<TSphere<FReal, 3>>(), TM);	//needed for return type
	}

	struct FMTDInfo
	{
		FVec3 Normal;
		float Penetration;
	};

	template <typename QueryGeometry>
	bool OverlapQuery(const FImplicitObject& A, const FRigidTransform3& ATM, const QueryGeometry& B, const FRigidTransform3& BTM, const FReal Thickness = 0, FMTDInfo* OutMTD=nullptr)
	{
		const EImplicitObjectType AType = A.GetType(true);
		constexpr EImplicitObjectType BType = QueryGeometry::StaticType();
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<FReal, 3>& TransformedA = static_cast<const TImplicitObjectTransformed<FReal, 3>&>(A);
			const FRigidTransform3 NewATM = TransformedA.GetTransform() * ATM;
			return OverlapQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Thickness, OutMTD);
		}

		const FRigidTransform3 BToATM = BTM.GetRelativeTransform(ATM);

		if(BType == ImplicitObjectType::Sphere)
		{
			const FImplicitObject& BBase = static_cast<const FImplicitObject&>(B);
			const TSphere<FReal, 3>& BSphere = static_cast<const TSphere<FReal, 3>&>(BBase);
			const FVec3 PtInA = BToATM.TransformPositionNoScale(BSphere.GetCenter());
			return A.Overlap(PtInA, Thickness + BSphere.GetRadius());
		}
		//todo: A is a sphere
		else if (A.IsConvex())
		{
			const FVec3 Offset = ATM.GetLocation() - BTM.GetLocation();
			if (OutMTD)
			{
				return CastHelper(A, BToATM, [&](const auto& AConcrete, const auto& BToAFullTM)
				{
					FVec3 LocalA,LocalB,LocalNormal;
					if(GJKPenetration<FReal>(AConcrete,B,BToAFullTM,OutMTD->Penetration,LocalA,LocalB,LocalNormal,Thickness,Offset.SizeSquared() < 1e-4 ? FVec3(1,0,0) : Offset))
					{
						OutMTD->Normal = ATM.TransformVectorNoScale(LocalNormal);
						return true;
					}

					return false;
				});
			}
			else
			{
				return CastHelper(A, BToATM, [&](const auto& AConcrete, const auto& BToAFullTM) { return GJKIntersection<FReal>(AConcrete, B, BToAFullTM, Thickness, Offset.SizeSquared() < 1e-4 ? FVec3(1, 0, 0) : Offset); });
			}
		}
		else
		{
			switch (AType)
			{
			case ImplicitObjectType::HeightField:
			{
				const THeightField<FReal>& AHeightField = static_cast<const THeightField<FReal>&>(A);
				return AHeightField.OverlapGeom(B, BToATM, Thickness);
			}
			case ImplicitObjectType::TriangleMesh:
			{
				const FTriangleMeshImplicitObject& ATriangleMesh = static_cast<const FTriangleMeshImplicitObject&>(A);
				return ATriangleMesh.OverlapGeom(B, BToATM, Thickness);
			}
			default:
				if (IsScaled(AType))
				{
					const auto& AScaled = TImplicitObjectScaled<FTriangleMeshImplicitObject>::AsScaledChecked(A);
					return AScaled.LowLevelOverlapGeom(B, BToATM, Thickness);
				}
				else if(IsInstanced(AType))
				{
					const auto& AInstanced = TImplicitObjectInstanced<FTriangleMeshImplicitObject>::AsInstancedChecked(A);
					return AInstanced.LowLevelOverlapGeom(B,BToATM,Thickness);
				}
				else
				{
					check(false);	//unsupported query type
				}
			}
		}

		return false;
	}

	template <typename SweptGeometry>
	bool SweepQuery(const FImplicitObject& A, const FRigidTransform3& ATM, const SweptGeometry& B, const FRigidTransform3& BTM, const FVec3& Dir, const FReal Length, FReal& OutTime, FVec3& OutPosition, FVec3& OutNormal, int32& OutFaceIndex, const FReal Thickness, const bool bComputeMTD)
	{
		const EImplicitObjectType AType = A.GetType(true);
		constexpr EImplicitObjectType BType = SweptGeometry::StaticType();

		bool bResult = false;
		
		if (AType == ImplicitObjectType::Transformed)
		{
			const TImplicitObjectTransformed<FReal, 3>& TransformedA = static_cast<const TImplicitObjectTransformed<FReal, 3>&>(A);
			const FRigidTransform3 NewATM = TransformedA.GetTransform() * ATM;
			return SweepQuery(*TransformedA.GetTransformedObject(), NewATM, B, BTM, Dir, Length, OutTime, OutPosition, OutNormal, OutFaceIndex, Thickness, bComputeMTD);
		}

		OutFaceIndex = INDEX_NONE;
		
		FVec3 LocalPosition(-TNumericLimits<float>::Max()); // Make it obvious when things go wrong
		FVec3 LocalNormal(0);

		const FRigidTransform3 BToATM = BTM.GetRelativeTransform(ATM);
		const FVec3 LocalDir = ATM.InverseTransformVectorNoScale(Dir);

		bool bSweepAsRaycast = BType == ImplicitObjectType::Sphere && !bComputeMTD;
		if (bSweepAsRaycast && IsScaled(AType))
		{
			const auto& Scaled = TImplicitObjectScaledGeneric<FReal, 3>::AsScaledChecked(A);
			const FVec3& Scale = Scaled.GetScale();
			bSweepAsRaycast = FMath::IsNearlyEqual(Scale[0], Scale[1]) && FMath::IsNearlyEqual(Scale[0], Scale[2]);
		}

		if (bSweepAsRaycast)
		{
			const FImplicitObject& BBase = B;
			const TSphere<FReal, 3>& BSphere = BBase.GetObjectChecked<TSphere<FReal, 3>>();
			const FVec3 Start = BToATM.TransformPositionNoScale(BSphere.GetCenter());
			bResult = A.Raycast(Start, LocalDir, Length, Thickness + BSphere.GetRadius(), OutTime, LocalPosition, LocalNormal, OutFaceIndex);
		}
		//todo: handle case where A is a sphere
		else if (A.IsConvex())
		{
			auto IsValidConvex = [](const FImplicitObject& InObject) -> bool
			{
				//todo: move this out of here
				if (const auto Convex = TImplicitObjectScaled<FConvex>::AsScaled(InObject))
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

			const FVec3 Offset = ATM.GetLocation() - BTM.GetLocation();
			bResult = CastHelper(A, BToATM, [&](const auto& ADowncast, const auto& BToAFullTM){ return GJKRaycast2(ADowncast, B, BToAFullTM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, Thickness, bComputeMTD, Offset, Thickness); });
			if (AType == ImplicitObjectType::Convex)
			{
				//todo: find face index
			}
			else if (AType == ImplicitObjectType::DEPRECATED_Scaled)
			{
				ensure(false);
				//todo: find face index if convex hull
			}
		}
		else
		{
			//todo: pass bComputeMTD into these functions
			switch (AType)
			{
			case ImplicitObjectType::HeightField:
			{
				const auto& AHeightField = static_cast<const THeightField<FReal>&>(A);
				bResult = AHeightField.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness, bComputeMTD);
				break;
			}
			case ImplicitObjectType::TriangleMesh:
			{
				const auto& ATriangleMesh = static_cast<const FTriangleMeshImplicitObject&>(A);
				bResult = ATriangleMesh.SweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness, bComputeMTD);
				break;
			}
			default:
				if (IsScaled(AType))
				{
					const auto& AScaled = TImplicitObjectScaled<FTriangleMeshImplicitObject>::AsScaledChecked(A);
					bResult = AScaled.LowLevelSweepGeom(B, BToATM, LocalDir, Length, OutTime, LocalPosition, LocalNormal, OutFaceIndex, Thickness, bComputeMTD);
					break;
				}
				else if(IsInstanced(AType))
				{
					const auto& Instanced = TImplicitObjectInstanced<FTriangleMeshImplicitObject>::AsInstancedChecked(A);
					bResult = Instanced.LowLevelSweepGeom(B,BToATM,LocalDir,Length,OutTime,LocalPosition,LocalNormal,OutFaceIndex,Thickness,bComputeMTD);
					break;
				}
				else
				{
					check(false);	//unsupported query type
				}

			}
		}

		//put back into world space
		if (bResult && (OutTime > 0 || bComputeMTD))
		{
			OutNormal = ATM.TransformVectorNoScale(LocalNormal);
			OutPosition = ATM.TransformPositionNoScale(LocalPosition);
		}

		return bResult;
	}
}
