// Copyright Epic Games, Inc. All Rights Reserved.

// NOTE: This is a temporary place holder representing the large world coordinate version of FVector, and will be replaced shortly. You SHOULD NOT be using these types in your code!

#include "Math/VectorLWC.h"
#include "CoreMinimal.h"

CORE_API const FVector3f FVector3f::ZeroVector(0, 0, 0);
CORE_API const FVector3f FVector3f::OneVector(1, 1, 1);
CORE_API const FVector3f FVector3f::UpVector(0, 0, 1);
CORE_API const FVector3f FVector3f::DownVector(0, 0, -1);
CORE_API const FVector3f FVector3f::ForwardVector(1, 0, 0);
CORE_API const FVector3f FVector3f::BackwardVector(-1, 0, 0);
CORE_API const FVector3f FVector3f::RightVector(0, 1, 0);
CORE_API const FVector3f FVector3f::LeftVector(0, -1, 0);
CORE_API const FVector3f FVector3f::XAxisVector(1, 0, 0);
CORE_API const FVector3f FVector3f::YAxisVector(0, 1, 0);
CORE_API const FVector3f FVector3f::ZAxisVector(0, 0, 1);

CORE_API const FVector3d FVector3d::ZeroVector(0, 0, 0);
CORE_API const FVector3d FVector3d::OneVector(1, 1, 1);
CORE_API const FVector3d FVector3d::UpVector(0, 0, 1);
CORE_API const FVector3d FVector3d::DownVector(0, 0, -1);
CORE_API const FVector3d FVector3d::ForwardVector(1, 0, 0);
CORE_API const FVector3d FVector3d::BackwardVector(-1, 0, 0);
CORE_API const FVector3d FVector3d::RightVector(0, 1, 0);
CORE_API const FVector3d FVector3d::LeftVector(0, -1, 0);
CORE_API const FVector3d FVector3d::XAxisVector(1, 0, 0);
CORE_API const FVector3d FVector3d::YAxisVector(0, 1, 0);
CORE_API const FVector3d FVector3d::ZAxisVector(0, 0, 1);

template<>
UE::Core::TVector<float>::TVector(const FVector4& V)
	: X(V.X), Y(V.Y), Z(V.Z)
{
	DiagnosticCheckNaN();
}

template<>
UE::Core::TVector<double>::TVector(const FVector4& V)
	: X(V.X), Y(V.Y), Z(V.Z)
{
	DiagnosticCheckNaN();
}

template<>
FQuat UE::Core::TVector<float>::ToOrientationQuat() const
{
	// Essentially an optimized Vector->Rotator->Quat made possible by knowing Roll == 0, and avoiding radians->degrees->radians.
	// This is done to avoid adding any roll (which our API states as a constraint).
	const float YawRad = FMath::Atan2(Y, X);
	const float PitchRad = FMath::Atan2(Z, FMath::Sqrt(X * X + Y * Y));

	const float DIVIDE_BY_2 = 0.5f;
	float SP, SY;
	float CP, CY;

	FMath::SinCos(&SP, &CP, PitchRad * DIVIDE_BY_2);
	FMath::SinCos(&SY, &CY, YawRad * DIVIDE_BY_2);

	FQuat RotationQuat;
	RotationQuat.X = SP * SY;
	RotationQuat.Y = -SP * CY;
	RotationQuat.Z = CP * SY;
	RotationQuat.W = CP * CY;
	return RotationQuat;
}

template<>
FQuat UE::Core::TVector<double>::ToOrientationQuat() const
{
	// NOTE: Precision issues here. Identical to the float version, due to missing double support for FMath::SinCos.
	const float YawRad = (float)FMath::Atan2(Y, X);
	const float PitchRad = (float)FMath::Atan2(Z, FMath::Sqrt(X * X + Y * Y));

	const float DIVIDE_BY_2 = 0.5f;
	float SP, SY;
	float CP, CY;

	FMath::SinCos(&SP, &CP, PitchRad * DIVIDE_BY_2);
	FMath::SinCos(&SY, &CY, YawRad * DIVIDE_BY_2);

	FQuat RotationQuat;
	RotationQuat.X = SP * SY;
	RotationQuat.Y = -SP * CY;
	RotationQuat.Z = CP * SY;
	RotationQuat.W = CP * CY;
	return RotationQuat;
}

template<>
FRotator UE::Core::TVector<float>::ToOrientationRotator() const
{
	FRotator R;

	// Find yaw.
	R.Yaw = FMath::Atan2(Y, X) * (180.f / PI);

	// Find pitch.
	R.Pitch = FMath::Atan2(Z, FMath::Sqrt(X * X + Y * Y)) * (180.f / PI);

	// Find roll.
	R.Roll = 0;

#if ENABLE_NAN_DIAGNOSTIC || (DO_CHECK && !UE_BUILD_SHIPPING)
	if (R.ContainsNaN())
	{
		logOrEnsureNanError(TEXT("FVector::Rotation(): Rotator result %s contains NaN! Input FVector = %s"), *R.ToString(), *this->ToString());
		R = FRotator::ZeroRotator;
	}
#endif

	return R;
}

template<>
FRotator UE::Core::TVector<double>::ToOrientationRotator() const
{
	FRotator R;

	// Find yaw.
	R.Yaw = static_cast<float>(FMath::Atan2(Y, X) * (180.0 / PI));

	// Find pitch.
	R.Pitch = static_cast<float>(FMath::Atan2(Z, FMath::Sqrt(X * X + Y * Y)) * (180.0 / PI));

	// Find roll.
	R.Roll = 0;

#if ENABLE_NAN_DIAGNOSTIC || (DO_CHECK && !UE_BUILD_SHIPPING)
	if (R.ContainsNaN())
	{
		logOrEnsureNanError(TEXT("FVector::Rotation(): Rotator result %s contains NaN! Input FVector = %s"), *R.ToString(), *this->ToString());
		R = FRotator::ZeroRotator;
	}
#endif

	return R;
}

template<>
FRotator UE::Core::TVector<float>::Rotation() const
{
	return ToOrientationRotator();
}

template<>
FRotator UE::Core::TVector<double>::Rotation() const
{
	return ToOrientationRotator();
}
