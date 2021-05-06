// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector2D.h"
#include "Math/Vector.h"
#include "Math/Vector4.h"
#include "Math/Rotator.h"
#include "Math/Color.h"
#include "EulerTransform.h"
#include "Styling/SlateColor.h"
#include "EntitySystem/MovieSceneComponentDebug.h"

class USceneComponent;


namespace UE
{
namespace MovieScene
{




/** Intermediate type for the vector property system that lets us store how many dimensions the vector should have */
struct FIntermediateVector
{
	float X, Y, Z, W;

	FIntermediateVector()
		: X(0), Y(0), Z(0), W(0)
	{}

	FIntermediateVector(float InX, float InY)
		: X(InX), Y(InY), Z(0), W(0)
	{}

	FIntermediateVector(float InX, float InY, float InZ)
		: X(InX), Y(InY), Z(InZ), W(0)
	{}

	FIntermediateVector(float InX, float InY, float InZ, float InW)
		: X(InX), Y(InY), Z(InZ), W(InW)
	{}
};

/** Color type for the color property system */
enum class EColorPropertyType : uint8
{
	/** Undefined */
	Undefined,
	/** FSlateColor */
	Slate, 
	/** FLinearColor */
	Linear,
	/** FColor */
	Color,
};

#if UE_MOVIESCENE_ENTITY_DEBUG

template<> struct TComponentDebugType<EColorPropertyType> { static const EComponentDebugType Type = EComponentDebugType::Uint8;   };

#endif

/** Intermediate type for the color property system that lets us store what kind of color type we should use */
struct FIntermediateColor
{
	float R, G, B, A;

	FIntermediateColor()
		: R(0.f), G(0.f), B(0.f), A(0.f)
	{}

	explicit FIntermediateColor(float InR, float InG, float InB, float InA)
		: R(InR), G(InG), B(InB), A(InA)
	{}
	explicit FIntermediateColor(const FLinearColor& InColor)
		: R(InColor.R), G(InColor.G), B(InColor.B), A(InColor.A)
	{}
	explicit FIntermediateColor(const FColor& InColor)
		: R(InColor.R), G(InColor.G), B(InColor.B), A(InColor.A)
	{}

	explicit FIntermediateColor(const FSlateColor& InSlateColor)
	{
		FLinearColor SpecifiedColor = InSlateColor.GetSpecifiedColor();
		R = SpecifiedColor.R;
		G = SpecifiedColor.G;
		B = SpecifiedColor.B;
		A = SpecifiedColor.A;
	}

	float operator[](int32 Index) const
	{
		check(Index >= 0 && Index <= 3);
		return (&R)[Index];
	}

	FColor GetColor() const
	{
		const bool bConvertBackToSRgb = true;
		const FColor SRgbColor = FLinearColor(R, G, B, A).ToFColor(bConvertBackToSRgb);
		return SRgbColor;
	}

	FLinearColor GetLinearColor() const
	{
		return FLinearColor(R, G, B, A);
	}

	FSlateColor GetSlateColor() const
	{
		return FSlateColor(GetLinearColor());
	}
};

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

MOVIESCENETRACKS_API FIntermediate3DTransform GetComponentTransform(const UObject* Object);
MOVIESCENETRACKS_API void SetComponentTransform(USceneComponent* SceneComponent, const FIntermediate3DTransform& InTransform);

MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediate3DTransform& In, FEulerTransform& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FEulerTransform& In, FIntermediate3DTransform& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediate3DTransform& In, FTransform& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FTransform& In, FIntermediate3DTransform& Out);

MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediateColor& InColor, FColor& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediateColor& InColor, FLinearColor& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediateColor& InColor, FSlateColor& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FColor& InColor, FIntermediateColor& OutIntermediate);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FLinearColor& InColor, FIntermediateColor& OutIntermediate);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FSlateColor& InColor, FIntermediateColor& OutIntermediate);

MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediateVector& InVector, FVector2D& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediateVector& InVector, FVector& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FIntermediateVector& InVector, FVector4& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FVector2D& In, FIntermediateVector& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FVector& In, FIntermediateVector& Out);
MOVIESCENETRACKS_API void ConvertOperationalProperty(const FVector4& In, FIntermediateVector& Out);


} // namespace MovieScene
} // namespace UE
