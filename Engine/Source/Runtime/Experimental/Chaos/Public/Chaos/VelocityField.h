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
		// Construct an uninitialized field. Mesh, properties, and velocity will have to be set for this field to be valid.
		TVelocityField()
			: Range(-1)
		{}

		// Construct a uniform field.
		TVelocityField(
			const TTriangleMesh<T>& TriangleMesh,
			const TVector<T, d>& InVelocity,
			const T InDragCoefficient = (T)0.5,
			const T InFluidDensity = (T)1.225e-6)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity(InVelocity)
			, Range(TriangleMesh.GetVertexRange())
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetProperties(InDragCoefficient, InFluidDensity);
		}

		// Construct a vector field.
		TVelocityField(
			const TTriangleMesh<T>& TriangleMesh,
			TFunction<TVector<T, d>(const TVector<T, d>&)> InGetVelocity,
			const T InDragCoefficient = (T)0.5,
			const T InFluidDensity = (T)1.225e-6)
			: PointToTriangleMap(TriangleMesh.GetPointToTriangleMap())
			, Elements(TriangleMesh.GetElements())
			, Velocity((T)0.)
			, GetVelocity(InGetVelocity)
			, Range(TriangleMesh.GetVertexRange())
		{
			Forces.SetNumUninitialized(Elements.Num());
			SetProperties(InDragCoefficient, InFluidDensity);
		}

		virtual ~TVelocityField() {}

		inline void UpdateForces(const TPBDParticles<T, d>& InParticles, const T /*Dt*/) const
		{
			if (!GetVelocity)
			{
				for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
				{
					UpdateField(InParticles, ElementIndex, Velocity);
				}
			}
			else
			{
				for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
				{
					const TVector<int32, 3>& Element = Elements[ElementIndex];

					// Get the triangle's position
					const TVector<T, d>& SurfacePosition = (T)(1. / 3.) * (
						InParticles.X(Element[0]) +
						InParticles.X(Element[1]) +
						InParticles.X(Element[2]));

					UpdateField(InParticles, ElementIndex, GetVelocity(SurfacePosition));
				}
			}
		}

		inline void Apply(TPBDParticles<T, d>& InParticles, const T Dt, const int32 Index) const
		{
			check(Index >= Range[0] && Index <= Range[1]);  // The index should always match the original triangle mesh range

			const TArray<int32>& ElementIndices = PointToTriangleMap[Index];
			for (const int32 ElementIndex : ElementIndices)
			{
				InParticles.F(Index) += Forces[ElementIndex];
			}
		}

		void SetProperties(const T InDragCoefficient, const T InFluidDensity)
		{
			DragCoefficient = InDragCoefficient;
			FluidDensity = InFluidDensity;
			QuarterRhoDragCoefficient = (T)0.25 * FluidDensity * DragCoefficient;
		}

		void SetDragCoefficient(const T InDragCoefficient) { SetProperties(InDragCoefficient, FluidDensity); }
		void SetFluidDensity(const T InFluidDensity) { SetProperties(DragCoefficient, InFluidDensity); }

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
		const TArray<TVector<T, d>>& GetForces() const { return Forces; }

	private:
		inline void UpdateField(const TPBDParticles<T, d>& InParticles, int32 ElementIndex, const TVector<T, d>& InVelocity) const
		{
			const TVector<int32, 3>& Element = Elements[ElementIndex];

			// Calculate direction and the relative velocity of the triangle to the flow
			const TVector<T, d>& SurfaceVelocity = (T)(1. / 3.) * (
				InParticles.V(Element[0]) +
				InParticles.V(Element[1]) +
				InParticles.V(Element[2]));
			TVector<T, d> Direction = InVelocity - SurfaceVelocity;
			const T RelVelocity = Direction.SafeNormalize();

			// Calculate the cross sectional area of the surface exposed to the flow
			TVector<T, d> Normal = TVector<T, d>::CrossProduct(
				InParticles.X(Element[1]) - InParticles.X(Element[0]),
				InParticles.X(Element[2]) - InParticles.X(Element[0]));
			const T DoubleArea = Normal.SafeNormalize();
			const T DoubleCrossSectionalArea = DoubleArea * FMath::Abs(TVector<T, d>::DotProduct(Direction, Normal));

			// Calculate the drag force
			const T Drag = QuarterRhoDragCoefficient * FMath::Square(RelVelocity) * DoubleCrossSectionalArea;
			Forces[ElementIndex] = Direction * Drag;
		}

	private:
		TConstArrayView<TArray<int32>> PointToTriangleMap;
		TConstArrayView<TVector<int32, 3>> Elements;
		TVector<T, d> Velocity;
		TFunction<TVector<T, d>(const TVector<T, d>&)> GetVelocity;
		mutable TArray<TVector<T, 3>> Forces;
		float QuarterRhoDragCoefficient;
		float DragCoefficient;
		float FluidDensity;
		TVector<int32, 2> Range;  // TODO: Remove? It is used by the check only
	};
}
