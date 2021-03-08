// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Transform.h"

using namespace Chaos;

PMatrix<Chaos::FReal, 4, 4> operator*(const TRigidTransform<Chaos::FReal, 3>& Transform, const PMatrix<Chaos::FReal, 4, 4>& Matrix)
{
	return Transform.ToMatrixNoScale() * static_cast<const FMatrix&>(Matrix);
}

PMatrix<Chaos::FReal, 4, 4> operator*(const PMatrix<Chaos::FReal, 4, 4>& Matrix, const TRigidTransform<Chaos::FReal, 3>& Transform)
{
	return static_cast<const FMatrix&>(Matrix) * Transform.ToMatrixNoScale();
}