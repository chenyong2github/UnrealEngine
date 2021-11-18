// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Misc/EnumClassFlags.h"
#include "Serialization/MemoryLayout.h"
#include "UObject/SoftObjectPtr.h"
#include "Shader/ShaderTypes.h"
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
	DoubleVector,
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
DECLARE_INTRINSIC_TYPE_LAYOUT(EMaterialParameterType);

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

extern ENGINE_API UE::Shader::EValueType GetShaderValueType(EMaterialParameterType Type);

enum class EMaterialGetParameterValueFlags : uint32
{
	None = 0u,
	CheckNonOverrides = (1u << 0),
	CheckInstanceOverrides = (1u << 1),

	CheckAll = CheckNonOverrides | CheckInstanceOverrides,
	Default = CheckAll,
};
ENUM_CLASS_FLAGS(EMaterialGetParameterValueFlags);

enum class EMaterialSetParameterValueFlags : uint32
{
	None = 0u,
	SetCurveAtlas = (1u << 0),
};
ENUM_CLASS_FLAGS(EMaterialSetParameterValueFlags);

USTRUCT()
struct FParameterChannelNames
{
	GENERATED_USTRUCT_BODY()

	FParameterChannelNames() = default;
	FParameterChannelNames(const FText& InR, const FText& InG, const FText& InB, const FText& InA) : R(InR), G(InG), B(InB), A(InA) {}

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText R;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText G;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText B;

	UPROPERTY(EditAnywhere, Category = MaterialExpressionVectorParameter)
	FText A;
};

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
	FMaterialParameterValue(const FVector3f& InValue) : Type(EMaterialParameterType::Vector) { Float[0] = InValue.X; Float[1] = InValue.Y; Float[2] = InValue.Z; Float[3] = 0.0f; }
	FMaterialParameterValue(const FVector4d& InValue) : Type(EMaterialParameterType::DoubleVector) { Double[0] = InValue.X; Double[1] = InValue.Y; Double[2] = InValue.Z; Double[3] = InValue.W; }
	FMaterialParameterValue(UTexture* InValue) : Type(EMaterialParameterType::Texture) { Texture = InValue; }
	FMaterialParameterValue(const TObjectPtr<UTexture>& InValue) : Type(EMaterialParameterType::Texture) { Texture = InValue; }
	FMaterialParameterValue(URuntimeVirtualTexture* InValue) : Type(EMaterialParameterType::RuntimeVirtualTexture) { RuntimeVirtualTexture = InValue; }
	FMaterialParameterValue(const TObjectPtr<URuntimeVirtualTexture>& InValue) : Type(EMaterialParameterType::RuntimeVirtualTexture) { RuntimeVirtualTexture = InValue; }

	// Gamethread parameters are typically non-const, but renderthread parameters are const
	// Would be possible to store an additional const-flag member, and provide runtime checks to ensure constness is not violated...maybe worth doing in the future
	FMaterialParameterValue(const UTexture* InValue) : Type(EMaterialParameterType::Texture) { Texture = const_cast<UTexture*>(InValue); }
	FMaterialParameterValue(const TObjectPtr<const UTexture>& InValue) : Type(EMaterialParameterType::Texture) { Texture = const_cast<UTexture*>(InValue.Get()); }
	FMaterialParameterValue(const URuntimeVirtualTexture* InValue) : Type(EMaterialParameterType::RuntimeVirtualTexture) { RuntimeVirtualTexture = const_cast<URuntimeVirtualTexture*>(InValue); }
	FMaterialParameterValue(const TObjectPtr<const URuntimeVirtualTexture>& InValue) : Type(EMaterialParameterType::RuntimeVirtualTexture) { RuntimeVirtualTexture = const_cast<URuntimeVirtualTexture*>(InValue.Get()); }

	FMaterialParameterValue(UFont* InValue, int32 InPage) : Type(EMaterialParameterType::Font) { Font.Value = InValue; Font.Page = InPage; }
	FMaterialParameterValue(bool InValue) : Type(EMaterialParameterType::StaticSwitch) { Bool[0] = InValue; }
	FMaterialParameterValue(const FStaticComponentMaskValue& InValue) : Type(EMaterialParameterType::StaticComponentMask) { Bool[0] = InValue.R; Bool[1] = InValue.G; Bool[2] = InValue.B; Bool[3] = InValue.A; }
	FMaterialParameterValue(bool bMaskR, bool bMaskG, bool bMaskB, bool bMaskA) : Type(EMaterialParameterType::StaticComponentMask) { Bool[0] = bMaskR; Bool[1] = bMaskG; Bool[2] = bMaskB; Bool[3] = bMaskA; }
	
	ENGINE_API FMaterialParameterValue(EMaterialParameterType Type, const UE::Shader::FValue& InValue);

	inline float AsScalar() const { check(Type == EMaterialParameterType::Scalar); return Float[0]; }
	inline FLinearColor AsLinearColor() const { check(Type == EMaterialParameterType::Vector); return FLinearColor(Float[0], Float[1], Float[2], Float[3]); }
	inline FVector4d AsVector4d() const { check(Type == EMaterialParameterType::DoubleVector); return FVector4d(Double[0], Double[1], Double[2], Double[3]); }
	inline bool AsStaticSwitch() const { check(Type == EMaterialParameterType::StaticSwitch); return Bool[0]; }
	inline FStaticComponentMaskValue AsStaticComponentMask() const { check(Type == EMaterialParameterType::StaticComponentMask); return FStaticComponentMaskValue(Bool[0], Bool[1], Bool[2], Bool[3]); }
	ENGINE_API UE::Shader::FValue AsShaderValue() const;

	union
	{
		double Double[4];
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
	case EMaterialParameterType::DoubleVector: return
		Lhs.Double[0] == Rhs.Double[0] &&
		Lhs.Double[1] == Rhs.Double[1] &&
		Lhs.Double[2] == Rhs.Double[2] &&
		Lhs.Double[3] == Rhs.Double[3];
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
	int32 PrimitiveDataIndex = INDEX_NONE;

	FMaterialParameterMetadata() = default;
	FMaterialParameterMetadata(const FMaterialParameterValue& InValue) : Value(InValue) {}
	FMaterialParameterMetadata(EMaterialParameterType Type, const UE::Shader::FValue& InValue) : Value(Type, InValue) {}

#if WITH_EDITORONLY_DATA
	/** Name of channels, for Vectors/Textures */
	FParameterChannelNames ChannelNames;

	/** Curve/Atlas used to generate scalar value */
	TSoftObjectPtr<class UCurveLinearColor> ScalarCurve;
	TSoftObjectPtr<class UCurveLinearColorAtlas> ScalarAtlas;

	/** Description of the parameter, typically taken from the 'Desc' field of the parameter's UMaterialExpression */
	FString Description;

	/** Name of the parameter's group */
	FName Group;

	/** UI range for scalar values */
	float ScalarMin = 0.0f;
	float ScalarMax = 0.0f;

	/** Used for sorting parameter within the group, in the UI */
	int32 SortPriority = 0;

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