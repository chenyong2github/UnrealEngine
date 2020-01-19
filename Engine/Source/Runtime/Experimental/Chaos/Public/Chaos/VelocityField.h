// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/DynamicParticles.h"
#include "Chaos/TriangleMesh.h"

namespace Chaos
{
	// Velocity field basic implementation
	// TODO: Add lift
	template<class T, int d>
	class TVelocityField
	{
	public:
		TVelocityField()
			: QuarterRhoDragCoefficient((T)0.), bIsUniform(true) {}

		TVelocityField(
			const TTriangleMesh<T>& TriangleMesh,
			TFunction<TVector<T, d>(const TVector<T, d>&)> InGetVelocity,
			const bool bInIsUniform,
			const T DragCoefficient = (T)0.5,
			const T FluidDensity = (T)1.225e-6)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())  // TODO(Kriss.Gossart): Replace map lookup by safer array lookup
			, Elements(TriangleMesh.GetElements())
			, GetVelocity(InGetVelocity)
			, bIsUniform(bInIsUniform)
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetProperties(DragCoefficient, FluidDensity);
		}

		virtual ~TVelocityField() {}

		inline void UpdateForces(const TPBDParticles<T, d>& InParticles, const T Dt) const
		{
			if (GetVelocity)
			{
				if (bIsUniform)
				{
					const TVector<T, d> Velocity = GetVelocity(TVector<T, d>(0.));
					for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
					{
						UpdateUniformField(InParticles, Dt, ElementIndex, Velocity);
					}
				}
				else
				{
					for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
					{
						UpdateVectorField(InParticles, Dt, ElementIndex);
					}
				}
			}
		}

		inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
		{
			if (const TArray<int32>* ElementIndices = PointToTriangleMap.Find(Index))  // TODO(Kriss.Gossart): Replace map lookup by faster array lookup
			{
				for (const int32 ElementIndex : *ElementIndices)
				{
					InParticles.F(Index) += Forces[ElementIndex];
				}
			}
		}

		void SetProperties(const T DragCoefficient, const T FluidDensity)
		{
			QuarterRhoDragCoefficient = (T)0.25 * FluidDensity * DragCoefficient;
		}

		const TArray<TVector<int32, 3>>& GetElements() const { return Elements; }
		const TArray<TVector<T, d>>& GetForces() const { return Forces; }

	private:
		inline void UpdateUniformField(const TPBDParticles<T, d>& InParticles, const T Dt, int32 ElementIndex, const TVector<T, d>& Velocity) const
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];

			// Calculate direction and the relative velocity of the triangle to the flow
			const TVector<T, d>& SurfaceVelocity = (T)(1. / 3.) * (
				InParticles.V(Element[0]) + 
				InParticles.V(Element[1]) + 
				InParticles.V(Element[2]));
			TVector<T, d> Direction = Velocity - SurfaceVelocity;
			const T RelVelocity = Direction.SafeNormalize();

			// Calculate the cross sectional area of the surface exposed to the flow
			TVector<float, 3> Normal = TVector<float, 3>::CrossProduct(
				InParticles.X(Element[1]) - InParticles.X(Element[0]),
				InParticles.X(Element[2]) - InParticles.X(Element[0]));
			const T DoubleArea = Normal.SafeNormalize();
			const T DoubleCrossSectionalArea = DoubleArea * FMath::Abs(TVector<float, 3>::DotProduct(Direction, Normal));

			// Calculate the drag force
			const T Drag = QuarterRhoDragCoefficient * FMath::Square(RelVelocity) * DoubleCrossSectionalArea;
			Forces[ElementIndex] = Direction * Drag;
		}

		inline void UpdateVectorField(const TPBDParticles<T, d>& InParticles, const T Dt, int32 ElementIndex) const
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];

			// Get the triangle's position
			const TVector<T, d>& SurfacePosition = (T)(1. / 3.) * (
				InParticles.X(Element[0]) +
				InParticles.X(Element[1]) +
				InParticles.X(Element[2]));

			// Calculate direction and the relative velocity of the triangle to the flow at the field's sampled position
			const TVector<T, d>& SurfaceVelocity = (T)(1. / 3.) * (
				InParticles.V(Element[0]) + 
				InParticles.V(Element[1]) + 
				InParticles.V(Element[2]));
			TVector<T, d> Direction = GetVelocity(SurfacePosition) - SurfaceVelocity;
			const T RelVelocity = Direction.SafeNormalize();

			// Calculate the cross sectional area of the surface exposed to the flow
			 TVector<float, 3> Normal = TVector<float, 3>::CrossProduct(
				InParticles.X(Element[1]) - InParticles.X(Element[0]),
				InParticles.X(Element[2]) - InParticles.X(Element[0]));
			const T DoubleArea = Normal.SafeNormalize();
			const T DoubleCrossSectionalArea = DoubleArea * FMath::Abs(TVector<float, 3>::DotProduct(Direction, Normal));

			// Calculate the drag force
			const T Drag = QuarterRhoDragCoefficient * FMath::Square(RelVelocity) * DoubleCrossSectionalArea;
			Forces[ElementIndex] = Direction * Drag;
		}

	private:
		const TMap<int32, TArray<int32>>& PointToTriangleMap;
		const TArray<TVector<int32, 3>>& Elements;
		TFunction<TVector<T, d>(const TVector<T, d>&)> GetVelocity;
		mutable TArray<TVector<T, 3>> Forces;
		float QuarterRhoDragCoefficient;
		bool bIsUniform;
	};
}
