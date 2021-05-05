// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Transform.h"
#include "Chaos/Real.h"

using namespace Chaos;

PMatrix<Chaos::FReal, 4, 4> operator*(const TRigidTransform<Chaos::FReal, 3>& Transform, const PMatrix<Chaos::FReal, 4, 4>& Matrix)
{
	// LWC_TODO: Perf pessimization
	return Transform.ToMatrixNoScale() * static_cast<const FMatrix&>(Matrix);
}

PMatrix<Chaos::FReal, 4, 4> operator*(const PMatrix<Chaos::FReal, 4, 4>& Matrix, const TRigidTransform<Chaos::FReal, 3>& Transform)
{
	// LWC_TODO: Perf pessimization
	return static_cast<const FMatrix&>(Matrix) * Transform.ToMatrixNoScale();
}