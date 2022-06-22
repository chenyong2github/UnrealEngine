// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"

#include "Chaos/BVHParticles.h"
#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionKeys.h"
#include "Chaos/Collision/ContactPoint.h"
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/Framework/UncheckedArray.h"
#include "Chaos/GJK.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Vector.h"

namespace Chaos
{
	class FCollisionConstraintAllocator;
	class FConstGenericParticleHandle;
	class FImplicitObject;
	class FParticlePairMidPhase;
	class FPBDCollisionConstraint;
	class FPBDCollisionConstraints;
	class FPBDCollisionSolver;
	class FSingleShapePairCollisionDetector;
	class FSolverBody;
	class FSolverBodyContainer;
	class FPBDCollisionSolverContainer;
	class FPerShapeData;

	UE_DEPRECATED(4.27, "Use FPBDCollisionConstraint instead")
	typedef FPBDCollisionConstraint FRigidBodyPointContactConstraint;

	CHAOS_API bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R);

	/**
	 * @brief A single point in a contact manifold.
	 * Each Collision Constraint will have up to 4 of these.
	*/
	class CHAOS_API FManifoldPoint
	{
	public:
		FManifoldPoint() 
			: ContactPoint()
			, ShapeAnchorPoints{ FVec3(0), FVec3(0) }
			, InitialShapeContactPoints{ FVec3(0), FVec3(0) }
			, NetPushOut(0)
			, NetImpulse(0)
			, TargetPhi(0)
			, Flags()
		{}

		FManifoldPoint(const FContactPoint& InContactPoint) 
			: ContactPoint(InContactPoint)
			, ShapeAnchorPoints{ FVec3(0), FVec3(0) }
			, InitialShapeContactPoints{ FVec3(0), FVec3(0) }
			, NetPushOut(0)
			, NetImpulse(0)
			, TargetPhi(0)
			, Flags()
		{}

		FContactPoint ContactPoint;			// Contact point results of low-level collision detection
		FVec3 ShapeAnchorPoints[2];			// The contact points from a prior frame, if availble. Used by friction.
		FVec3 InitialShapeContactPoints[2];	// ShapeContactPoints when the constraint was first initialized. Used to track reusablility
		FVec3 NetPushOut;					// Total pushout applied at this contact point
		FVec3 NetImpulse;					// Total impulse applied by this contact point
		FReal TargetPhi;					// Usually 0, but can be used to add padding or penetration (e.g., via a collision modifer)

		union FFlags
		{
			FFlags() { Reset(); }
			
			void Reset() { Bits = 0; }

			struct
			{
				uint8 bDisabled : 1;						// Whether the point was disabled by edge pruning etc
				uint8 bWasRestored : 1;						// Whether the point was retored from the previous frame due to lack of movement
				uint8 bWasReplaced : 1;						// @todo(chaos): remove this
				uint8 bWasFrictionRestored : 1;				// Whether we used data from the previous frame to set up the contacts to maintain static friction
				uint8 bInsideStaticFrictionCone : 1;		// Whether we are inside the static friction cone
			};
			uint8 Bits;
		} Flags;
	};

	/**
	 * @brief The friction data for a manifold point
	 * This is the information that needs to be stored between ticks to implement static friction.
	*/
	class CHAOS_API FSavedManifoldPoint
	{
	public:
		FVec3 ShapeContactPoints[2];
	};

	/*
	 * @brief Material properties for a collision constraint
	*/
	class CHAOS_API FPBDCollisionConstraintMaterial
	{
	public:
		FPBDCollisionConstraintMaterial()
			: MaterialDynamicFriction(0)
			, MaterialStaticFriction(0)
			, MaterialRestitution(0)
			, DynamicFriction(0)
			, StaticFriction(0)
			, Restitution(0)
			, RestitutionPadding(0)
			, RestitutionThreshold(0)
			, InvMassScale0(1.f)
			, InvMassScale1(1.f)
			, InvInertiaScale0(1.f)
			, InvInertiaScale1(1.f)
		{
		}

		// Material properties pulled from the materials of the two shapes involved in the contact
		FReal MaterialDynamicFriction;
		FReal MaterialStaticFriction;
		FReal MaterialRestitution;

		// Final material properties (post modifier) used by the solver. These get reset every frame to the material values above
		FReal DynamicFriction;
		FReal StaticFriction;
		FReal Restitution;

		FReal RestitutionPadding; // For StandardPBD implementation of resitution, we pad constraints on initial contact to enforce outward velocity
		FReal RestitutionThreshold;
		FReal InvMassScale0;
		FReal InvMassScale1;
		FReal InvInertiaScale0;
		FReal InvInertiaScale1;

		// Reset the material properties to those pulled from the shape's materials (i.e., back to the state before any contact modification)
		void ResetMaterialModifications()
		{
			DynamicFriction = MaterialDynamicFriction;
			StaticFriction = MaterialStaticFriction;
			Restitution = MaterialRestitution;
			InvMassScale0 = FReal(1);
			InvMassScale1 = FReal(1);
			InvInertiaScale0 = FReal(1);
			InvInertiaScale1 = FReal(1);
		}

		// @todo(chaos): remove this
		void Reset()
		{
			RestitutionPadding = 0;
		}
	};

	// Renamed to FPBDCollisionConstraintMaterial
	using FCollisionContact UE_DEPRECATED(5.1, "FCollisionContact was renamed to FPBDCollisionConstraintMaterial") = FPBDCollisionConstraintMaterial;


	/**
	 * @brief Information used by the constraint allocator
	 * This includes any information used for optimizations like array indexes etc
	 * as well as data for managing lifetime and pruning.
	*/
	class FPBDCollisionConstraintContainerCookie
	{
	public:
		FPBDCollisionConstraintContainerCookie()
			: MidPhase(nullptr)
			, bIsMultiShapePair(false)
			, CreationEpoch(INDEX_NONE)
			, LastUsedEpoch(INDEX_NONE)
			, ConstraintIndex(INDEX_NONE)
			, SweptConstraintIndex(INDEX_NONE)
		{
		}

		/**
		 * @brief Used to clear the container data when copying constraints out of the container (see resim cache)
		 * The constraint index will not be valid when the constraint is copied out of the container, but everything
		 * else is ok and should be restorable.
		*/
		void ClearContainerData()
		{
			MidPhase = nullptr;
			ConstraintIndex = INDEX_NONE;
			SweptConstraintIndex = INDEX_NONE;
		}

		// The constraint owner - set when the constraint is created
		FParticlePairMidPhase* MidPhase;

		// Used by the MidPhase when a constraint is reactivated from a Resim cache
		// If true, indicates that the constraint was created from the recursive collision detection
		// path rather than the prefiltered shape-pair loop
		bool bIsMultiShapePair;

		// The Epoch when then constraint was initially created
		int32 CreationEpoch;

		// The Epoch when the constraint was last used
		int32 LastUsedEpoch;

		// The index in the container - this changes every tick
		int32 ConstraintIndex;

		// The index in swept constraints - this changes every tick
		int32 SweptConstraintIndex;
	};


	/**
	 * @brief A contact constraint
	 * 
	 * A contact constraint represents the non-penetration, friction, and restitution constraints for a single
	 * shape pair on a particle pair. I.e., a specific particle-pair may have multiple contact constraints
	 * between them if one or boht has multuple collision shapes that overlap the shape(s) of the other body.
	 * 
	 * Each contact constraint contains a Manifold, which is a set of contact points that approximate the
	 * contact patch between the two shapes.
	 * 
	 * Contact constraints are allocated on the heap and have permanent addresses. They use intrusive handles
	 * to reduce unnecessary indirection.
	 * 
	*/
	class CHAOS_API FPBDCollisionConstraint : public FPBDCollisionConstraintHandle
	{
		friend class FCollisionConstraintAllocator;
		friend class FMultiShapePairCollisionDetector;
		friend class FParticlePairMidPhase;
		friend class FPBDCollisionConstraints;
		friend class FSingleShapePairCollisionDetector;

		friend CHAOS_API bool ContactConstraintSortPredicate(const FPBDCollisionConstraint& L, const FPBDCollisionConstraint& R);

	public:
		using FConstraintContainerHandle = TIntrusiveConstraintHandle<FPBDCollisionConstraint>;

		static const int32 MaxManifoldPoints = 4;

		/**
		 * @brief Create a contact constraint
		 * Initializes a constraint stored inline in an object. Only intended to be called once.
		 * Does not reinitialize all data so not intended to reset a constraint for reuse with different particles etc.
		*/
		static void MakeInline(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FPerShapeData* Shape0,
			const FBVHParticles* Simplicial0,
			const FRigidTransform3& ImplicitLocalTransform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FPerShapeData* Shape1,
			const FBVHParticles* Simplicial1,
			const FRigidTransform3& ImplicitLocalTransform1,
			const FReal InCullDistance,
			const bool bInUseManifold,
			const EContactShapesType ShapesType,
			FPBDCollisionConstraint& OutConstraint);

		/**
		 * @brief Create a contact constraint
		 * Allocates a constraint on the heap, with a permanent address.
		 * May return null if we hit the contact limit for the scene.
		*/
		static TUniquePtr<FPBDCollisionConstraint> Make(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FPerShapeData* Shape0,
			const FBVHParticles* Simplicial0,
			const FRigidTransform3& ImplicitLocalTransform0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FPerShapeData* Shape1,
			const FBVHParticles* Simplicial1,
			const FRigidTransform3& ImplicitLocalTransform1,
			const FReal InCullDistance,
			const bool bInUseManifold,
			const EContactShapesType ShapesType);

		/**
		 * @brief For use by the tri mesh and heighfield collision detection as a temporary measure
		 * @see FHeightField::ContactManifoldImp, FTriangleMeshImplicitObject::ContactManifoldImp
		*/
		static FPBDCollisionConstraint MakeTriangle(const FImplicitObject* Implicit0);

		/**
		 * @brief Return a constraint copied from the Source constraint, for use in the Resim Cache or other system
		 * @note Unlike the other factory method, this version returns a constraint by value for emplacing into an array (ther others are by pointer)
		*/
		static FPBDCollisionConstraint MakeCopy(const FPBDCollisionConstraint& Source);

		FPBDCollisionConstraint();

		/**
		 * @brief The current CCD state of this constraint
		 * This may change from tick to tick as an object's velocity changes.
		*/
		ECollisionCCDType GetCCDType() const { return CCDType; }

		/**
		 * @brief Enable or disable CCD for this constraint
		*/
		void SetCCDEnabled(const bool bCCDEnabled)
		{ 
			CCDType = bCCDEnabled ? ECollisionCCDType::Enabled : ECollisionCCDType::Disabled;

			// Initialize the CCD thresholds (one time only)
			if (bCCDEnabled && (CCDEnablePenetration == FReal(0)))
			{
				InitCCDThreshold();
			}
		}

		/**
		 * @brief If CCD is enabled, contacts deeper than this will be handled by CCD
		*/
		FReal GetCCDEnablePenetration() const
		{
			return CCDEnablePenetration;
		}

		/**
		 * @brief If CCD is enabled and processed the contact, CCD resolution leaves up to this much penetration
		*/
		FReal GetCCDTargetPenetration() const
		{
			return CCDTargetPenetration;
		}

		//
		// API
		//

		FGeometryParticleHandle* GetParticle0() { return Particle[0]; }
		const FGeometryParticleHandle* GetParticle0() const { return Particle[0]; }
		FGeometryParticleHandle* GetParticle1() { return Particle[1]; }
		const FGeometryParticleHandle* GetParticle1() const { return Particle[1]; }
		FGeometryParticleHandle* GetParticle(const int32 ParticleIndex) { check((ParticleIndex >= 0) && (ParticleIndex < 2)); return Particle[ParticleIndex]; }
		const FGeometryParticleHandle* GetParticle(const int32 ParticleIndex) const { check((ParticleIndex >= 0) && (ParticleIndex < 2)); return Particle[ParticleIndex]; }

		const FImplicitObject* GetImplicit0() const { return Implicit[0]; }
		const FImplicitObject* GetImplicit1() const { return Implicit[1]; }
		const FImplicitObject* GetImplicit(const int32 ParticleIndex) const { check((ParticleIndex >= 0) && (ParticleIndex < 2)); return Implicit[ParticleIndex]; }

		const FPerShapeData* GetShape0() const { return Shape[0]; }
		const FPerShapeData* GetShape1() const { return Shape[1]; }
		const FPerShapeData* GetShape(const int32 ParticleIndex) const { check((ParticleIndex >= 0) && (ParticleIndex < 2)); return Shape[ParticleIndex]; }

		const FBVHParticles* GetCollisionParticles0() const { return Simplicial[0]; }
		const FBVHParticles* GetCollisionParticles1() const { return Simplicial[1]; }
		const FBVHParticles* GetCollisionParticles(const int32 ParticleIndex) const { check((ParticleIndex >= 0) && (ParticleIndex < 2)); return Simplicial[ParticleIndex]; }

		const FReal GetCollisionMargin0() const { return CollisionMargins[0]; }
		const FReal GetCollisionMargin1() const { return CollisionMargins[1]; }

		const bool IsQuadratic0() const { return Flags.bIsQuadratic0; }
		const bool IsQuadratic1() const { return Flags.bIsQuadratic1; }
		const bool HasQuadraticShape() const { return (Flags.bIsQuadratic0 || Flags.bIsQuadratic1); }
		const FReal GetCollisionRadius0() const { return (Flags.bIsQuadratic0) ? CollisionMargins[0] : FReal(0); }
		const FReal GetCollisionRadius1() const { return (Flags.bIsQuadratic1) ? CollisionMargins[1] : FReal(0); }

		/** \brief Called each frame when the constraint is active after primary collision detection (but not per incremental collision detection call if enabled) */
		void Activate()
		{
			AccumulatedImpulse = FVec3(0);
			SetDisabled(false);
		}

		// @todo(chaos): half of this API is wrong for the new multi-point manifold constraints. Remove it

		void ResetPhi(FReal InPhi) { ClosestManifoldPointIndex = INDEX_NONE; }
		FReal GetPhi() const { return (ClosestManifoldPointIndex != INDEX_NONE) ? ManifoldPoints[ClosestManifoldPointIndex].ContactPoint.Phi : TNumericLimits<FReal>::Max(); }

		void SetDisabled(bool bInDisabled) { Flags.bDisabled = bInDisabled; }
		bool GetDisabled() const { return Flags.bDisabled; }

		void SetIsProbe(bool bInProbe) { Flags.bIsProbe = bInProbe; }
		bool GetIsProbe() const { return Flags.bIsProbe; }

		virtual void SetIsSleeping(const bool bInIsSleeping) override;

		// Get the world-space normal of the closest manifold point
		// @todo(chaos): remove (used by legacy RBAN collision solver)
		FVec3 CalculateWorldContactNormal() const;

		// Get the world-space contact location of the closest manifold point
		// @todo(chaos): remove (used by legacy RBAN collision solver)
		FVec3 CalculateWorldContactLocation() const;

		void SetInvMassScale0(const FReal InInvMassScale) { Material.InvMassScale0 = InInvMassScale; }
		FReal GetInvMassScale0() const { return Material.InvMassScale0; }

		void SetInvMassScale1(const FReal InInvMassScale) { Material.InvMassScale1 = InInvMassScale; }
		FReal GetInvMassScale1() const { return Material.InvMassScale1; }

		void SetInvInertiaScale0(const FReal InInvInertiaScale) { Material.InvInertiaScale0 = InInvInertiaScale; }
		FReal GetInvInertiaScale0() const { return Material.InvInertiaScale0; }

		void SetInvInertiaScale1(const FReal InInvInertiaScale) { Material.InvInertiaScale1 = InInvInertiaScale; }
		FReal GetInvInertiaScale1() const { return Material.InvInertiaScale1; }

		void SetStiffness(FReal InStiffness) { Stiffness = InStiffness; }
		FReal GetStiffness() const { return Stiffness; }

		void SetRestitution(const FReal InRestitution) { Material.Restitution = InRestitution; }
		FReal GetRestitution() const { return Material.Restitution; }

		void SetRestitutionThreshold(const FReal InRestitutionThreshold) { Material.RestitutionThreshold = InRestitutionThreshold; }
		FReal GetRestitutionThreshold() const { return Material.RestitutionThreshold; }

		void SetRestitutionPadding(const FReal InRestitutionPadding) { Material.RestitutionPadding = InRestitutionPadding; }
		FReal GetRestitutionPadding() const { return Material.RestitutionPadding; }

		void SetStaticFriction(const FReal InStaticFriction) { Material.StaticFriction = InStaticFriction; }
		FReal GetStaticFriction() const { return FMath::Max(Material.StaticFriction, Material.DynamicFriction); }

		void SetDynamicFriction(const FReal InDynamicFriction) { Material.DynamicFriction = InDynamicFriction; }
		FReal GetDynamicFriction() const { return Material.DynamicFriction; }

		EContactShapesType GetShapesType() const { return ShapesType; }

		FString ToString() const;

		FReal GetCullDistance() const { return CullDistance; }
		void SetCullDistance(FReal InCullDistance) { CullDistance = InCullDistance; }

		// Whether we are using manifolds (either one-shot or incremental)
		bool GetUseManifold() const { return Flags.bUseManifold; }

		// Whether we can use incremental manifolds (updated each iteration)
		bool GetUseIncrementalManifold() const { return Flags.bUseIncrementalManifold; }

		// Whether we run collision detection every iteration (true if we are not using one shot manifolds)
		// NOTE: This is initially set based on whether are allowing incremental manifolds
		bool GetUseIncrementalCollisionDetection() const { return !Flags.bUseManifold || Flags.bUseIncrementalManifold; }

		/**
		* Reset the material properties to those from the shape materials. Called each frame to reset contact modifications to the material.
		*/
		inline void ResetModifications()
		{
			Material.ResetMaterialModifications();

			// Reset other properties which may have changed in contact modification last frame
			Flags.bIsProbe = Flags.bIsProbeUnmodified;
		}

		/**
		 * @brief Clear the current and previous manifolds
		*/
		void ResetManifold();

		// @todo(chaos): remove array view and provide per-point accessor
		TArrayView<FManifoldPoint> GetManifoldPoints() { return MakeArrayView(ManifoldPoints.begin(), ManifoldPoints.Num()); }
		TArrayView<const FManifoldPoint> GetManifoldPoints() const { return MakeArrayView(ManifoldPoints.begin(), ManifoldPoints.Num()); }

		int32 NumManifoldPoints() const { return ManifoldPoints.Num(); }
		FManifoldPoint& GetManifoldPoint(const int32 PointIndex) { return ManifoldPoints[PointIndex]; }
		const FManifoldPoint& GetManifoldPoint(const int32 PointIndex) const { return ManifoldPoints[PointIndex]; }
		const FManifoldPoint* GetClosestManifoldPoint() const { return (ClosestManifoldPointIndex != INDEX_NONE) ? &ManifoldPoints[ClosestManifoldPointIndex] : nullptr; }
		const FSavedManifoldPoint* FindSavedManifoldPoint(const FManifoldPoint& ManifoldPoint) const;
		void ResetSavedManifoldPoints() { SavedManifoldPoints.Reset(); }

		inline int32 NumEnabledManifoldPoints() const
		{
			int32 NumEnabled = 0;
			if (IsEnabled())
			{
				for (int32 ManifoldPointIndex = 0; ManifoldPointIndex < NumManifoldPoints(); ++ManifoldPointIndex)
				{
					if (!ManifoldPoints[ManifoldPointIndex].Flags.bDisabled)
					{
						++NumEnabled;
					}
				}
			}
			return NumEnabled;
		}

		inline void DisableManifoldPoint(const int32 DisabledManifoldPointIndex)
		{
			ManifoldPoints[DisabledManifoldPointIndex].Flags.bDisabled = true;
			if (NumEnabledManifoldPoints() == 0)
			{
				SetDisabled(true);
			}
		}

		void AddIncrementalManifoldContact(const FContactPoint& ContactPoint);

		// @todo(chaos): remove this and use SetOneShotManifoldContacts
		inline void AddOneshotManifoldContact(const FContactPoint& ContactPoint)
		{
			if (ContactPoint.IsSet() && !ManifoldPoints.IsFull())
			{
				int32 ManifoldPointIndex = AddManifoldPoint(ContactPoint);
				if (ManifoldPoints[ManifoldPointIndex].ContactPoint.Phi < GetPhi())
				{
					ClosestManifoldPointIndex = ManifoldPointIndex;
				}

				// If we can use a one-shot manifold with this shape pair, disable the incremental flag
				// to prevent calling collision detection again
				Flags.bUseIncrementalManifold = false;
			}
		}

		/**
		 * @brief Replace the current manifold points with the input.
		 * The input array should contain no more than MaxManifoldPoints contacts (any extra will be ignored).
		 * We assume that all input contacts have been initialized and will not return false from IsSet().
		 * Ignores contacts deeper than the CullDistance for this constraint.
		*/
		inline void SetOneShotManifoldContacts(const TArrayView<const FContactPoint>& ContactPoints)
		{
			ResetActiveManifoldContacts();

			// Disable the incremental flag to prevent calling collision detection again
			Flags.bUseIncrementalManifold = false;

			FReal MinPhi = TNumericLimits<FReal>::Max();
			const int32 NumContacts = FMath::Min(ContactPoints.Num(), MaxManifoldPoints);
			for (int32 ContactIndex = 0; ContactIndex < NumContacts; ++ContactIndex)
			{
				const FContactPoint& ContactPoint = ContactPoints[ContactIndex];
				if (ContactPoint.Phi < CullDistance)
				{
					int32 ManifoldPointIndex = AddManifoldPoint(ContactPoint);
					if (ContactPoint.Phi < MinPhi)
					{
						ClosestManifoldPointIndex = ManifoldPointIndex;
						MinPhi = ContactPoint.Phi;
					}
				}
			}
		}

		void UpdateManifoldContacts();

		// Particle-relative transform of each collision shape in the constraint
		const FRigidTransform3& GetShapeRelativeTransform0() const { return ImplicitTransform[0]; }
		const FRigidTransform3& GetShapeRelativeTransform1() const { return ImplicitTransform[1]; }
		const FRigidTransform3& GetShapeRelativeTransform(const int32 ParticleIndex) const { check((ParticleIndex >= 0) && (ParticleIndex < 2)); return ImplicitTransform[ParticleIndex]; }

		const FRigidTransform3& GetShapeWorldTransform0() const { return ShapeWorldTransform0; }
		const FRigidTransform3& GetShapeWorldTransform1() const { return ShapeWorldTransform1; }

		void SetShapeWorldTransforms(const FRigidTransform3& InShapeWorldTransform0, const FRigidTransform3& InShapeWorldTransform1)
		{
			ShapeWorldTransform0 = InShapeWorldTransform0;
			ShapeWorldTransform1 = InShapeWorldTransform1;
		}

		void SetLastShapeWorldTransforms(const FRigidTransform3& InShapeWorldTransform0, const FRigidTransform3& InShapeWorldTransform1)
		{
			LastShapeWorldPositionDelta = InShapeWorldTransform0.GetTranslation() - InShapeWorldTransform1.GetTranslation();
			LastShapeWorldRotationDelta = InShapeWorldTransform0.GetRotation().Inverse() * InShapeWorldTransform1.GetRotation();
		}

		bool UpdateAndTryRestoreManifold();
		void ResetActiveManifoldContacts();
		bool TryAddManifoldContact(const FContactPoint& ContactPoint);
		bool TryInsertManifoldContact(const FContactPoint& ContactPoint);

		//@ todo(chaos): These are for the collision forwarding system - this should use the collision modifier system (which should be extended to support adding collisions)
		void SetManifoldPoints(const TArray<FManifoldPoint>& InManifoldPoints)
		{ 
			ManifoldPoints.SetNum(FMath::Min(MaxManifoldPoints, InManifoldPoints.Num()));
			for (int32 ManifoldPointIndex = 0; ManifoldPoints.Num(); ++ManifoldPointIndex)
			{
				ManifoldPoints[ManifoldPointIndex] = InManifoldPoints[ManifoldPointIndex];
			}
		}

		// The GJK warm-start data. This is updated directly in the narrow phase
		FGJKSimplexData& GetGJKWarmStartData() { return GJKWarmStartData; }

		FSolverBody* GetSolverBody0() { return SolverBodies[0]; }
		const FSolverBody* GetSolverBody0() const { return SolverBodies[0]; }
		FSolverBody* GetSolverBody1() { return SolverBodies[1]; }
		const FSolverBody* GetSolverBody1() const { return SolverBodies[1]; }

		void SetSolverBodies(FSolverBody* InSolverBody0, FSolverBody* InSolverBody1)
		{
			SolverBodies[0] = InSolverBody0;
			SolverBodies[1] = InSolverBody1;
		}

		int32 GetSolverIndex() const { return SolverIndex; }
		void SetSolverIndex(int32 InSolverIndex)
		{
			SolverIndex = InSolverIndex;
		}

		/**
		 * @brief Whether this constraint was fully restored from a previous tick, and the manifold should be reused as-is
		*/
		bool WasManifoldRestored() const { return Flags.bWasManifoldRestored; }

		/**
		 * Determine the constraint direction based on Normal and Phi.
		 * This function assumes that the constraint is update-to-date.
		 */
		ECollisionConstraintDirection GetConstraintDirection(const FReal Dt) const;

		/**
		 * @brief Called before SetSolverResults() to reset accumulators
		*/
		void ResetSolverResults()
		{
			ResetSavedManifoldPoints();
		}

		/**
		 * @brief Time of impact from CCD sweep test if CCD is activate.Otherwise undefined.
		*/
		FReal GetCCDTimeOfImpact() const { return CCDTimeOfImpact; }

		/**
		 * @brief Set the CCD TOI from the collision detection sweep
		*/
		void SetCCDTimeOfImpact(const FReal TOI) { CCDTimeOfImpact = TOI; }

		/**
		 * \brief Store the results of CCD contact resolution, if active
		 */
		void SetCCDResults(const FVec3& InNetImpulse)
		{
			AccumulatedImpulse += InNetImpulse;
		}

		/**
		 * @brief Store the data from the solver that is retained between ticks for the specified manifold point or used by dependent systems (plasticity, breaking, etc.)
		*/
		void SetSolverResults(
			const int32 ManifoldPointIndex, 
			const FVec3& NetPushOut, 
			const FVec3& NetImpulse, 
			const FReal StaticFrictionRatio,
			const FReal Dt)
		{
			FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];

			ManifoldPoint.NetPushOut = NetPushOut;
			ManifoldPoint.NetImpulse = NetImpulse;
			ManifoldPoint.Flags.bInsideStaticFrictionCone = FMath::IsNearlyEqual(StaticFrictionRatio, FReal(1));

			AccumulatedImpulse += NetImpulse + (NetPushOut / Dt);

			// Save contact data for friction
			// NOTE: we do this even for points that did not apply PushOut or Impulse so that
			// we get previous contact data for initial contacts (sometimes). Otherwise we
			// end up having to estimate the previous contact from velocities
			if (!SavedManifoldPoints.IsFull())
			{
				const int32 SavedIndex = SavedManifoldPoints.Add();
				FSavedManifoldPoint& SavedManifoldPoint = SavedManifoldPoints[SavedIndex];

				if (StaticFrictionRatio >= FReal(1.0f - UE_KINDA_SMALL_NUMBER))
				{
					// StaticFrictionRatio ~= 1: Static friction held - we keep the same contacts points as-is for use next frame
					SavedManifoldPoint.ShapeContactPoints[0] = ManifoldPoint.ShapeAnchorPoints[0];
					SavedManifoldPoint.ShapeContactPoints[1] = ManifoldPoint.ShapeAnchorPoints[1];
				}
				else if (StaticFrictionRatio < FReal(UE_KINDA_SMALL_NUMBER))
				{
					// StaticFrictionRatio ~= 0: No friction (or no contact) - discard the friction anchors
					SavedManifoldPoint.ShapeContactPoints[0] = ManifoldPoint.ContactPoint.ShapeContactPoints[0];
					SavedManifoldPoint.ShapeContactPoints[1] = ManifoldPoint.ContactPoint.ShapeContactPoints[1];
				}
				else
				{
					// 0 < StaticFrictionRatio < 1: We exceeded the friction cone. Slide the friction anchor 
					// toward the last-detected contact position so that it sits at the edge of the friction cone.
					SavedManifoldPoint.ShapeContactPoints[0] = FVec3::Lerp(ManifoldPoint.ContactPoint.ShapeContactPoints[0], ManifoldPoint.ShapeAnchorPoints[0], StaticFrictionRatio);
					SavedManifoldPoint.ShapeContactPoints[1] = FVec3::Lerp(ManifoldPoint.ContactPoint.ShapeContactPoints[1], ManifoldPoint.ShapeAnchorPoints[1], StaticFrictionRatio);
				}
			}
		}

	public:
		const FPBDCollisionConstraintHandle* GetConstraintHandle() const { return this; }
		FPBDCollisionConstraintHandle* GetConstraintHandle() { return this; }

	protected:

		FPBDCollisionConstraint(
			FGeometryParticleHandle* Particle0,
			const FImplicitObject* Implicit0,
			const FPerShapeData* Shape0,
			const FBVHParticles* Simplicial0,
			FGeometryParticleHandle* Particle1,
			const FImplicitObject* Implicit1,
			const FPerShapeData* Shape1,
			const FBVHParticles* Simplicial1);

		// Set all the data not initialized in the constructor
		void Setup(
			const ECollisionCCDType InCCDType,
			const EContactShapesType InShapesType,
			const FRigidTransform3& InImplicitTransform0,
			const FRigidTransform3& InImplicitTransform1,
			const FReal InCullDistance,
			const bool bInUseManifold);

		// Access to the data used by the container
		const FPBDCollisionConstraintContainerCookie& GetContainerCookie() const { return ContainerCookie; }
		FPBDCollisionConstraintContainerCookie& GetContainerCookie() { return ContainerCookie; }

		bool AreMatchingContactPoints(const FContactPoint& A, const FContactPoint& B, FReal& OutScore) const;
		int32 FindManifoldPoint(const FContactPoint& ContactPoint) const;

		inline void InitManifoldPoint(const int32 ManifoldPointIndex)
		{
			FManifoldPoint& ManifoldPoint = ManifoldPoints[ManifoldPointIndex];
			ManifoldPoint.InitialShapeContactPoints[0] = ManifoldPoint.ContactPoint.ShapeContactPoints[0];
			ManifoldPoint.InitialShapeContactPoints[1] = ManifoldPoint.ContactPoint.ShapeContactPoints[1];
			ManifoldPoint.TargetPhi = FReal(0);
			ManifoldPoint.Flags.Reset();
			ManifoldPoint.NetPushOut = FVec3(0);
			ManifoldPoint.NetImpulse = FVec3(0);
		}

		inline int32 AddManifoldPoint(const FContactPoint& ContactPoint)
		{
			int32 ManifoldPointIndex = ManifoldPoints.Add();	// Note: no initialization (see TCArray)
			ManifoldPoints[ManifoldPointIndex].ContactPoint = ContactPoint;
			InitManifoldPoint(ManifoldPointIndex);
			return ManifoldPointIndex;
		}

		// Update the store Phi for the manifold point based on current world-space shape transforms
		// @todo(chaos): Only intended for use by the legacy solvers - remove it
		void UpdateManifoldPointPhi(const int32 ManifoldPointIndex);

		void InitMarginsAndTolerances(const EImplicitObjectType ImplicitType0, const EImplicitObjectType ImplicitType1, const FReal Margin0, const FReal Margin1);

		void InitCCDThreshold();

	private:
		FReal CalculateSavedManifoldPointScore(const FSavedManifoldPoint& SavedManifoldPoint, const FManifoldPoint& ManifoldPoint, const FReal DistanceToleranceSq) const;

		// Local-space transforms of the shape (relative to particle)
		FRigidTransform3 ImplicitTransform[2];
		
		FGeometryParticleHandle* Particle[2];
		const FImplicitObject* Implicit[2];
		const FPerShapeData* Shape[2];
		const FBVHParticles* Simplicial[2];

		FPBDCollisionConstraintMaterial Material;
		FReal Stiffness;

	public:
		FVec3 AccumulatedImpulse;					// @todo(chaos): we need to accumulate angular impulse separately

	private:
		// Value in range [0,1] used to interpolate P between [X,P] that we will rollback to when solving at time of impact.
		FReal CCDTimeOfImpact;

		// The penetration at which CCD contacts get processed following a CCD sweep test.
		// NOTE: also see GeometryParticle CCDAxisThreshold, which is used to determine when to enable sweeping during collision detection.
		// Calculated from particle pair properties when constraint is created.
		FReal CCDEnablePenetration;

		// The penetration we leave behind when rolling back to a CCD time of impact. Should be less than or equal to CCDEnablePenetration.
		// Calculated from particle pair properties when constraint is created.
		FReal CCDTargetPenetration;

		FPBDCollisionConstraintContainerCookie ContainerCookie;
		EContactShapesType ShapesType;
		ECollisionCCDType CCDType;

		// The shape transforms at the current particle transforms
		FRigidTransform3 ShapeWorldTransform0;
		FRigidTransform3 ShapeWorldTransform1;

		// The separation distance at which we don't track contacts
		FReal CullDistance;

		// The margins to use during collision detection. We don't always use the margins on the shapes directly.
		// E.g., we use the smallest non-zero margin for 2 convex shapes. See InitMarginsAndTolerances
		FReal CollisionMargins[2];

		// The collision tolerance is used to determine whether a new contact matches an old on. It is derived from the
		// margins of the two shapes, as well as their types
		FReal CollisionTolerance;

		// The index into ManifoldPoints of the point with the lowest Phi
		int32 ClosestManifoldPointIndex;

		// Used by manifold point injection to see how many points were in the manifold before UpdateAndTryRestore
		int32 ExpectedNumManifoldPoints;

		union FFlags
		{
			FFlags() : Bits(0) {}
			struct
			{
				uint32 bDisabled : 1;
				uint32 bUseManifold : 1;
				uint32 bUseIncrementalManifold : 1;
				uint32 bWasManifoldRestored : 1;
				uint32 bIsQuadratic0 : 1;
				uint32 bIsQuadratic1 : 1;
				uint32 bIsProbeUnmodified : 1;  // Is this constraint a probe pre-contact-modification
				uint32 bIsProbe : 1;            // Is this constraint currently a probe
			};
			uint32 Bits;
		} Flags;

		// Relative transform the last time we ran the narrow phase
		// Used to detect when the bodies have moved too far to reues the manifold
		FVec3 LastShapeWorldPositionDelta;
		FRotation3 LastShapeWorldRotationDelta;

		// These are only needed here while we still have the legacy solvers (not QuasiPBD)
		FSolverBody* SolverBodies[2];

		// Stores the index into the solver container for this constraint
		int32 SolverIndex;

		// Simplex data from the last call to GJK, used to warm-start GJK
		FGJKSimplexData GJKWarmStartData;

		TCArray<FSavedManifoldPoint, MaxManifoldPoints> SavedManifoldPoints;
		TCArray<FManifoldPoint, MaxManifoldPoints> ManifoldPoints;
	};


	UE_DEPRECATED(4.27, "FCollisionConstraintBase has been removed and folded into FPBDCollisionConstraint. Use FPBDCollisionConstraint")
	typedef FPBDCollisionConstraint FCollisionConstraintBase;

	UE_DEPRECATED(4.27, "FRigidBodySweptPointContactConstraint has been removed and folded into FPBDCollisionConstraint. Use FPBDCollisionConstraint")
	typedef FPBDCollisionConstraint FRigidBodySweptPointContactConstraint;
}
