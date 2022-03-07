// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include "HLSLTree/HLSLTreeCommon.h"
#include "MaterialTypes.h"
#include "Materials/MaterialLayersFunctions.h"
#include "RHIDefinitions.h"

enum class EMaterialParameterType : uint8;
struct FMaterialCachedExpressionData;
struct FMaterialLayersFunctions;

namespace UE::HLSLTree::Material
{

enum class EExternalInput : uint8
{
	None,

	TexCoord0,
	TexCoord1,
	TexCoord2,
	TexCoord3,
	TexCoord4,
	TexCoord5,
	TexCoord6,
	TexCoord7,

	TexCoord0_Ddx,
	TexCoord1_Ddx,
	TexCoord2_Ddx,
	TexCoord3_Ddx,
	TexCoord4_Ddx,
	TexCoord5_Ddx,
	TexCoord6_Ddx,
	TexCoord7_Ddx,

	TexCoord0_Ddy,
	TexCoord1_Ddy,
	TexCoord2_Ddy,
	TexCoord3_Ddy,
	TexCoord4_Ddy,
	TexCoord5_Ddy,
	TexCoord6_Ddy,
	TexCoord7_Ddy,

	LightmapTexCoord,
	LightmapTexCoord_Ddx,
	LightmapTexCoord_Ddy,

	TwoSidedSign,
	VertexColor,
	VertexColor_Ddx,
	VertexColor_Ddy,

	WorldPosition,
	WorldPosition_NoOffsets,
	TranslatedWorldPosition,
	TranslatedWorldPosition_NoOffsets,
	ActorWorldPosition,

	PrevWorldPosition,
	PrevWorldPosition_NoOffsets,
	PrevTranslatedWorldPosition,
	PrevTranslatedWorldPosition_NoOffsets,

	WorldPosition_Ddx,
	WorldPosition_Ddy,

	WorldVertexNormal,
	WorldVertexTangent,
	WorldNormal,
	WorldReflection,

	PreSkinnedPosition,
	PreSkinnedNormal,
	PreSkinnedLocalBoundsMin,
	PreSkinnedLocalBoundsMax,

	ViewportUV,
	PixelPosition,
	ViewSize,
	RcpViewSize,
	FieldOfView,
	TanHalfFieldOfView,
	CotanHalfFieldOfView,
	TemporalSampleCount,
	TemporalSampleIndex,
	TemporalSampleOffset,
	PreExposure,
	RcpPreExposure,
	EyeAdaptation,
	RuntimeVirtualTextureOutputLevel,
	RuntimeVirtualTextureOutputDerivative,
	RuntimeVirtualTextureMaxLevel,

	CameraVector,
	CameraWorldPosition,
	ViewWorldPosition,
	PreViewTranslation,
	TangentToWorld,
	LocalToWorld,
	WorldToLocal,
	TranslatedWorldToCameraView,
	TranslatedWorldToView,
	CameraViewToTranslatedWorld,
	ViewToTranslatedWorld,
	WorldToParticle,
	WorldToInstance,
	ParticleToWorld,
	InstanceToWorld,

	PrevFieldOfView,
	PrevTanHalfFieldOfView,
	PrevCotanHalfFieldOfView,

	PrevCameraWorldPosition,
	PrevViewWorldPosition,
	PrevPreViewTranslation,
	PrevLocalToWorld,
	PrevWorldToLocal,
	PrevTranslatedWorldToCameraView,
	PrevTranslatedWorldToView,
	PrevCameraViewToTranslatedWorld,
	PrevViewToTranslatedWorld,

	PixelDepth,
	PixelDepth_Ddx,
	PixelDepth_Ddy,

	GameTime,
	RealTime,
	DeltaTime,

	PrevGameTime,
	PrevRealTime,

	ParticleColor,
	ParticleTranslatedWorldPosition,
	ParticleRadius,
};
static constexpr int32 MaxNumTexCoords = 8;

struct FExternalInputDescription
{
	FExternalInputDescription(const TCHAR* InName, Shader::EValueType InType, EExternalInput InDdx = EExternalInput::None, EExternalInput InDdy = EExternalInput::None, EExternalInput InPreviousFrame = EExternalInput::None)
		: Name(InName), Type(InType), Ddx(InDdx), Ddy(InDdy), PreviousFrame(InPreviousFrame)
	{}

	const TCHAR* Name;
	Shader::EValueType Type;
	EExternalInput Ddx;
	EExternalInput Ddy;
	EExternalInput PreviousFrame;
};

FExternalInputDescription GetExternalInputDescription(EExternalInput Input);

inline bool IsTexCoord(EExternalInput Type)
{
	return FMath::IsWithin((int32)Type, (int32)EExternalInput::TexCoord0, (int32)EExternalInput::TexCoord0 + MaxNumTexCoords);
}
inline bool IsTexCoord_Ddx(EExternalInput Type)
{
	return FMath::IsWithin((int32)Type, (int32)EExternalInput::TexCoord0_Ddx, (int32)EExternalInput::TexCoord0_Ddx + MaxNumTexCoords);
}
inline bool IsTexCoord_Ddy(EExternalInput Type)
{
	return FMath::IsWithin((int32)Type, (int32)EExternalInput::TexCoord0_Ddy, (int32)EExternalInput::TexCoord0_Ddy + MaxNumTexCoords);
}
inline EExternalInput MakeInputTexCoord(int32 Index)
{
	check(Index >= 0 && Index < MaxNumTexCoords);
	return (EExternalInput)((int32)EExternalInput::TexCoord0 + Index);
}

class FExpressionExternalInput : public FExpression
{
public:
	FExpressionExternalInput(EExternalInput InInputType) : InputType(InInputType) {}

	EExternalInput InputType;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
};

class FExpressionShadingModel : public FExpression
{
public:
	explicit FExpressionShadingModel(EMaterialShadingModel InShadingModel)
		: ShadingModel(InShadingModel)
	{}

	TEnumAsByte<EMaterialShadingModel> ShadingModel;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

inline void AppendHash(FHasher& Hasher, const FMaterialParameterInfo& Value)
{
	AppendHash(Hasher, Value.Name);
	AppendHash(Hasher, Value.Index);
	AppendHash(Hasher, Value.Association);
}

inline void AppendHash(FHasher& Hasher, const FMaterialParameterValue& Value)
{
	switch (Value.Type)
	{
	case EMaterialParameterType::Scalar: AppendHash(Hasher, Value.Float[0]); break;
	case EMaterialParameterType::Vector: AppendHash(Hasher, Value.Float); break;
	case EMaterialParameterType::DoubleVector: AppendHash(Hasher, Value.Double); break;
	case EMaterialParameterType::Texture: AppendHash(Hasher, Value.Texture); break;
	case EMaterialParameterType::Font: AppendHash(Hasher, Value.Font); break;
	case EMaterialParameterType::RuntimeVirtualTexture: AppendHash(Hasher, Value.RuntimeVirtualTexture); break;
	case EMaterialParameterType::StaticSwitch: AppendHash(Hasher, Value.Bool[0]); break;
	case EMaterialParameterType::StaticComponentMask: AppendHash(Hasher, Value.Bool); break;
	default: checkNoEntry(); break;
	}
}

inline void AppendHash(FHasher& Hasher, const FMaterialParameterMetadata& Meta)
{
	AppendHash(Hasher, Meta.Value);
}

class FExpressionParameter : public FExpression
{
public:
	explicit FExpressionParameter(const FMaterialParameterInfo& InParameterInfo, const FMaterialParameterMetadata& InParameterMeta, const Shader::FValue& InDefaultValue)
		: ParameterInfo(InParameterInfo), ParameterMeta(InParameterMeta), DefaultValue(InDefaultValue)
	{
	}

	FMaterialParameterInfo ParameterInfo;
	FMaterialParameterMetadata ParameterMeta;
	Shader::FValue DefaultValue;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionTextureSize : public FExpression
{
public:
	explicit FExpressionTextureSize(const FMaterialParameterInfo& InParameterInfo, UTexture* InTexture)
		: ParameterInfo(InParameterInfo), Texture(InTexture)
	{}

	FMaterialParameterInfo ParameterInfo;
	UTexture* Texture;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionFunctionCall : public FExpressionForward
{
public:
	FExpressionFunctionCall(FExpression* InExpression, UMaterialFunctionInterface* InMaterialFunction)
		: FExpressionForward(InExpression)
		, MaterialFunction(InMaterialFunction)
	{}

	UMaterialFunctionInterface* MaterialFunction;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
};

class FExpressionMaterialLayers : public FExpressionForward
{
public:
	FExpressionMaterialLayers(FExpression* InExpression, const FMaterialLayersFunctions* InMaterialLayers)
		: FExpressionForward(InExpression)
		, MaterialLayers(InMaterialLayers)
	{}

	const FMaterialLayersFunctions* MaterialLayers;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
};

class FExpressionSceneTexture : public FExpression
{
public:
	FExpressionSceneTexture(FExpression* InTexCoordExpression, uint32 InSceneTextureId, bool bInFiltered)
		: TexCoordExpression(InTexCoordExpression)
		, SceneTextureId(InSceneTextureId)
		, bFiltered(bInFiltered)
	{}

	FExpression* TexCoordExpression;
	uint32 SceneTextureId;
	bool bFiltered;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
};

struct FNoiseParameters
{
	FNoiseParameters() { FMemory::Memzero(*this); }

	int32 Quality;
	int32 Levels;
	float Scale;
	uint32 RepeatSize;
	float OutputMin;
	float OutputMax;
	float LevelScale;
	uint8 NoiseFunction;
	bool bTiling;
	bool bTurbulence;
};

class FExpressionNoise : public FExpression
{
public:
	FExpressionNoise(const FNoiseParameters& InParams, FExpression* InPositionExpression, FExpression* InFilterWidthExpression)
		: PositionExpression(InPositionExpression)
		, FilterWidthExpression(InFilterWidthExpression)
		, Parameters(InParams)
	{}

	FExpression* PositionExpression;
	FExpression* FilterWidthExpression;
	FNoiseParameters Parameters;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
};

class FEmitData
{
public:
	FEmitData()
	{
		for (int32 Index = 0; Index < SF_NumFrequencies; ++Index)
		{
			ExternalInputMask[Index].Init(false, 256);
		}
	}

	const FStaticParameterSet* StaticParameters = nullptr;
	FMaterialCachedExpressionData* CachedExpressionData = nullptr;
	TMap<Shader::FValue, uint32> DefaultUniformValues;
	bool bReadMaterialNormal = false;
	bool bUsesVertexColor = false;
	bool bNeedsParticlePosition = false;
	bool bUsesParticleColor = false;
	TBitArray<> ExternalInputMask[SF_NumFrequencies];
	FMaterialShadingModelField ShadingModelsFromCompilation;

	bool IsExternalInputUsed(EShaderFrequency Frequency, EExternalInput Input) const { return ExternalInputMask[Frequency][(uint8)Input]; }
};

} // namespace UE::HLSLTree::Material

#endif // WITH_EDITOR
