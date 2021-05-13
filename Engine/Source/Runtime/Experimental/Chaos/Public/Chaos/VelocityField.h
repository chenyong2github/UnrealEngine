// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDParticles.h"
#include "Chaos/TriangleMesh.h"

namespace Chaos
{
	// Velocity field basic implementation
	// TODO: Add lift
	class CHAOS_API FVelocityField final
	{
	public:
		static constexpr FReal DefaultDragCoefficient = (FReal)0.5;
		static constexpr FReal DefaultLiftCoefficient = (FReal)0.1;
		static constexpr FReal DefaultFluidDensity = (FReal)1.225e-6;

		// Construct an uninitialized field. Mesh, properties, and velocity will have to be set for this field to be valid.
		FVelocityField()
			: Offset(INDEX_NONE)
			, NumParticles(0)
		{
			SetProperties(FVec2::ZeroVector, FVec2::ZeroVector, (FReal)0.);
		}

		// Construct a uniform field.
		UE_DEPRECATED(4.27, "Use Chaos fields instead.")
		FVelocityField(
			const FTriangleMesh& TriangleMesh,
			const FVec3& InVelocity,
			const FReal InDragCoefficient = DefaultDragCoefficient,
			const FReal InLiftCoefficient = DefaultLiftCoefficient,
			const FReal InFluidDensity = DefaultFluidDensity)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity(InVelocity)
			, Offset(TriangleMesh.GetVertexRange()[0])
			, NumParticles(TriangleMesh.GetVertexRange()[1] - TriangleMesh.GetVertexRange()[0] + 1)
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetProperties(InDragCoefficient, InLiftCoefficient, InFluidDensity);
		}

		// Construct a vector field.
		UE_DEPRECATED(4.27, "Use Chaos fields instead.")
		FVelocityField(
			const FTriangleMesh& TriangleMesh,
			TFunction<FVec3(const FVec3&)> GetVelocity,
			const FReal InDragCoefficient = DefaultDragCoefficient,
			const FReal InLiftCoefficient = DefaultLiftCoefficient,
			const FReal InFluidDensity = DefaultFluidDensity)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity(GetVelocity(FVec3::ZeroVector))
			, Offset(TriangleMesh.GetVertexRange()[0])
			, NumParticles(TriangleMesh.GetVertexRange()[1] - TriangleMesh.GetVertexRange()[0] + 1)
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetProperties(InDragCoefficient, InLiftCoefficient, InFluidDensity);
		}

		~FVelocityField() {}

		void UpdateForces(const FPBDParticles& InParticles, const FReal /*Dt*/);

		inline void Apply(FPBDParticles& InParticles, const FReal Dt, const int32 Index) const
		{
			checkSlow(Index >= Offset && Index < Offset + NumParticles);  // The index should always match the original triangle mesh range

			const TArray<int32>& ElementIndices = PointToTriangleMap[Index];
			for (const int32 ElementIndex : ElementIndices)
			{
				InParticles.F(Index) += Forces[ElementIndex];
			}
		}

		UE_DEPRECATED(4.27, "Use SetProperties instead.")
		void SetFluidDensity(const FReal InFluidDensity)
		{
			QuarterRho = (FReal)0.25 * InFluidDensity;
		}

		UE_DEPRECATED(4.27, "Use SetProperties instead.")
		void SetCoefficients(const FReal InDragCoefficient, const FReal InLiftCoefficient)
		{
			SetProperties(FVec2(InDragCoefficient), FVec2(InLiftCoefficient), (FReal)4. * QuarterRho);
		}

		void SetProperties(const FVec2& Drag, const FVec2& Lift, const FReal FluidDensity)
		{
			constexpr FReal OneQuarter = (FReal)0.25;
			QuarterRho = FluidDensity * OneQuarter;

			constexpr FReal MinCoefficient = (FReal)0.;
			constexpr FReal MaxCoefficient = (FReal)10.;
			DragBase = FMath::Clamp(Drag[0], MinCoefficient, MaxCoefficient);
			DragRange = FMath::Clamp(Drag[1], MinCoefficient, MaxCoefficient) - DragBase;
			LiftBase = FMath::Clamp(Lift[0], MinCoefficient, MaxCoefficient);
			LiftRange = FMath::Clamp(Lift[1], MinCoefficient, MaxCoefficient) - LiftBase;
		}

		bool IsActive() const 
		{
			return (DragBase > (FReal)0. || DragRange != (FReal)0.) || (LiftBase > (FReal)0. || LiftRange != (FReal)0.);  // Note: range can be a negative value (although not when base is zero)
		}

		void SetGeometry(const FTriangleMesh* TriangleMesh, const TConstArrayView<FRealSingle>& DragMultipliers, const TConstArrayView<FRealSingle>& LiftMultipliers);

		void SetVelocity(const FVec3& InVelocity) { Velocity = InVelocity; }

		UE_DEPRECATED(4.27, "Use SetVeloccity(const FVec3&) instead.")
		void SetVelocity(TFunction<FVec3(const FVec3&)> GetVelocity)  { Velocity = GetVelocity(FVec3::ZeroVector); }

		const TConstArrayView<TVector<int32, 3>>& GetElements() const { return Elements; }
		TConstArrayView<FVec3> GetForces() const { return TConstArrayView<FVec3>(Forces); }

	private:
		void UpdateField(const FPBDParticles& InParticles, int32 ElementIndex, const FVec3& InVelocity, const FReal Cd, const FReal Cl)
		{
			const TVec3<int32>& Element = Elements[ElementIndex];

			// Calculate the normal and the area of the surface exposed to the flow
			FVec3 N = FVec3::CrossProduct(
				InParticles.X(Element[1]) - InParticles.X(Element[0]),
				InParticles.X(Element[2]) - InParticles.X(Element[0]));
			const FReal DoubleArea = N.SafeNormalize();

			// Calculate the direction and the relative velocity of the triangle to the flow
			const FVec3& SurfaceVelocity = (FReal)(1. / 3.) * (
				InParticles.V(Element[0]) +
				InParticles.V(Element[1]) +
				InParticles.V(Element[2]));
			const FVec3 V = InVelocity - SurfaceVelocity;

			// Set the aerodynamic forces
			const FReal VDotN = FVec3::DotProduct(V, N);
			const FReal VSquare = FVec3::DotProduct(V, V);

			Forces[ElementIndex] = QuarterRho * DoubleArea * (VDotN >= (FReal)0. ?  // The flow can hit either side of the triangle, so the normal might need to be reversed
				(Cd - Cl) * VDotN * V + Cl * VSquare * N :
				(Cl - Cd) * VDotN * V - Cl * VSquare * N);
		}

	private:
		TConstArrayView<TArray<int32>> PointToTriangleMap;
		TConstArrayView<TVec3<int32>> Elements;
		TArray<FVec3> Forces;
		TArray<FVec2> Multipliers;
		FVec3 Velocity;
		FReal DragBase;
		FReal DragRange;
		FReal LiftBase;
		FReal LiftRange;
		FReal QuarterRho;
		int32 Offset;
		int32 NumParticles;
	};
}

// Support ISPC enable/disable in non-shipping builds
#if !INTEL_ISPC
const bool bChaos_VelocityField_ISPC_Enabled = false;
#elif UE_BUILD_SHIPPING
const bool bChaos_VelocityField_ISPC_Enabled = true;
#else
extern CHAOS_API bool bChaos_VelocityField_ISPC_Enabled;
#endif
