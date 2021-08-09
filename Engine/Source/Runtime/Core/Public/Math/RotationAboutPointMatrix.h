// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/RotationTranslationMatrix.h"

// LWC_TODO: FRotator -> TRotator<T>

namespace UE {
namespace Math {

/** Rotates about an Origin point. */
template<typename T>	
struct TRotationAboutPointMatrix
	: public TRotationTranslationMatrix<T>
{
public:
	using TRotationTranslationMatrix<T>::M;

	/**
	 * Constructor.
	 *
	 * @param Rot rotation
	 * @param Origin about which to rotate.
	 */
	TRotationAboutPointMatrix(const FRotator& Rot, const TVector<T>& Origin);

	/** Matrix factory. Return an TMatrix<T> so we don't have type conversion issues in expressions. */
	static TMatrix<T> Make(const FRotator& Rot, const TVector<T>& Origin)
	{
		return TRotationAboutPointMatrix(Rot, Origin);
	}

	/** Matrix factory. Return an TMatrix<T> so we don't have type conversion issues in expressions. */
	static TMatrix<T> Make(const FQuat& Rot, const TVector<T>& Origin)
	{
		return TRotationAboutPointMatrix(Rot.Rotator(), Origin);
	}
};

template<typename T>
FORCEINLINE TRotationAboutPointMatrix<T>::TRotationAboutPointMatrix(const FRotator& Rot, const TVector<T>& Origin)
	: TRotationTranslationMatrix<T>(Rot, Origin)
{
	// FRotationTranslationMatrix generates R * T.
	// We need -T * R * T, so prepend that translation:
	TVector<T> XAxis(M[0][0], M[1][0], M[2][0]);
	TVector<T> YAxis(M[0][1], M[1][1], M[2][1]);
	TVector<T> ZAxis(M[0][2], M[1][2], M[2][2]);

	M[3][0]	-= XAxis | Origin;
	M[3][1]	-= YAxis | Origin;
	M[3][2]	-= ZAxis | Origin;
}

} // namespace Math
} // namespace UE

DECLARE_LWC_TYPE(RotationAboutPointMatrix, 44);