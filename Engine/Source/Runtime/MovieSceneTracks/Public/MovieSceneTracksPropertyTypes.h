// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "Math/Rotator.h"

class USceneComponent;


namespace UE
{
namespace MovieScene
{


/** Intermediate type used for applying partially animated transforms. Saves us from repteatedly recomposing quaternions from euler angles */
struct FIntermediate3DTransform
{
	float T_X, T_Y, T_Z, R_X, R_Y, R_Z, S_X, S_Y, S_Z;

	FIntermediate3DTransform()
		: T_X(0.f), T_Y(0.f), T_Z(0.f), R_X(0.f), R_Y(0.f), R_Z(0.f), S_X(0.f), S_Y(0.f), S_Z(0.f)
	{}

	FIntermediate3DTransform(float InT_X, float InT_Y, float InT_Z, float InR_X, float InR_Y, float InR_Z, float InS_X, float InS_Y, float InS_Z)
		: T_X(InT_X), T_Y(InT_Y), T_Z(InT_Z), R_X(InR_X), R_Y(InR_Y), R_Z(InR_Z), S_X(InS_X), S_Y(InS_Y), S_Z(InS_Z)
	{}

	FIntermediate3DTransform(const FVector& InLocation, const FRotator& InRotation, const FVector& InScale)
		: T_X(InLocation.X), T_Y(InLocation.Y), T_Z(InLocation.Z)
		, R_X(InRotation.Roll), R_Y(InRotation.Pitch), R_Z(InRotation.Yaw)
		, S_X(InScale.X), S_Y(InScale.Y), S_Z(InScale.Z)
	{}

	float operator[](int32 Index) const
	{
		check(Index >= 0 && Index < 9);
		return (&T_X)[Index];
	}

	FVector GetTranslation() const
	{
		return FVector(T_X, T_Y, T_Z);
	}
	FRotator GetRotation() const
	{
		return FRotator(R_Y, R_Z, R_X);
	}
	FVector GetScale() const
	{
		return FVector(S_X, S_Y, S_Z);
	}

	MOVIESCENETRACKS_API void ApplyTo(USceneComponent* SceneComponent) const;
};


} // namespace MovieScene
} // namespace UE
