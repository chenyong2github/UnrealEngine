// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDParticles.h"
#include "Chaos/TriangleMesh.h"

namespace Chaos
{
	// Velocity field basic implementation
	// TODO: Add lift
	class CHAOS_API FVelocityField
	{
	public:
		static constexpr FReal DefaultDragCoefficient = (FReal)0.5;
		static constexpr FReal DefaultLiftCoefficient = (FReal)0.1;
		static constexpr FReal DefaultFluidDensity = (FReal)1.225e-6;

		// Construct an uninitialized field. Mesh, properties, and velocity will have to be set for this field to be valid.
		FVelocityField()
			: Range(-1)
		{
			SetCoefficients((FReal)0., (FReal)0.);
		}

		// Construct a uniform field.
		FVelocityField(
			const FTriangleMesh& TriangleMesh,
			const FVec3& InVelocity,
			const FReal InDragCoefficient = DefaultDragCoefficient,
			const FReal InLiftCoefficient = DefaultLiftCoefficient,
			const FReal InFluidDensity = DefaultFluidDensity)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity(InVelocity)
			, Range(TriangleMesh.GetVertexRange())
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetCoefficients(InDragCoefficient, InLiftCoefficient);
			SetFluidDensity(InFluidDensity);
		}

		// Construct a vector field.
		FVelocityField(
			const FTriangleMesh& TriangleMesh,
			TFunction<FVec3(const FVec3&)> InGetVelocity,
			const FReal InDragCoefficient = DefaultDragCoefficient,
			const FReal InLiftCoefficient = DefaultLiftCoefficient,
			const FReal InFluidDensity = DefaultFluidDensity)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity((FReal)0.)
			, GetVelocity(InGetVelocity)
			, Range(TriangleMesh.GetVertexRange())
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetCoefficients(InDragCoefficient, InLiftCoefficient);
			SetFluidDensity(InFluidDensity);
		}

		virtual ~FVelocityField() {}

		void UpdateForces(const FPBDParticles& InParticles, const FReal /*Dt*/);

		inline void Apply(FPBDParticles& InParticles, const FReal Dt, const int32 Index) const
		{
			checkSlow(Index >= Range[0] && Index <= Range[1]);  // The index should always match the original triangle mesh range

			const TArray<int32>& ElementIndices = PointToTriangleMap[Index];
			for (const int32 ElementIndex : ElementIndices)
			{
				InParticles.F(Index) += Forces[ElementIndex];
			}
		}

		void SetFluidDensity(const FReal InFluidDensity)
		{
			QuarterRho = (FReal)0.25 * InFluidDensity;
		}

		void SetCoefficients(const FReal InDragCoefficient, const FReal InLiftCoefficient)
		{
			Cd = InDragCoefficient;
			Cl = InLiftCoefficient;
		}

		bool IsActive() const
		{
			return Cd > (FReal)0. || Cl > (FReal)0.;
		}

		void SetGeometry(const FTriangleMesh* TriangleMesh)
		{
			if (TriangleMesh)
			{
				PointToTriangleMap = TriangleMesh->GetPointToTriangleMap();
				Elements = TriangleMesh->GetElements();
				Range = TriangleMesh->GetVertexRange();
				Forces.SetNumUninitialized(Elements.Num());
			}
			else
			{
				PointToTriangleMap = TArrayView<TArray<int32>>();
				Elements = TArrayView<TVector<int32, 3>>();
				Range = TVector<int32, 2>(-1);
				Forces.Reset();
			}
		}

		void SetVelocity(const FVec3& InVelocity)
		{
			Velocity = InVelocity;
			GetVelocity = TFunction<FVec3(const FVec3&)>();
		}

		void SetVelocity(TFunction<FVec3(const FVec3&)> InGetVelocity)
		{
			GetVelocity = InGetVelocity;
		}

		const TConstArrayView<TVector<int32, 3>>& GetElements() const { return Elements; }
		TConstArrayView<FVec3> GetForces() const { return TConstArrayView<FVec3>(Forces); }

	private:
		inline void UpdateField(const FPBDParticles& InParticles, int32 ElementIndex, const FVec3& InVelocity)
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
		FVec3 Velocity;
		TFunction<FVec3(const FVec3&)> GetVelocity;
		TArray<FVec3> Forces;
		FReal QuarterRho;
		FReal Cd;
		FReal Cl;
		TVec2<int32> Range;  // TODO: Remove? It is used by the check only
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
