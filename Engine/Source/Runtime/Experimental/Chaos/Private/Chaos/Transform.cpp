// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Transform.h"
#include "Chaos/Real.h"

using namespace Chaos;

PMatrix<Chaos::FReal, 4, 4> TRigidTransform<Chaos::FReal, 3>::operator*(const PMatrix<Chaos::FReal, 4, 4>& Matrix) const
{
	// LWC_TODO: Perf pessimization
	return ToMatrixNoScale() * static_cast<const UE::Math::TMatrix<FReal>&>(Matrix);
}

