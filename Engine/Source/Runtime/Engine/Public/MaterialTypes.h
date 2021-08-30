// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "MaterialTypes.generated.h"

class UTexture;
class UCurveLinearColor;
class UCurveLinearColorAtlas;
class UFont;
class URuntimeVirtualTexture;

enum class EMaterialParameterType : uint8
{
	Scalar = 0u,
	Vector,
	Texture,
	Font,
	RuntimeVirtualTexture,

	NumRuntime, // Runtime parameter types must go above here, and editor-only ones below

	// TODO - Would be nice to make static parameter values editor-only, but will save that for a future-refactor
	StaticSwitch = NumRuntime,
	StaticComponentMask,

	Num,
	None = 0xff,
};

static const int32 NumMaterialParameterTypes = (int32)EMaterialParameterType::Num;
static const int32 NumMaterialRuntimeParameterTypes = (int32)EMaterialParameterType::NumRuntime;
static const int32 NumMaterialEditorOnlyParameterTypes = NumMaterialParameterTypes - NumMaterialRuntimeParameterTypes;

static inline bool IsStaticMaterialParameter(EMaterialParameterType InType)
{
	switch (InType)
	{
	case EMaterialParameterType::StaticSwitch:
	case EMaterialParameterType::StaticComponentMask:
		return true;
	default:
		return false;
	}
}

enum class EMaterialGetParameterValueFlags : uint32
{
	None = 0u,
	CheckNonOverrides = (1u << 0),
	CheckInstanceOverrides = (1u << 1),
	CheckParent = (1u << 2),
	IsParent = (1u << 3), // automatically set when recursing into a parent

	CheckAll = CheckNonOverrides | CheckInstanceOverrides | CheckParent,
};
ENUM_CLASS_FLAGS(EMaterialGetParameterValueFlags);

enum class EMaterialSetParameterValueFlags : uint32
{
	None = 0u,
	SetCurveAtlas = (1u << 0),
};
ENUM_CLASS_FLAGS(EMaterialSetParameterValueFlags);

USTRUCT()
struct FStaticComponentMaskValue
{
	GENERATED_USTRUCT_BODY();

	FStaticComponentMaskValue() : R(false), G(false), B(false), A(false) {}
	FStaticComponentMaskValue(bool InR, bool InG, bool InB, bool InA) : R(InR), G(InG), B(InB), A(InA) {}

	UPROPERTY()
	bool R = false;

	UPROPERTY()
	bool G = false;

	UPROPERTY()
	bool B = false;

	UPROPERTY()
	bool A = false;
};

struct FMaterialParameterValue
{
	FMaterialParameterValue() : Type(EMaterialParameterType::None) {}
	FMaterialParameterValue(float InValue) : Type(EMaterialParameterType::Scalar) { Float[0] = InValue; }
	FMaterialParameterValue(const FLinearColor& InValue) : Type(EMaterialParameterType::Vector) { Float[0] = InValue.R; Float[1] = InValue.G; Float[2] = InValue.B; Float[3] = InValue.A; }
	FMaterialParameterValue(UTexture* InValue) : Type(EMaterialParameterType::Texture) { Texture = InValue; }
	FMaterialParameterValue(const TObjectPtr<UTexture>& InValue) : Type(EMaterialParameterType::Texture) { Texture = InValue; }
	FMaterialParameterValue(URuntimeVirtualTexture* InValue) : Type(EMaterialParameterType::RuntimeVirtualTexture) { RuntimeVirtualTexture = InValue; }
	FMaterialParameterValue(const TObjectPtr<URuntimeVirtualTexture>& InValue) : Type(EMaterialParameterType::RuntimeVirtualTexture) { RuntimeVirtualTexture = InValue; }
	FMaterialParameterValue(UFont* InValue, int32 InPage) : Type(EMaterialParameterType::Font) { Font.Value = InValue; Font.Page = InPage; }
	FMaterialParameterValue(bool InValue) : Type(EMaterialParameterType::StaticSwitch) { Bool[0] = InValue; }
	FMaterialParameterValue(const FStaticComponentMaskValue& InValue) : Type(EMaterialParameterType::StaticComponentMask) { Bool[0] = InValue.R; Bool[1] = InValue.G; Bool[2] = InValue.B; Bool[3] = InValue.A; }
	FMaterialParameterValue(bool bMaskR, bool bMaskG, bool bMaskB, bool bMaskA) : Type(EMaterialParameterType::StaticComponentMask) { Bool[0] = bMaskR; Bool[1] = bMaskG; Bool[2] = bMaskB; Bool[3] = bMaskA; }

	inline float AsScalar() const { check(Type == EMaterialParameterType::Scalar); return Float[0]; }
	inline FLinearColor AsLinearColor() const { check(Type == EMaterialParameterType::Vector); return FLinearColor(Float[0], Float[1], Float[2], Float[3]); }
	inline bool AsStaticSwitch() const { check(Type == EMaterialParameterType::StaticSwitch); return Bool[0]; }
	inline FStaticComponentMaskValue AsStaticComponentMask() const { check(Type == EMaterialParameterType::StaticComponentMask); return FStaticComponentMaskValue(Bool[0], Bool[1], Bool[2], Bool[3]); }

	union
	{
		float Float[4];
		bool Bool[4];
		UTexture* Texture;
		URuntimeVirtualTexture* RuntimeVirtualTexture;
		struct
		{
			UFont* Value;
			int32 Page;
		} Font;
	};
	EMaterialParameterType Type;
};

inline bool operator==(const FMaterialParameterValue& Lhs, const FMaterialParameterValue& Rhs)
{
	const EMaterialParameterType Type = Lhs.Type;
	if (Type != Rhs.Type)
	{
		return false;
	}
	switch (Type)
	{
	case EMaterialParameterType::None: return true;
	case EMaterialParameterType::Scalar: return Lhs.Float[0] == Rhs.Float[0];
	case EMaterialParameterType::Vector: return
		Lhs.Float[0] == Rhs.Float[0] &&
		Lhs.Float[1] == Rhs.Float[1] &&
		Lhs.Float[2] == Rhs.Float[2] &&
		Lhs.Float[3] == Rhs.Float[3];
	case EMaterialParameterType::Texture: return Lhs.Texture == Rhs.Texture;
	case EMaterialParameterType::Font: return Lhs.Font.Value == Rhs.Font.Value && Lhs.Font.Page == Rhs.Font.Page;
	case EMaterialParameterType::RuntimeVirtualTexture: return Lhs.RuntimeVirtualTexture == Rhs.RuntimeVirtualTexture;
	case EMaterialParameterType::StaticSwitch: return Lhs.Bool[0] == Rhs.Bool[0];
	case EMaterialParameterType::StaticComponentMask: return
		Lhs.Bool[0] == Rhs.Bool[0] &&
		Lhs.Bool[1] == Rhs.Bool[1] &&
		Lhs.Bool[2] == Rhs.Bool[2] &&
		Lhs.Bool[3] == Rhs.Bool[3];
	default: checkNoEntry(); return false;
	}
}

inline bool operator!=(const FMaterialParameterValue& Lhs, const FMaterialParameterValue& Rhs)
{
	return !operator==(Lhs, Rhs);
}

/** Holds a value, along with editor-only metadata that describes that value */
struct FMaterialParameterMetadata
{
	FMaterialParameterValue Value;

#if WITH_EDITORONLY_DATA
	/** Name of channels, for Vectors/Textures */
	FText ChannelName[4];

	/** Curve/Atlas used to generate scalar value */
	TSoftObjectPtr<class UCurveLinearColor> ScalarCurve;
	TSoftObjectPtr<class UCurveLinearColorAtlas> ScalarAtlas;

	/** UI range for scalar values */
	float ScalarMin = 0.0f;
	float ScalarMax = 0.0f;

	/** GUID of the UMaterialExpression this parameter came from */
	FGuid ExpressionGuid;

	/** Should curves be used? */
	bool bUsedAsAtlasPosition = false;

	/** Valid for Vector parameters */
	bool bUsedAsChannelMask = false;

	/** Is the parameter overriden on the material it was queried from? */
	bool bOverride = false;
#endif // WITH_EDITORONLY_DATA
};