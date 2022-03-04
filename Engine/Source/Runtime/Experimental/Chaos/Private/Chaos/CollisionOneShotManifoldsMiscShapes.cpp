// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CollisionOneShotManifoldsMiscShapes.h"

#include "Chaos/CollisionOneShotManifolds.h"
#include "Chaos/CollisionResolution.h"
#include "Chaos/Collision/CapsuleConvexContactPoint.h"
#include "Chaos/Collision/ContactPointsMiscShapes.h"
#include "Chaos/Collision/SphereConvexContactPoint.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Convex.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/UncheckedArray.h"
#include "Chaos/GJK.h"
#include "Chaos/ImplicitObjectScaled.h"
#include "Chaos/Transform.h"
#include "Chaos/Triangle.h"
#include "Chaos/Utilities.h"
#include "ChaosStats.h"

#include "HAL/IConsoleManager.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	extern FRealSingle Chaos_Collision_EdgePrunePlaneDistance;
	extern FRealSingle Chaos_Collision_Manifold_SphereCapsuleSizeThreshold;
	extern FRealSingle Chaos_Collision_Manifold_CapsuleAxisAlignedThreshold;
	extern FRealSingle Chaos_Collision_Manifold_CapsuleDeepPenetrationFraction;
	extern FRealSingle Chaos_Collision_Manifold_CapsuleRadialContactFraction;

	extern bool bChaos_Collision_UseGJK2;
	extern FRealSingle Chaos_Collision_GJKEpsilon;
	extern FRealSingle Chaos_Collision_EPAEpsilon;

	namespace Collisions
	{
		void ConstructSphereSphereOneShotManifold(
			const TSphere<FReal, 3>& SphereA,
			const FRigidTransform3& SphereATransform, //world
			const TSphere<FReal, 3>& SphereB,
			const FRigidTransform3& SphereBTransform, //world
			const FReal Dt,
			FPBDCollisionConstraint& Constraint)
		{
			SCOPE_CYCLE_COUNTER_MANIFOLD();
			
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereATransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(SphereBTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			FContactPoint ContactPoint = SphereSphereContactPoint(SphereA, SphereATransform, SphereB, SphereBTransform, Constraint.GetCullDistance(), Constraint.GetRestitutionPadding());
			if (ContactPoint.Phi < Constraint.GetCullDistance())
			{
				Constraint.AddOneshotManifoldContact(ContactPoint);
			}
		}

		void ConstructSpherePlaneOneShotManifold(
			const TSphere<FReal, 3>& Sphere, 
			const FRigidTransform3& SphereTransform, 
			const TPlane<FReal, 3>& Plane, 
			const FRigidTransform3& PlaneTransform, 
			const FReal Dt, 
			FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(PlaneTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			FContactPoint ContactPoint = SpherePlaneContactPoint(Sphere, SphereTransform, Plane, PlaneTransform, Constraint.GetRestitutionPadding());
			if (ContactPoint.Phi < Constraint.GetCullDistance())
			{
				Constraint.AddOneshotManifoldContact(ContactPoint);
			}
		}	

		void ConstructSphereBoxOneShotManifold(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FImplicitBox3& Box, const FRigidTransform3& BoxTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(BoxTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			FContactPoint ContactPoint = SphereBoxContactPoint(Sphere, SphereTransform, Box, BoxTransform, Constraint.GetRestitutionPadding());
			if (ContactPoint.Phi < Constraint.GetCullDistance())
			{
				Constraint.AddOneshotManifoldContact(ContactPoint);
			}
		}

		// Build a sphere-Capsule manifold.
		// When the sphere and capsule are of similar size, we usually only need a 1-point manifold.
		// If the sphere is larger than the capsule, we need to generate a multi-point manifold so that
		// we don't end up jittering between collisions on each end cap. E.g., consider a small capsule
		// lying horizontally on a very large sphere (almost flat) - we need at least 2 contact points to 
		// make this stable.
		void ConstructSphereCapsuleOneShotManifold(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FCapsule& Capsule, const FRigidTransform3& CapsuleTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(CapsuleTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			Constraint.ResetActiveManifoldContacts();

			// Build a multi-point manifold
			const FReal NetCullDistance = Sphere.GetRadius() + Capsule.GetRadius() + Constraint.GetCullDistance() + Constraint.GetRestitutionPadding();
			const FReal NetCullDistanceSq = FMath::Square(NetCullDistance);

			// Transform the sphere into capsule space and find the closest point on the capsule line segment
			// @todo(chaos) this would be much simpler if the sphere's were always at the origin and capsules were at the origin and axis aligned
			const FRigidTransform3 SphereToCapsuleTransform = SphereTransform.GetRelativeTransformNoScale(CapsuleTransform);
			const FVec3 SpherePos = SphereToCapsuleTransform.TransformPositionNoScale(Sphere.GetCenter());
			const FReal NearPosT = Utilities::ClosestTimeOnLineSegment(SpherePos, Capsule.GetX1(), Capsule.GetX2());

			// Add the closest contact point to the manifold
			const FVec3 NearPos = FMath::Lerp(Capsule.GetX1(), Capsule.GetX2(), NearPosT);
			const FVec3 NearPosDelta = SpherePos - NearPos;
			const FReal NearPosDistanceSq = NearPosDelta.SizeSquared();
			if (NearPosDistanceSq > SMALL_NUMBER)
			{
				if (NearPosDistanceSq < NetCullDistanceSq)
				{
					const FReal NearPosDistance = FMath::Sqrt(NearPosDistanceSq);
					const FVec3 NearPosDir = NearPosDelta / NearPosDistance;
					const FReal NearPhi = NearPosDistance - Sphere.GetRadius() - Capsule.GetRadius() - Constraint.GetRestitutionPadding();

					FContactPoint NearContactPoint;
					NearContactPoint.ShapeContactPoints[0] = SphereToCapsuleTransform.InverseTransformPositionNoScale(SpherePos - Sphere.GetRadius() * NearPosDir);
					NearContactPoint.ShapeContactPoints[1] = NearPos + Capsule.GetRadius() * NearPosDir;
					NearContactPoint.ShapeContactNormal = NearPosDir;
					NearContactPoint.Phi = NearPhi;
					NearContactPoint.FaceIndex = INDEX_NONE;
					NearContactPoint.ContactType = EContactPointType::VertexPlane;
					Constraint.AddOneshotManifoldContact(NearContactPoint);

					// If we have a small sphere, just stick with the 1-point manifold
					const FReal SphereCapsuleSizeThreshold = FReal(Chaos_Collision_Manifold_SphereCapsuleSizeThreshold);
					if (Sphere.GetRadius() < SphereCapsuleSizeThreshold * (Capsule.GetHeight() + Capsule.GetRadius()))
					{
						return;
					}

					// If the capsule is non-dynamic there's no point in creating the multipoint manifold
					if (!FConstGenericParticleHandle(Constraint.GetParticle1())->IsDynamic())
					{
						return;
					}

					// If the contact is deep, there's a high chance that pushing one end out will push the other deeper and we also need more contacts.
					// Note: we only consider the radius of the dynamic object(s) when deciding what "deep" means because the extra contacts are only
					// to prevent excessive rotation from the single contact we have so far, and only the dynamic objects will rotate.
					const FReal DeepRadiusFraction = Chaos_Collision_Manifold_CapsuleDeepPenetrationFraction;
					const bool bIsDeep = NearPhi < -DeepRadiusFraction * Capsule.GetRadius();
					if (!bIsDeep)
					{
						return;
					}

					// Now add the two end caps
					// Calculate the vector orthogonal to the capsule axis that gives the nearest points on the capsule cyclinder to the sphere
					// The initial length will be proportional to the sine of the angle between the axis and the delta position and will approach
					// zero when the capsule is end-on to the sphere, in which case we won't add the end caps.
					constexpr FReal EndCapSinAngleThreshold = FReal(0.35);	// about 20deg
					constexpr FReal EndCapDistanceThreshold = FReal(0.2);	// fraction
					FVec3 CapsuleOrthogonal = FVec3::CrossProduct(Capsule.GetAxis(), FVec3::CrossProduct(Capsule.GetAxis(), NearPosDir));
					const FReal CapsuleOrthogonalLenSq = CapsuleOrthogonal.SizeSquared();
					if (CapsuleOrthogonalLenSq > FMath::Square(EndCapSinAngleThreshold))
					{
						// Orthogonal must point towards the sphere, but currently depends on the relative axis orientation
						CapsuleOrthogonal = CapsuleOrthogonal * FMath::InvSqrt(CapsuleOrthogonalLenSq);
						if (FVec3::DotProduct(CapsuleOrthogonal, SpherePos - Capsule.GetCenter()) < FReal(0))
						{
							CapsuleOrthogonal = -CapsuleOrthogonal;
						}

						if (NearPosT > EndCapDistanceThreshold)
						{
							const FVec3 EndCapPos0 = Capsule.GetX1() + CapsuleOrthogonal * Capsule.GetRadius();
							const FReal EndCapDistance0 = (SpherePos - EndCapPos0).Size();
							const FReal EndCapPhi0 = EndCapDistance0 - Sphere.GetRadius() - Constraint.GetRestitutionPadding();
							
							if (EndCapPhi0 < Constraint.GetCullDistance())
							{
								const FVec3 EndCapPosDir0 = (SpherePos - EndCapPos0) / EndCapDistance0;
								const FVec3 SpherePos0 = SpherePos - EndCapPosDir0 * Sphere.GetRadius();
						
								FContactPoint EndCapContactPoint0;
								EndCapContactPoint0.ShapeContactPoints[0] = SphereToCapsuleTransform.InverseTransformPositionNoScale(SpherePos0);
								EndCapContactPoint0.ShapeContactPoints[1] = EndCapPos0;
								EndCapContactPoint0.ShapeContactNormal = EndCapPosDir0;
								EndCapContactPoint0.Phi = EndCapPhi0;
								EndCapContactPoint0.FaceIndex = INDEX_NONE;
								EndCapContactPoint0.ContactType = EContactPointType::VertexPlane;
								Constraint.AddOneshotManifoldContact(EndCapContactPoint0);
							}
						}

						if (NearPosT < FReal(1) - EndCapDistanceThreshold)
						{
							const FVec3 EndCapPos1 = Capsule.GetX2() + CapsuleOrthogonal * Capsule.GetRadius();
							const FReal EndCapDistance1 = (SpherePos - EndCapPos1).Size();
							const FReal EndCapPhi1 = EndCapDistance1 - Sphere.GetRadius() - Constraint.GetRestitutionPadding();

							if (EndCapPhi1 < Constraint.GetCullDistance())
							{
								const FVec3 EndCapPosDir1 = (SpherePos - EndCapPos1) / EndCapDistance1;
								const FVec3 SpherePos1 = SpherePos - EndCapPosDir1 * Sphere.GetRadius();

								FContactPoint EndCapContactPoint0;
								EndCapContactPoint0.ShapeContactPoints[0] = SphereToCapsuleTransform.InverseTransformPositionNoScale(SpherePos1);
								EndCapContactPoint0.ShapeContactPoints[1] = EndCapPos1;
								EndCapContactPoint0.ShapeContactNormal = EndCapPosDir1;
								EndCapContactPoint0.Phi = EndCapPhi1;
								EndCapContactPoint0.FaceIndex = INDEX_NONE;
								EndCapContactPoint0.ContactType = EContactPointType::VertexPlane;
								Constraint.AddOneshotManifoldContact(EndCapContactPoint0);
							}
						}
					}
				}
			}
		}

		template<typename ConvexType>
		void ConstructSphereConvexManifoldImpl(const FImplicitSphere3& Sphere, const ConvexType& Convex, const FRigidTransform3& SphereToConvexTransform, const FReal CullDistance, TCArray<FContactPoint, 4>& ContactPoints)
		{
			// Transform the sphere into convex space
			const FVec3 SpherePos = SphereToConvexTransform.TransformPositionNoScale(Sphere.GetCenter());
			const FReal SphereRadius = Sphere.GetRadius();

			// No margins for the convex, but treat the sphere as a point with a margin
			FGJKSphereShape GJKSphere(SpherePos, SphereRadius);
			TGJKShape<ConvexType> GJKConvex(Convex);

			// GJK and EPA tolerances. See comments in GJKContactPointMargin
			const FReal GJKEpsilon = Chaos_Collision_GJKEpsilon;
			const FReal EPAEpsilon = Chaos_Collision_EPAEpsilon;
			FReal ClosestPenetration;
			int32 ClosestVertexIndexSphere = INDEX_NONE, ClosestVertexIndexConvex = INDEX_NONE;
			FReal ClosestSupportMaxDelta = FReal(0);

			// Primary contact
			// NOTE: swapped contact point order to match desired output order (Sphere, Convex)
			FContactPoint ClosestContactPoint;
			if (bChaos_Collision_UseGJK2)
			{
				GJKPenetrationSameSpace2(
					GJKConvex, 
					GJKSphere, 
					ClosestPenetration,
					ClosestContactPoint.ShapeContactPoints[1], 
					ClosestContactPoint.ShapeContactPoints[0], 
					ClosestContactPoint.ShapeContactNormal, 
					ClosestVertexIndexConvex,
					ClosestVertexIndexSphere,
					ClosestSupportMaxDelta,
					GJKEpsilon, 
					EPAEpsilon);
			}
			else
			{
				GJKPenetrationSameSpace(
					GJKConvex,
					GJKSphere,
					ClosestPenetration,
					ClosestContactPoint.ShapeContactPoints[1],
					ClosestContactPoint.ShapeContactPoints[0],
					ClosestContactPoint.ShapeContactNormal,
					ClosestVertexIndexConvex,
					ClosestVertexIndexSphere,
					ClosestSupportMaxDelta,
					GJKEpsilon,
					EPAEpsilon);
			}

			// Stop now if beyond cull distance
			const FReal ClosestPhi = -ClosestPenetration;
			if (ClosestPhi > CullDistance)
			{
				return;
			}

			// We always use the primary contact so add it to the output now
			ClosestContactPoint.ShapeContactPoints[0] = SphereToConvexTransform.InverseTransformPositionNoScale(ClosestContactPoint.ShapeContactPoints[0]);
			ClosestContactPoint.Phi = ClosestPhi;
			ClosestContactPoint.FaceIndex = INDEX_NONE;
			ClosestContactPoint.ContactType = EContactPointType::Unknown;
			ContactPoints.Add(ClosestContactPoint);

			// If the sphere is "large" compared to the convex add more points
			const FReal SpheerConvexManifoldSizeThreshold = FReal(1);
			const FReal ConvexSize = Convex.BoundingBox().Extents().GetAbsMax();
			if (SphereRadius > SpheerConvexManifoldSizeThreshold * ConvexSize)
			{
				// Find the convex plane to use - the one most opposing the primary contact normal
				const int32 ConvexPlaneIndex = Convex.GetMostOpposingPlane(-ClosestContactPoint.ShapeContactNormal);
				if (ConvexPlaneIndex != INDEX_NONE)
				{
					FVec3 ConvexPlanePosition, ConvexPlaneNormal;
					Convex.GetPlaneNX(ConvexPlaneIndex, ConvexPlaneNormal, ConvexPlanePosition);

					// Project the face verts onto the sphere along the normal and generate speculative contacts
					// We actually just take a third of the points, chosen arbitrarily. This may not be the best choice for convexes where
					// most of the face verts are close to each other with a few outliers. 
					// @todo(chaos): a better option would be to build a triangle of contacts around the primary contact, with the verts projected into the convex face
					const int32 NumConvexPlaneVertices = Convex.NumPlaneVertices(ConvexPlaneIndex);
					const int32 PlaneVertexStride = FMath::Max(1, NumConvexPlaneVertices / 3);
					for (int32 PlaneVertexIndex = 0; PlaneVertexIndex < NumConvexPlaneVertices; PlaneVertexIndex += PlaneVertexStride)
					{
						const FVec3 ConvexPlaneVertex = Convex.GetVertex(Convex.GetPlaneVertex(ConvexPlaneIndex, PlaneVertexIndex));
						const FReal ConvexContactDistance = Utilities::RaySphereIntersectionDistance(ConvexPlaneVertex, ClosestContactPoint.ShapeContactNormal, SpherePos, SphereRadius);
						if (ConvexContactDistance < CullDistance)
						{
							FContactPoint& ConvexContactPoint = ContactPoints[ContactPoints.Add()];
							ConvexContactPoint.ShapeContactPoints[0] = SphereToConvexTransform.InverseTransformPositionNoScale(ConvexPlaneVertex + ClosestContactPoint.ShapeContactNormal * ConvexContactDistance);
							ConvexContactPoint.ShapeContactPoints[1] = ConvexPlaneVertex;
							ConvexContactPoint.ShapeContactNormal = ClosestContactPoint.ShapeContactNormal;
							ConvexContactPoint.Phi = ConvexContactDistance;
							ConvexContactPoint.FaceIndex = INDEX_NONE;
							ConvexContactPoint.ContactType = EContactPointType::VertexPlane;

							if (ContactPoints.IsFull())
							{
								break;
							}
						}
					}
				}
			}
		}


		void ConstructSphereConvexManifold(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FImplicitObject3& Convex, const FRigidTransform3& ConvexTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(ConvexTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			const FRigidTransform3 SphereToConvexTransform = SphereTransform.GetRelativeTransformNoScale(ConvexTransform);

			TCArray<FContactPoint, 4> ContactPoints;
			if (const FImplicitBox3* RawBox = Convex.template GetObject<FImplicitBox3>())
			{
				ConstructSphereConvexManifoldImpl(Sphere, *RawBox, SphereToConvexTransform, Constraint.GetCullDistance(), ContactPoints);
			}
			else if (const TImplicitObjectScaled<FImplicitConvex3>* ScaledConvex = Convex.template GetObject<TImplicitObjectScaled<FImplicitConvex3>>())
			{
				ConstructSphereConvexManifoldImpl(Sphere, *ScaledConvex, SphereToConvexTransform, Constraint.GetCullDistance(), ContactPoints);
			}
			else if (const TImplicitObjectInstanced<FImplicitConvex3>* InstancedConvex = Convex.template GetObject<TImplicitObjectInstanced<FImplicitConvex3>>())
			{
				ConstructSphereConvexManifoldImpl(Sphere, *InstancedConvex, SphereToConvexTransform, Constraint.GetCullDistance(), ContactPoints);
			}
			else if (const FImplicitConvex3* RawConvex = Convex.template GetObject<FImplicitConvex3>())
			{
				ConstructSphereConvexManifoldImpl(Sphere, *RawConvex, SphereToConvexTransform, Constraint.GetCullDistance(), ContactPoints);
			}
			else
			{
				check(false);
			}

			// Add the points to the constraint
			Constraint.ResetActiveManifoldContacts();
			for (FContactPoint& ContactPoint : ContactPoints)
			{
				Constraint.AddOneshotManifoldContact(ContactPoint);
			}
		}

		template <typename TriMeshType>
		void ConstructSphereTriangleMeshOneShotManifold(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereWorldTransform, const TriMeshType& TriangleMesh, const FRigidTransform3& TriMeshWorldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereWorldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(TriMeshWorldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			FContactPoint ContactPoint = SphereTriangleMeshContactPoint(Sphere, SphereWorldTransform, TriangleMesh, TriMeshWorldTransform, Constraint.GetCullDistance(), 0.0f);
			if (ContactPoint.Phi < Constraint.GetCullDistance())
			{
				Constraint.AddOneshotManifoldContact(ContactPoint);
			}
		}

		void ConstructSphereHeightFieldOneShotManifold(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereTransform, const FHeightField& Heightfield, const FRigidTransform3& HeightfieldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(SphereTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(HeightfieldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			FContactPoint ContactPoint = SphereHeightFieldContactPoint(Sphere, SphereTransform, Heightfield, HeightfieldTransform, Constraint.GetCullDistance(), 0.0f);
			if (ContactPoint.Phi < Constraint.GetCullDistance())
			{
				Constraint.AddOneshotManifoldContact(ContactPoint);
			}
		}

		// @todo(chaos): this will be faster if we transform into the space of one of the capsules
		void ConstructCapsuleCapsuleOneShotManifold(const FCapsule& CapsuleA, const FRigidTransform3& CapsuleATransform, const FCapsule& CapsuleB, const FRigidTransform3& CapsuleBTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(CapsuleATransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(CapsuleBTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			FVec3 AAxis(CapsuleATransform.TransformVector(CapsuleA.GetSegment().GetAxis()));
			const FVec3 BAxis(CapsuleBTransform.TransformVector(CapsuleB.GetSegment().GetAxis()));

			const FReal AHalfLen = CapsuleA.GetHeight() / 2.0f;
			const FReal BHalfLen = CapsuleB.GetHeight() / 2.0f;

			// Used in a few places below where we need to use the smaller/larger capsule, but always a dynamic one
			const FReal ADynamicRadius = FConstGenericParticleHandle(Constraint.GetParticle0())->IsDynamic() ? CapsuleA.GetRadius() : TNumericLimits<FReal>::Max();
			const FReal BDynamicRadius = FConstGenericParticleHandle(Constraint.GetParticle1())->IsDynamic() ? CapsuleB.GetRadius() : TNumericLimits<FReal>::Max();

			// Make both capsules point in the same general direction
			FReal ADotB = FVec3::DotProduct(AAxis, BAxis);
			if (ADotB < 0)
			{
				ADotB = -ADotB;
				AAxis = -AAxis;
			}

			// Get the closest points on the two line segments. This is used to generate the closest contact point
			// which is always added to the manifold (if within CullDistance). We may also add other points.
			FVector AClosest, BClosest;
			const FVector ACenter = CapsuleATransform.TransformPosition(CapsuleA.GetCenter());
			const FVector BCenter = CapsuleBTransform.TransformPosition(CapsuleB.GetCenter());
			FMath::SegmentDistToSegmentSafe(
				ACenter + AHalfLen * AAxis,
				ACenter - AHalfLen * AAxis,
				BCenter + BHalfLen * BAxis,
				BCenter - BHalfLen * BAxis,
				AClosest,
				BClosest);

			FVec3 ClosestDelta = BClosest - AClosest;
			FReal ClosestDeltaLen = ClosestDelta.Size();

			// Stop now if we are beyond the cull distance
			const FReal ClosestPhi = ClosestDeltaLen - (CapsuleA.GetRadius() + CapsuleB.GetRadius());
			if (ClosestPhi > Constraint.GetCullDistance())
			{
				return;
			}

			// Calculate the normal from the two closest points. Handle exact axis overlaps.
			FVec3 ClosestNormal;
			if (ClosestDeltaLen > KINDA_SMALL_NUMBER)
			{
				ClosestNormal = -ClosestDelta / ClosestDeltaLen;
			}
			else
			{
				// Center axes exactly intersect. We'll fake a result that pops the capsules out along the Z axis, with the smaller capsule going up
				ClosestNormal = (ADynamicRadius <= BDynamicRadius) ? FVec3(0, 0, 1) : FVec3(0, 0, -1);
			}
			const FVec3 ClosestLocationA = AClosest - ClosestNormal * CapsuleA.GetRadius();
			const FVec3 ClosestLocationB = BClosest + ClosestNormal * CapsuleB.GetRadius();

			// We always add the closest point to the manifold
			// We may also add 2 more points generated from the end cap positions of the smaller capsule
			FContactPoint ClosestContactPoint;
			ClosestContactPoint.ShapeContactPoints[0] = CapsuleATransform.InverseTransformPosition(ClosestLocationA);
			ClosestContactPoint.ShapeContactPoints[1] = CapsuleBTransform.InverseTransformPosition(ClosestLocationB);
			ClosestContactPoint.ShapeContactNormal = CapsuleBTransform.InverseTransformVector(ClosestNormal);
			ClosestContactPoint.Phi = ClosestPhi;
			ClosestContactPoint.FaceIndex = INDEX_NONE;
			ClosestContactPoint.ContactType = EContactPointType::VertexPlane;
			Constraint.AddOneshotManifoldContact(ClosestContactPoint);

			// We don't generate manifold points within this fraction (of segment length) distance
			constexpr FReal TDeltaThreshold = FReal(0.2);		// fraction

			// If the nearest cylinder normal is parallel to the other axis within this tolerance, we stick with 1 manifold point
			constexpr FReal SinAngleThreshold = FReal(0.35);	// about 20deg (this would be an endcap-versus-cylinderwall collision at >70 degs)

			// If the capsules are in an X configuration, this controls the distance of the manifold points from the closest point
			const FReal RadialContactFraction = Chaos_Collision_Manifold_CapsuleRadialContactFraction;

			// Calculate the line segment times for the nearest point calculate above
			// NOTE: TA and TB will be in [-1, 1]
			const FReal TA = FVec3::DotProduct(AClosest - ACenter, AAxis) / AHalfLen;
			const FReal TB = FVec3::DotProduct(BClosest - BCenter, BAxis) / BHalfLen;

			// If we have an end-end contact with no segment overlap, stick with the single point manifold
			// This is when we have two capsules laid end to end (as opposed to side-by-side)
			// NOTE: This test only works because we made the axes point in the same direction above
			if ((TA < FReal(-1) + TDeltaThreshold) && (TB > FReal(1) - TDeltaThreshold))
			{
				return;
			}
			if ((TB < FReal(-1) + TDeltaThreshold) && (TA > FReal(1) - TDeltaThreshold))
			{
				return;
			}

			// If the axes are closely aligned, we definitely want more contact points (e.g., capsule lying on top of another).
			// Also if the contact is deep, there's a high chance that pushing one end out will push the other deeper and we also need more contacts.
			// Note: we only consider the radius of the dynamic object(s) when deciding what "deep" means because the extra contacts are only
			// to prevent excessive rotation from the single contact we have so far, and only the dynamic objects will rotate.
			const FReal AxisDotMinimum = Chaos_Collision_Manifold_CapsuleAxisAlignedThreshold;
			const FReal DeepRadiusFraction = Chaos_Collision_Manifold_CapsuleDeepPenetrationFraction;
			const FReal MinDynamicRadius = FMath::Min(ADynamicRadius, BDynamicRadius);
			const bool bAreAligned = ADotB > AxisDotMinimum;
			const bool bIsDeep = ClosestPhi < -DeepRadiusFraction * MinDynamicRadius;
			if (!bAreAligned && !bIsDeep)
			{
				return;
			}

			// Lambda: Create a contact point between a point on the cylinder of FirstCapsule at FirstT, with the nearest point on SecondCapsule
			auto MakeCapsuleSegmentContact = [](
				const FReal FirstT,
				const FVec3& FirstCenter,
				const FVec3& FirstAxis,
				const FReal FirstHalfLen,
				const FReal FirstRadius,
				const FRigidTransform3& FirstTransform,
				const FVec3& SecondCenter,
				const FVec3& SecondAxis,
				const FReal SecondHalfLen,
				const FReal SecondRadius,
				const FRigidTransform3& SecondTransform,
				const FVec3& Orthogonal,
				const FReal CullDistance,
				const bool bSwap) -> FContactPoint
			{
				FContactPoint ContactPoint;

				const FVec3 FirstContactPos = FirstCenter + (FirstT * FirstHalfLen) * FirstAxis + Orthogonal * FirstRadius;
				const FVec3 SecondSegmentPos = FMath::ClosestPointOnLine(SecondCenter - SecondHalfLen * SecondAxis, SecondCenter + SecondHalfLen * SecondAxis, FirstContactPos);
				const FReal SecondSegmentDist = (FirstContactPos - SecondSegmentPos).Size();
				const FVec3 SecondSegmentDir = (FirstContactPos - SecondSegmentPos) / SecondSegmentDist;
				const FVec3 SecondContactPos = SecondSegmentPos + SecondRadius * SecondSegmentDir;
				const FReal ContactPhi = SecondSegmentDist - SecondRadius;

				if (ContactPhi < CullDistance)
				{
					if (!bSwap)
					{
						ContactPoint.ShapeContactPoints[0] = FirstTransform.InverseTransformPositionNoScale(FirstContactPos);
						ContactPoint.ShapeContactPoints[1] = SecondTransform.InverseTransformPositionNoScale(SecondContactPos);
						ContactPoint.ShapeContactNormal = SecondTransform.InverseTransformVectorNoScale(SecondSegmentDir);
					}
					else
					{
						ContactPoint.ShapeContactPoints[0] = SecondTransform.InverseTransformPositionNoScale(SecondContactPos);
						ContactPoint.ShapeContactPoints[1] = FirstTransform.InverseTransformPositionNoScale(FirstContactPos);
						ContactPoint.ShapeContactNormal = -FirstTransform.InverseTransformVectorNoScale(SecondSegmentDir);
					}
					ContactPoint.Phi = ContactPhi;
					ContactPoint.FaceIndex = INDEX_NONE;
					ContactPoint.ContactType = EContactPointType::VertexPlane;
				}

				return ContactPoint;
			};

			// Lambda: Add up to 2 more contacts from the cylindrical surface on FirstCylinder, if they are not too close to the existing contact.
			// The point locations depend on cylinder alignment.
			auto MakeCapsuleEndPointContacts = [&Constraint, TDeltaThreshold, SinAngleThreshold, RadialContactFraction, &MakeCapsuleSegmentContact](
				const FReal FirstT,
				const FVec3& FirstCenter,
				const FVec3& FirstAxis,
				const FReal FirstHalfLen,
				const FReal FirstRadius,
				const FRigidTransform3& FirstTransform,
				const FVec3& SecondCenter,
				const FVec3& SecondAxis,
				const FReal SecondHalfLen,
				const FReal SecondRadius,
				const FRigidTransform3& SecondTransform,
				const FVec3& ClosestDir,
				const FReal FirstAxisDotSecondAxis,
				const bool bSwap) -> void
			{
				// Orthogonal: the vector from a point on FirstCapsule's axis to its cylinder surface, in the direction of SecondCapsule
				FVec3 Orthogonal = FVec3::CrossProduct(FirstAxis, FVec3::CrossProduct(FirstAxis, ClosestDir));
				const FReal OrthogonalLenSq = Orthogonal.SizeSquared();
				if (OrthogonalLenSq > FMath::Square(SinAngleThreshold))
				{
					Orthogonal = Orthogonal * FMath::InvSqrt(OrthogonalLenSq);
					if (FVec3::DotProduct(Orthogonal, SecondCenter - FirstCenter) < FReal(0))
					{
						Orthogonal = -Orthogonal;
					}

					// Clip the FirstCapsule's end points to be within the line segment of SecondCapsule
					// This is to restrict the extra contacts to the overlapping line segment (e.g, when capsules are lying partly on top of each other)
					const FReal ProjectedLen = FReal(2) * FirstHalfLen * FirstAxisDotSecondAxis;
					const FReal ClippedTMin = FVec3::DotProduct((SecondCenter - SecondHalfLen * SecondAxis) - (FirstCenter + FirstHalfLen * FirstAxis), SecondAxis) / ProjectedLen;
					const FReal ClippedTMax = FVec3::DotProduct((SecondCenter + SecondHalfLen * SecondAxis) - (FirstCenter - FirstHalfLen * FirstAxis), SecondAxis) / ProjectedLen;

					// Clip the FirstCapsules end points to be within some laterial distance od the SecondCapsule's axis
					// This restricts the contacts to be at a useful location when line segments are perpendicular to each other 
					// (e.g., when the capsules are on top of each other but in a cross)
					// As we get more perpendicular, move limits closer to radius fraction
					const FReal MaxDeltaTRadial = RadialContactFraction * (SecondRadius / FirstHalfLen);
					const FReal RadialClippedTMax = FMath::Lerp(FReal(MaxDeltaTRadial), FReal(1), FirstAxisDotSecondAxis);

					const FReal TMin = FMath::Max3(FReal(-1), ClippedTMin, -RadialClippedTMax);
					const FReal TMax = FMath::Min3(FReal(1), ClippedTMax, RadialClippedTMax);

					if (TMin < FirstT - TDeltaThreshold)
					{
						FContactPoint EndContact0 = MakeCapsuleSegmentContact(TMin, FirstCenter, FirstAxis, FirstHalfLen, FirstRadius, FirstTransform, SecondCenter, SecondAxis, SecondHalfLen, SecondRadius, SecondTransform, Orthogonal, Constraint.GetCullDistance(), bSwap);
						if (EndContact0.Phi < Constraint.GetCullDistance())
						{
							Constraint.AddOneshotManifoldContact(EndContact0);
						}
					}
					if (TMax > FirstT + TDeltaThreshold)
					{
						FContactPoint EndContact1 = MakeCapsuleSegmentContact(TMax, FirstCenter, FirstAxis, FirstHalfLen, FirstRadius, FirstTransform, SecondCenter, SecondAxis, SecondHalfLen, SecondRadius, SecondTransform, Orthogonal, Constraint.GetCullDistance(), bSwap);
						if (EndContact1.Phi < Constraint.GetCullDistance())
						{
							Constraint.AddOneshotManifoldContact(EndContact1);
						}
					}
				}
			};

			// Generate the extra manifold points
			if (ADynamicRadius <= BDynamicRadius)
			{
				MakeCapsuleEndPointContacts(TA, ACenter, AAxis, AHalfLen, CapsuleA.GetRadius(), CapsuleATransform, BCenter, BAxis, BHalfLen, CapsuleB.GetRadius(), CapsuleBTransform, ClosestNormal, ADotB, false);
			}
			else
			{
				MakeCapsuleEndPointContacts(TB, BCenter, BAxis, BHalfLen, CapsuleB.GetRadius(), CapsuleBTransform, ACenter, AAxis, AHalfLen, CapsuleA.GetRadius(), CapsuleATransform, ClosestNormal, ADotB, true);
			}
		}

		template <typename TriMeshType>
		void ConstructCapsuleTriMeshOneShotManifold(const FCapsule& Capsule, const FRigidTransform3& CapsuleWorldTransform, const TriMeshType& TriangleMesh, const FRigidTransform3& TriMeshWorldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(CapsuleWorldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(TriMeshWorldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			TArray<FContactPoint> ContactPoints;
			GJKImplicitManifold<FCapsule, TriMeshType>(Capsule, CapsuleWorldTransform, TriangleMesh, TriMeshWorldTransform, Constraint.GetCullDistance(), 0.0f, ContactPoints);
			for (FContactPoint& ContactPoint : ContactPoints)
			{
				if (ContactPoint.Phi < Constraint.GetCullDistance())
				{
					Constraint.AddOneshotManifoldContact(ContactPoint);
				}
			}
		}

		void ConstructCapsuleHeightFieldOneShotManifold(const FCapsule& Capsule, const FRigidTransform3& CapsuleTransform, const FHeightField& HeightField, const FRigidTransform3& HeightFieldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(CapsuleTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(HeightFieldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			TArray<FContactPoint> ContactPoints;
			GJKImplicitManifold<FCapsule>(Capsule, CapsuleTransform, HeightField, HeightFieldTransform, Constraint.GetCullDistance(), 0.0f, ContactPoints);
			for (FContactPoint& ContactPoint : ContactPoints)
			{
				if (ContactPoint.Phi < Constraint.GetCullDistance())
				{
					Constraint.AddOneshotManifoldContact(ContactPoint);
				}
			}
		}

		template<typename ConvexType>
		void ConstructConvexHeightFieldOneShotManifold(const FImplicitObject& Convex, const FRigidTransform3& ConvexTransform, const FHeightField& HeightField, const FRigidTransform3& HeightFieldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint)
		{
			// We only build one shot manifolds once
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(ConvexTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(HeightFieldTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// @todo(chaos): support manifold maintenance
			Constraint.ResetActiveManifoldContacts();

			//FContactPoint ContactPoint = ConvexHeightFieldContactPoint(Convex, ConvexTransform, HeightField, HeightFieldTransform, Constraint.GetCullDistance(), 0.0f);

			TArray<FContactPoint> ContactPoints;
			GJKImplicitManifold<ConvexType>(Convex, ConvexTransform, HeightField, HeightFieldTransform, Constraint.GetCullDistance(), 0.0f, ContactPoints);
			for (FContactPoint& ContactPoint : ContactPoints)
			{
				if (ContactPoint.Phi < Constraint.GetCullDistance())
				{
					Constraint.AddOneshotManifoldContact(ContactPoint);
				}
			}
		}

		template<typename ConvexType>
		void ConstructPlanarConvexTriMeshOneShotManifoldImp(const ConvexType& Convex, const FRigidTransform3& ConvexTransform, const FImplicitObject& TriMesh, const FRigidTransform3& TriMeshTransform, FPBDCollisionConstraint& Constraint)
		{
			// All convexes are pre-scaled, or wrapped in TImplicitObjectScaled
			ensure(ConvexTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));
			ensure(TriMeshTransform.GetScale3D() == FVec3(1.0f, 1.0f, 1.0f));

			// Unwrap the tri mesh (remove Scaled or Instanced) and get the scale
			FVec3 TriMeshScale;
			FReal TriMeshMargin;	// Not used - will be zero
			const FTriangleMeshImplicitObject* UnscaledTriMesh = UnwrapImplicit<FTriangleMeshImplicitObject>(TriMesh, TriMeshScale, TriMeshMargin);
			check(UnscaledTriMesh != nullptr);

			const FRigidTransform3 TriangleMeshToConvexTransform = TriMeshTransform.GetRelativeTransformNoScale(ConvexTransform);

			// Calculate the query bounds in trimesh space
			// NOTE: to handle negative scales, we need to include it in the AABB transform (cannot use FAABB3::Scale)
			const FRigidTransform3 QueryTransform = FRigidTransform3(TriangleMeshToConvexTransform.GetTranslation(), TriangleMeshToConvexTransform.GetRotation(), TriMeshScale);
			const FAABB3 TriMeshQueryBounds = Convex.BoundingBox().InverseTransformedAABB(QueryTransform);
			const FReal CullDistance = Constraint.GetCullDistance();

			// A set of contact points which contains points from all triangle-convex manifolds
			TArray<FContactPoint> ContactPoints;

			// Prime the triangle producer with overlappig indices
			FTriangleMeshTriangleProducer TriangleProducer;
			int32 TriangleIndex;
			FTriangle Triangle;
			TCArray<FContactPoint, 4> TriangleManifoldPoints;
			TriangleProducer.Reset(*UnscaledTriMesh, TriMeshQueryBounds);

			// Loop over all the triangles, build a manifold and add the points to the total manifold
			while (TriangleProducer.NextTriangle(*UnscaledTriMesh, QueryTransform, Triangle, TriangleIndex))
			{
				TriangleManifoldPoints.Reset();
				ConstructPlanarConvexTriangleOneShotManifold(Convex, Triangle, CullDistance, TriangleManifoldPoints);
					
				for (int32 TriangleContactIndex = 0; TriangleContactIndex < TriangleManifoldPoints.Num(); ++TriangleContactIndex)
				{
					FContactPoint& ContactPoint = ContactPoints[ContactPoints.AddUninitialized()];
					ContactPoint.ShapeContactPoints[0] = TriangleManifoldPoints[TriangleContactIndex].ShapeContactPoints[0];
					ContactPoint.ShapeContactPoints[1] = TriangleMeshToConvexTransform.InverseTransformPositionNoScale(TriangleManifoldPoints[TriangleContactIndex].ShapeContactPoints[1]);
					ContactPoint.ShapeContactNormal = TriangleMeshToConvexTransform.InverseTransformVectorNoScale(TriangleManifoldPoints[TriangleContactIndex].ShapeContactNormal);
					ContactPoint.Phi = TriangleManifoldPoints[TriangleContactIndex].Phi;
					ContactPoint.FaceIndex = TriangleIndex;
					ContactPoint.ContactType = TriangleManifoldPoints[TriangleContactIndex].ContactType;
				}
			}

			// Remove edge contacts that are "hidden" by face contacts
			// @todo(chaos): EdgePruneDistance should be some fraction of the convex margin...
			if (ContactPoints.Num() > 0)
			{
				const FReal EdgePruneDistance = Chaos_Collision_EdgePrunePlaneDistance;
				Collisions::PruneEdgeContactPointsUnordered(ContactPoints, EdgePruneDistance);
			}

			// Whittle the manifold down to 4 points
			if (ContactPoints.Num() > 4)
			{
				std::sort(&ContactPoints[0], &ContactPoints[0] + ContactPoints.Num(), [](const FContactPoint& L, const FContactPoint& R) { return L.Phi < R.Phi; });

				// Remove all points (except for the deepest one, and ones with phis similar to it)
				// NOTE: relies on the sort above
				const FReal CullMargin = 0.1f;
				int32 NewContactPointCount = ContactPoints.Num() > 0 ? 1 : 0;
				for (int32 Index = 1; Index < ContactPoints.Num(); Index++)
				{
					if (ContactPoints[Index].Phi < 0 || ContactPoints[Index].Phi - ContactPoints[0].Phi < CullMargin)
					{
						NewContactPointCount++;
					}
					else
					{
						break;
					}
				}
				ContactPoints.SetNum(NewContactPointCount, false);
			}

			if (ContactPoints.Num() > 4)
			{
				// Reduce to only 4 contact points from here
				// NOTE: relies on the sort above
				Collisions::ReduceManifoldContactPointsTriangeMesh(ContactPoints);
			}

			// Add the manifold points to the constraint
			Constraint.ResetActiveManifoldContacts();
			for (FContactPoint& ContactPoint : ContactPoints)
			{
				// NOTE: We don't reuse manifolds between frames for Convex-TriMesh so it's not too bad to
				// skip manifold points that are beyond the cull distance
				if (ContactPoint.Phi < Constraint.GetCullDistance())
				{
					Constraint.AddOneshotManifoldContact(ContactPoint);
				}
			}
		}

		void ConstructPlanarConvexTriMeshOneShotManifold(const FImplicitObject& Convex, const FRigidTransform3& ConvexTransform, const FImplicitObject& TriangleMesh, const FRigidTransform3& TriangleMeshTransform, FPBDCollisionConstraint& Constraint)
		{
			if (const FImplicitBox3* RawBox = Convex.template GetObject<FImplicitBox3>())
			{
				ConstructPlanarConvexTriMeshOneShotManifoldImp(*RawBox, ConvexTransform, TriangleMesh, TriangleMeshTransform, Constraint);
			}
			else if (const TImplicitObjectScaled<FImplicitConvex3>* ScaledConvex = Convex.template GetObject<TImplicitObjectScaled<FImplicitConvex3>>())
			{
				ConstructPlanarConvexTriMeshOneShotManifoldImp(*ScaledConvex, ConvexTransform, TriangleMesh, TriangleMeshTransform, Constraint);
			}
			else if (const TImplicitObjectInstanced<FImplicitConvex3>* InstancedConvex = Convex.template GetObject<TImplicitObjectInstanced<FImplicitConvex3>>())
			{
				ConstructPlanarConvexTriMeshOneShotManifoldImp(*InstancedConvex, ConvexTransform, TriangleMesh, TriangleMeshTransform, Constraint);
			}
			else if (const FImplicitConvex3* RawConvex = Convex.template GetObject<FImplicitConvex3>())
			{
				ConstructPlanarConvexTriMeshOneShotManifoldImp(*RawConvex, ConvexTransform, TriangleMesh, TriangleMeshTransform, Constraint);
			}
			else
			{
				check(false);
			}
		}


		template void ConstructSphereTriangleMeshOneShotManifold<TImplicitObjectScaled<class Chaos::FTriangleMeshImplicitObject, 1>>(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereWorldTransform, const TImplicitObjectScaled<class Chaos::FTriangleMeshImplicitObject, 1>& TriangleMesh, const FRigidTransform3& TriMeshWorldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint);
		template void ConstructSphereTriangleMeshOneShotManifold<FTriangleMeshImplicitObject>(const TSphere<FReal, 3>& Sphere, const FRigidTransform3& SphereWorldTransform, const FTriangleMeshImplicitObject& TriangleMesh, const FRigidTransform3& TriMeshWorldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint);

		template void ConstructCapsuleTriMeshOneShotManifold<TImplicitObjectScaled<class Chaos::FTriangleMeshImplicitObject, 1>>(const FCapsule& Capsule, const FRigidTransform3& CapsuleWorldTransform, const TImplicitObjectScaled<class Chaos::FTriangleMeshImplicitObject, 1>& TriangleMesh, const FRigidTransform3& TriMeshWorldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint);
		template void ConstructCapsuleTriMeshOneShotManifold<FTriangleMeshImplicitObject>(const FCapsule& Capsule, const FRigidTransform3& CapsuleWorldTransform, const FTriangleMeshImplicitObject& TriangleMesh, const FRigidTransform3& TriMeshWorldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint);

		template void ConstructConvexHeightFieldOneShotManifold<FConvex>(const FImplicitObject& Convex, const FRigidTransform3& ConvexTransform, const FHeightField& HeightField, const FRigidTransform3& HeightFieldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint);
		template void ConstructConvexHeightFieldOneShotManifold<FImplicitBox3>(const FImplicitObject& Convex, const FRigidTransform3& ConvexTransform, const FHeightField& HeightField, const FRigidTransform3& HeightFieldTransform, const FReal Dt, FPBDCollisionConstraint& Constraint);
	}
}

