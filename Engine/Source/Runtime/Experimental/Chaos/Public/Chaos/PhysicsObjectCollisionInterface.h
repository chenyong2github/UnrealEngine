// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/Interface/SQTypes.h"
#include "Chaos/GeometryQueries.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/PhysicsObjectInterface.h"
#include "Framework/Threading.h"

namespace Chaos
{
	struct CHAOS_API FSweepParameters
	{
		bool bSweepComplex = false;
		bool bComputeMTD = false;
	};

	template<EThreadContext Id>
	class CHAOS_API FPhysicsObjectCollisionInterface
	{
	public:
		explicit FPhysicsObjectCollisionInterface(FReadPhysicsObjectInterface<Id>& InInterface) :
			Interface(InInterface)
		{}

		// This function will not compute any overlap heuristic.
		bool PhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex);

		// Returns all the overlaps within A given a shape B.
		template<typename TOverlapHit>
		bool PhysicsObjectOverlap(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, TArray<TOverlapHit>& OutOverlaps)
		{
			static_assert(std::is_same_v<TOverlapHit, ChaosInterface::TThreadOverlapHit<Id>>);
			return PairwiseShapeOverlapHelper(
				ObjectA,
				InTransformA,
				ObjectB,
				InTransformB,
				bTraceComplex,
				false,
				FVector::Zero(),
				[this, ObjectA, &OutOverlaps](const FShapeOverlapData& A, const FShapeOverlapData& B, const FMTDInfo&)
				{
					ChaosInterface::FOverlapHit Overlap;
					Overlap.Shape = A.Shape;
					Overlap.Actor = Interface.GetParticle(ObjectA);
					OutOverlaps.Add(Overlap);
					return false;
				}
			);
		}

		// This function does the same as GetPhysicsObjectOverlap but also computes the MTD metric.
		bool PhysicsObjectOverlapWithMTD(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, FMTDInfo& OutMTD);

		// This function does the same as GetPhysicsObjectOverlap but also computes the AABB overlap metric.
		bool PhysicsObjectOverlapWithAABB(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FBox& OutOverlap);
		bool PhysicsObjectOverlapWithAABBSize(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, const FVector& Tolerance, FVector& OutOverlapSize);

		template<typename TRaycastHit>
		bool LineTrace(TArrayView<const FConstPhysicsObjectHandle> InObjects, const FVector& WorldStart, const FVector& WorldEnd, bool bTraceComplex, TRaycastHit& OutBestHit)
		{
			static_assert(std::is_same_v<TRaycastHit, ChaosInterface::TThreadRaycastHit<Id>>);
			bool bHit = false;
			OutBestHit.Distance = TNumericLimits<float>::Max();

			const FVector Delta = WorldEnd - WorldStart;
			const FReal DeltaMag = Delta.Size();
			if (DeltaMag < UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}

			FTransform BestWorldTM = FTransform::Identity;

			for (const FConstPhysicsObjectHandle Object : InObjects)
			{
				const FTransform WorldTM = Interface.GetTransform(Object);
				const FVector LocalStart = WorldTM.InverseTransformPositionNoScale(WorldStart);
				const FVector LocalDelta = WorldTM.InverseTransformVectorNoScale(Delta);

				Interface.VisitEveryShape(
					{ &Object, 1 },
					[this, &bHit, &WorldTM, &LocalStart, &LocalDelta, &Delta, DeltaMag, &BestWorldTM, bTraceComplex, &OutBestHit](const FConstPhysicsObjectHandle IterObject, TThreadShapeInstance<Id>* Shape)
					{
						check(Shape);

						FCollisionFilterData ShapeFilter = Shape->GetQueryData();
						const bool bShapeIsComplex = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::ComplexCollision)) != 0;
						const bool bShapeIsSimple = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::SimpleCollision)) != 0;
						if ((bTraceComplex && bShapeIsComplex) || (!bTraceComplex && bShapeIsSimple))
						{
							FReal Distance;
							FVec3 LocalPosition;
							FVec3 LocalNormal;
							int32 FaceIndex;

							const bool bRaycastHit = Shape->GetGeometry()->Raycast(
								LocalStart,
								LocalDelta / DeltaMag,
								DeltaMag,
								0,
								Distance,
								LocalPosition,
								LocalNormal,
								FaceIndex
							);

							if (bRaycastHit)
							{
								if (Distance < OutBestHit.Distance)
								{
									bHit = true;
									BestWorldTM = WorldTM;
									OutBestHit.Distance = static_cast<float>(Distance);
									OutBestHit.WorldNormal = LocalNormal;
									OutBestHit.WorldPosition = LocalPosition;
									OutBestHit.Shape = Shape;
									OutBestHit.Actor = Interface.GetParticle(IterObject);
									OutBestHit.FaceIndex = FaceIndex;
								}
							}
						}
						return false;
					}
				);
			}

			if (bHit)
			{
				OutBestHit.WorldNormal = BestWorldTM.TransformVectorNoScale(OutBestHit.WorldNormal);
				OutBestHit.WorldPosition = BestWorldTM.TransformPositionNoScale(OutBestHit.WorldPosition);
			}

			return bHit;
		}

		template<typename TOverlapHit>
		bool ShapeOverlap(TArrayView<const FConstPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& GeomTransform, TArray<TOverlapHit>& OutOverlaps)
		{
			static_assert(std::is_same_v<TOverlapHit, ChaosInterface::TThreadOverlapHit<Id>>);
			bool bHasOverlap = false;
			for (const FConstPhysicsObjectHandle Object : InObjects)
			{
				const FTransform WorldTM = Interface.GetTransform(Object);

				Interface.VisitEveryShape(
					{ &Object, 1 },
					[this, &bHasOverlap, &WorldTM, &InGeom, &GeomTransform, &OutOverlaps](const FConstPhysicsObjectHandle IterObject, TThreadShapeInstance<Id>* Shape)
					{
						check(Shape);
						const bool bOverlap = Chaos::Utilities::CastHelper(
							InGeom,
							GeomTransform,
							[Shape, &WorldTM](const auto& Downcast, const auto& FullTransformB)
							{
								return Chaos::OverlapQuery(*Shape->GetGeometry(), WorldTM, Downcast, FullTransformB, 0, nullptr);
							}
						);

						if (bOverlap)
						{
							bHasOverlap = true;

							ChaosInterface::FOverlapHit Overlap;
							Overlap.Shape = Shape;
							Overlap.Actor = Interface.GetParticle(IterObject);
							OutOverlaps.Add(Overlap);
						}
						return false;
					}
				);
			}
			return bHasOverlap;
		}

		template<typename TSweepHit>
		bool ShapeSweep(TArrayView<const FConstPhysicsObjectHandle> InObjects, const Chaos::FImplicitObject& InGeom, const FTransform& StartTM, const FVector& EndPos, const FSweepParameters& Params, TSweepHit& OutBestHit)
		{
			static_assert(std::is_same_v<TSweepHit, ChaosInterface::TThreadSweepHit<Id>>);
			bool bHit = false;
			const FVector StartPos = StartTM.GetTranslation();
			const FVector Delta = EndPos - StartPos;
			const FReal DeltaMag = Delta.Size();
			if (DeltaMag < UE_KINDA_SMALL_NUMBER)
			{
				return false;
			}
			const FVec3 Dir = Delta / DeltaMag;

			for (const FConstPhysicsObjectHandle Object : InObjects)
			{
				const FTransform WorldTM = Interface.GetTransform(Object);

				Interface.VisitEveryShape(
					{ &Object, 1 },
					[this, &WorldTM, &InGeom, &StartTM, &bHit, &Delta, DeltaMag, &Dir, &OutBestHit, &Params](const FConstPhysicsObjectHandle IterObject, TThreadShapeInstance<Id>* Shape)
					{
						check(Shape);

						FCollisionFilterData ShapeFilter = Shape->GetQueryData();
						const bool bShapeIsComplex = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::ComplexCollision)) != 0;
						const bool bShapeIsSimple = (ShapeFilter.Word3 & static_cast<uint8>(EFilterFlags::SimpleCollision)) != 0;
						if ((Params.bSweepComplex && bShapeIsComplex) || (!Params.bSweepComplex && bShapeIsSimple))
						{
							FVec3 WorldPosition;
							FVec3 WorldNormal;
							FReal Distance;
							int32 FaceIdx;
							FVec3 FaceNormal;

							const bool bShapeHit = Chaos::Utilities::CastHelper(
								InGeom,
								StartTM,
								[Shape, &WorldTM, &Dir, DeltaMag, &Distance, &WorldPosition, &WorldNormal, &FaceIdx, &FaceNormal, &Params](const auto& Downcast, const auto& FullTransformB)
								{
									// Set bComputeMTD to true to better match internal SQ Visitor behavior.
									return Chaos::SweepQuery(*Shape->GetGeometry(), WorldTM, Downcast, FullTransformB, Dir, DeltaMag, Distance, WorldPosition, WorldNormal, FaceIdx, FaceNormal, 0.f, Params.bComputeMTD);
								}
							);

							if (bShapeHit)
							{
								bHit = true;

								OutBestHit.Shape = Shape;
								OutBestHit.WorldPosition = WorldPosition;
								OutBestHit.WorldNormal = WorldNormal;
								OutBestHit.Distance = static_cast<float>(Distance);
								OutBestHit.FaceIndex = FaceIdx;
								if (OutBestHit.Distance > 0.f)
								{
									const FVector LocalPosition = WorldTM.InverseTransformPositionNoScale(OutBestHit.WorldPosition);
									const FVector LocalUnitDir = WorldTM.InverseTransformVectorNoScale(Dir);
									OutBestHit.FaceIndex = Shape->GetGeometry()->FindMostOpposingFace(LocalPosition, LocalUnitDir, OutBestHit.FaceIndex, 1);
								}
								OutBestHit.Actor = Interface.GetParticle(IterObject);
							}
						}
						return false;
					}
				);

			}
			return bHit;
		}


	private:
		FReadPhysicsObjectInterface<Id>& Interface;

		struct FShapeOverlapData
		{
			TThreadShapeInstance<Id>* Shape;
			FAABB3 BoundingBox;
		};

		/**
		 * For every pair of shapes that overlap, allows the caller to perform some computation. If additional pairs of shapes need to be examined, the input TFunction should return true.
		 */
		bool PairwiseShapeOverlapHelper(const FConstPhysicsObjectHandle ObjectA, const FTransform& InTransformA, const FConstPhysicsObjectHandle ObjectB, const FTransform& InTransformB, bool bTraceComplex, bool bComputeMTD, const FVector& Tolerance, const TFunction<bool(const FShapeOverlapData&, const FShapeOverlapData&, const FMTDInfo&)>& Lambda);
	};

	using FPhysicsObjectCollisionInterface_External = FPhysicsObjectCollisionInterface<EThreadContext::External>;
	using FPhysicsObjectCollisionInterface_Internal = FPhysicsObjectCollisionInterface<EThreadContext::Internal>;
}