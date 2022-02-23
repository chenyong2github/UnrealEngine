// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HLSLTree/HLSLTree.h"

enum class EMaterialParameterType : uint8;

namespace UE
{

namespace HLSLTree
{

class FExpressionConstant : public FExpression
{
public:
	explicit FExpressionConstant(const Shader::FValue& InValue)
		: Value(InValue)
	{}

	Shader::FValue Value;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionMaterialShadingModel : public FExpression
{
public:
	explicit FExpressionMaterialShadingModel(EMaterialShadingModel InShadingModel)
		: ShadingModel(InShadingModel)
	{}

	TEnumAsByte<EMaterialShadingModel> ShadingModel;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionMaterialParameter : public FExpression
{
public:
	explicit FExpressionMaterialParameter(EMaterialParameterType InType, const FName& InName, const Shader::FValue& InDefaultValue)
		: ParameterName(InName), DefaultValue(InDefaultValue), ParameterType(InType)
	{
	}

	FName ParameterName;
	Shader::FValue DefaultValue;
	EMaterialParameterType ParameterType;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

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

class FExpressionMaterialSceneTexture : public FExpression
{
public:
	FExpressionMaterialSceneTexture(FExpression* InTexCoordExpression, uint32 InSceneTextureId, bool bInFiltered)
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

struct FMaterialNoiseParameters
{
	FMaterialNoiseParameters() { FMemory::Memzero(*this); }

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

class FExpressionMaterialNoise : public FExpression
{
public:
	FExpressionMaterialNoise(const FMaterialNoiseParameters& InParams, FExpression* InPositionExpression, FExpression* InFilterWidthExpression)
		: PositionExpression(InPositionExpression)
		, FilterWidthExpression(InFilterWidthExpression)
		, Parameters(InParams)
	{}

	FExpression* PositionExpression;
	FExpression* FilterWidthExpression;
	FMaterialNoiseParameters Parameters;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
};

class FExpressionTextureSample : public FExpression
{
public:
	FExpressionTextureSample(FExpression* InTextureExpression,
		FExpression* InTexCoordExpression,
		const FExpressionDerivatives& InTexCoordDerivatives,
		ESamplerSourceMode InSamplerSource,
		ETextureMipValueMode InMipValueMode)
		: TextureExpression(InTextureExpression)
		, TexCoordExpression(InTexCoordExpression)
		, TexCoordDerivatives(InTexCoordDerivatives)
		, SamplerSource(InSamplerSource)
		, MipValueMode(InMipValueMode)
	{}

	FExpression* TextureExpression;
	FExpression* TexCoordExpression;
	FExpressionDerivatives TexCoordDerivatives;
	ESamplerSourceMode SamplerSource;
	ETextureMipValueMode MipValueMode;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
};

class FExpressionGetStructField : public FExpression
{
public:
	FExpressionGetStructField(const Shader::FStructType* InStructType, const Shader::FStructField* InField, FExpression* InStructExpression)
		: StructType(InStructType)
		, Field(InField)
		, StructExpression(InStructExpression)
	{
	}

	const Shader::FStructType* StructType;
	const Shader::FStructField* Field;
	FExpression* StructExpression;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionSetStructField : public FExpression
{
public:
	FExpressionSetStructField(const Shader::FStructType* InStructType, const Shader::FStructField* InField, FExpression* InStructExpression, FExpression* InFieldExpression)
		: StructType(InStructType)
		, Field(InField)
		, StructExpression(InStructExpression)
		, FieldExpression(InFieldExpression)
	{
		check(InStructType);
		check(InField);
	}

	const Shader::FStructType* StructType;
	const Shader::FStructField* Field;
	FExpression* StructExpression;
	FExpression* FieldExpression;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionSelect : public FExpression
{
public:
	FExpressionSelect(FExpression* InCondition, FExpression* InTrue, FExpression* InFalse)
		: ConditionExpression(InCondition)
		, TrueExpression(InTrue)
		, FalseExpression(InFalse)
	{}

	FExpression* ConditionExpression;
	FExpression* TrueExpression;
	FExpression* FalseExpression;

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionDerivative : public FExpression
{
public:
	FExpressionDerivative(EDerivativeCoordinate InCoord, FExpression* InInput) : Input(InInput), Coord(InCoord) {}

	FExpression* Input;
	EDerivativeCoordinate Coord;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

struct FSwizzleParameters
{
	FSwizzleParameters() : NumComponents(0) { ComponentIndex[0] = ComponentIndex[1] = ComponentIndex[2] = ComponentIndex[3] = INDEX_NONE; }
	explicit FSwizzleParameters(int8 IndexR, int8 IndexG = INDEX_NONE, int8 IndexB = INDEX_NONE, int8 IndexA = INDEX_NONE);

	FRequestedType GetRequestedInputType(const FRequestedType& RequestedType) const;
	bool HasSwizzle() const;

	int8 ComponentIndex[4];
	int32 NumComponents;
};
FSwizzleParameters MakeSwizzleMask(bool bInR, bool bInG, bool bInB, bool bInA);

class FExpressionSwizzle : public FExpression
{
public:
	FExpressionSwizzle(const FSwizzleParameters& InParams, FExpression* InInput)
		: Parameters(InParams)
		, Input(InInput)
	{}

	FSwizzleParameters Parameters;
	FExpression* Input;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

class FExpressionAppend : public FExpression
{
public:
	FExpressionAppend(FExpression* InLhs, FExpression* InRhs)
		: Lhs(InLhs)
		, Rhs(InRhs)
	{}

	FExpression* Lhs;
	FExpression* Rhs;

	virtual void ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const override;
	virtual FExpression* ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const override;
	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;
	virtual void EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const override;
};

/** Can be used to emit HLSL chunks with no inputs, where it's not worth the trouble of defining a new expression type */
class FExpressionInlineCustomHLSL : public FExpression
{
public:
	FExpressionInlineCustomHLSL(Shader::EValueType InType, FStringView InCode) : Code(InCode), ResultType(InType) {}

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;

	FStringView Code;
	Shader::EValueType ResultType;
};

class FExpressionCustomHLSL : public FExpression
{
public:
	FExpressionCustomHLSL(FStringView InDeclarationCode, FStringView InFunctionCode, TArrayView<FCustomHLSLInput> InInputs, const Shader::FStructType* InOutputStructType)
		: DeclarationCode(InDeclarationCode)
		, FunctionCode(InFunctionCode)
		, Inputs(InInputs)
		, OutputStructType(InOutputStructType)
	{}

	virtual bool PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const override;
	virtual void EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const override;

	FStringView DeclarationCode;
	FStringView FunctionCode;
	TArray<FCustomHLSLInput, TInlineAllocator<8>> Inputs;
	const Shader::FStructType* OutputStructType = nullptr;
};

class FStatementReturn : public FStatement
{
public:
	FExpression* Expression;

	virtual bool Prepare(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitShader(FEmitContext& Context, FEmitScope& Scope) const override;
};

class FStatementBreak : public FStatement
{
public:
	virtual bool Prepare(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitShader(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const override;
};

class FStatementIf : public FStatement
{
public:
	FExpression* ConditionExpression;
	FScope* ThenScope;
	FScope* ElseScope;
	FScope* NextScope;

	virtual bool Prepare(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitShader(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const override;
};

class FStatementLoop : public FStatement
{
public:
	FStatement* BreakStatement;
	FScope* LoopScope;
	FScope* NextScope;

	virtual bool IsLoop() const override { return true; }
	virtual bool Prepare(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitShader(FEmitContext& Context, FEmitScope& Scope) const override;
	virtual void EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const override;
};

}
}
