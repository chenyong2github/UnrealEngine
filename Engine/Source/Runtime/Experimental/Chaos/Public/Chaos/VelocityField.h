// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDParticles.h"
#include "Chaos/TriangleMesh.h"

namespace Chaos
{
	// Velocity field basic implementation
	// TODO: Add lift
	template<class T, int d>
	class CHAOS_API TVelocityField
	{
	public:
		// Construct an uninitialized field. Mesh, properties, and velocity will have to be set for this field to be valid.
		TVelocityField()
			: Range(-1)
		{
			SetCoefficients((T)0., (T)0.);
		}

		// Construct a uniform field.
		TVelocityField(
			const TTriangleMesh<T>& TriangleMesh,
			const TVector<T, d>& InVelocity,
			const T InDragCoefficient = (T)0.5,
			const T InLiftCoefficient = (T)0.1,
			const T InFluidDensity = (T)1.225e-6)
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
		TVelocityField(
			const TTriangleMesh<T>& TriangleMesh,
			TFunction<TVector<T, d>(const TVector<T, d>&)> InGetVelocity,
			const T InDragCoefficient = (T)0.5,
			const T InLiftCoefficient = (T)0.1,
			const T InFluidDensity = (T)1.225e-6)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity((T)0.)
			, GetVelocity(InGetVelocity)
			, Range(TriangleMesh.GetVertexRange())
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetCoefficients(InDragCoefficient, InLiftCoefficient);
			SetFluidDensity(InFluidDensity);
		}

		virtual ~TVelocityField() {}

		void UpdateForces(const TPBDParticles<T, d>& InParticles, const T /*Dt*/);

		inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
		{
			checkSlow(Index >= Range[0] && Index <= Range[1]);  // The index should always match the original triangle mesh range

			const TArray<int32>& ElementIndices = PointToTriangleMap[Index];
			for (const int32 ElementIndex : ElementIndices)
			{
				InParticles.F(Index) += Forces[ElementIndex];
			}
		}

		void SetFluidDensity(const T InFluidDensity)
		{
			QuarterRho = (T)0.25 * InFluidDensity;
		}

		void SetCoefficients(const T InDragCoefficient, const T InLiftCoefficient)
		{
			Cd = InDragCoefficient;
			Cl = InLiftCoefficient;
		}

		bool IsActive() const
		{
			return Cd > (T)0. || Cl > (T)0.;
		}

		void SetGeometry(const TTriangleMesh<T>* TriangleMesh)
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

		void SetVelocity(const TVector<T, d>& InVelocity)
		{
			Velocity = InVelocity;
			GetVelocity = TFunction<TVector<T, d>(const TVector<T, d>&)>();
		}

		void SetVelocity(TFunction<TVector<T, d>(const TVector<T, d>&)> InGetVelocity)
		{
			GetVelocity = InGetVelocity;
		}

		const TConstArrayView<TVector<int32, 3>>& GetElements() const { return Elements; }
		TConstArrayView<TVector<T, d>> GetForces() const { return TConstArrayView<TVector<T, d>>(Forces); }

	private:
		inline void UpdateField(const TPBDParticles<T, d>& InParticles, int32 ElementIndex, const TVector<T, d>& InVelocity)
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];

			// Calculate the normal and the area of the surface exposed to the flow
			TVector<T, d> N = TVector<T, d>::CrossProduct(
				InParticles.X(Element[1]) - InParticles.X(Element[0]),
				InParticles.X(Element[2]) - InParticles.X(Element[0]));
			const T DoubleArea = N.SafeNormalize();

			// Calculate the direction and the relative velocity of the triangle to the flow
			const TVector<T, d>& SurfaceVelocity = (T)(1. / 3.) * (
				InParticles.V(Element[0]) +
				InParticles.V(Element[1]) +
				InParticles.V(Element[2]));
			const TVector<T, d> V = InVelocity - SurfaceVelocity;

			// Set the aerodynamic forces
			const T VDotN = TVector<T, d>::DotProduct(V, N);
			const T VSquare = TVector<T, d>::DotProduct(V, V);

			Forces[ElementIndex] = QuarterRho * DoubleArea * (VDotN >= (T)0. ?  // The flow can hit either side of the triangle, so the normal might need to be reversed
				(Cd - Cl) * VDotN * V + Cl * VSquare * N :
				(Cl - Cd) * VDotN * V - Cl * VSquare * N);
		}

	private:
		TConstArrayView<TArray<int32>> PointToTriangleMap;
		TConstArrayView<TVector<int32, 3>> Elements;
		TVector<T, d> Velocity;
		TFunction<TVector<T, d>(const TVector<T, d>&)> GetVelocity;
		TArray<TVector<T, 3>> Forces;
		float QuarterRho;
		float Cd;
		float Cl;
		TVector<int32, 2> Range;  // TODO: Remove? It is used by the check only
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
