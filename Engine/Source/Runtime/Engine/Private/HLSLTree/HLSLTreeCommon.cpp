// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "MaterialSceneTextureId.h"
#include "Engine/BlendableInterface.h" // BL_AfterTonemapping
#include "Engine/Texture.h"

namespace UE
{
namespace HLSLTree
{

struct FPreshaderLoopScope
{
	const FStatement* BreakStatement = nullptr;
	Shader::FPreshaderLabel BreakLabel;
};

FExternalInputDescription GetExternalInputDescription(EExternalInput Input)
{
	switch (Input)
	{
	case EExternalInput::None: return FExternalInputDescription(TEXT("None"), Shader::EValueType::Void);

	case EExternalInput::TexCoord0: return FExternalInputDescription(TEXT("TexCoord0"), Shader::EValueType::Float2, EExternalInput::TexCoord0_Ddx, EExternalInput::TexCoord0_Ddy);
	case EExternalInput::TexCoord1: return FExternalInputDescription(TEXT("TexCoord1"), Shader::EValueType::Float2, EExternalInput::TexCoord1_Ddx, EExternalInput::TexCoord1_Ddy);
	case EExternalInput::TexCoord2: return FExternalInputDescription(TEXT("TexCoord2"), Shader::EValueType::Float2, EExternalInput::TexCoord2_Ddx, EExternalInput::TexCoord2_Ddy);
	case EExternalInput::TexCoord3: return FExternalInputDescription(TEXT("TexCoord3"), Shader::EValueType::Float2, EExternalInput::TexCoord3_Ddx, EExternalInput::TexCoord3_Ddy);
	case EExternalInput::TexCoord4: return FExternalInputDescription(TEXT("TexCoord4"), Shader::EValueType::Float2, EExternalInput::TexCoord4_Ddx, EExternalInput::TexCoord4_Ddy);
	case EExternalInput::TexCoord5: return FExternalInputDescription(TEXT("TexCoord5"), Shader::EValueType::Float2, EExternalInput::TexCoord5_Ddx, EExternalInput::TexCoord5_Ddy);
	case EExternalInput::TexCoord6: return FExternalInputDescription(TEXT("TexCoord6"), Shader::EValueType::Float2, EExternalInput::TexCoord6_Ddx, EExternalInput::TexCoord6_Ddy);
	case EExternalInput::TexCoord7: return FExternalInputDescription(TEXT("TexCoord7"), Shader::EValueType::Float2, EExternalInput::TexCoord7_Ddx, EExternalInput::TexCoord7_Ddy);

	case EExternalInput::TexCoord0_Ddx: return FExternalInputDescription(TEXT("TexCoord0_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord1_Ddx: return FExternalInputDescription(TEXT("TexCoord1_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord2_Ddx: return FExternalInputDescription(TEXT("TexCoord2_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord3_Ddx: return FExternalInputDescription(TEXT("TexCoord3_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord4_Ddx: return FExternalInputDescription(TEXT("TexCoord4_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord5_Ddx: return FExternalInputDescription(TEXT("TexCoord5_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord6_Ddx: return FExternalInputDescription(TEXT("TexCoord6_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord7_Ddx: return FExternalInputDescription(TEXT("TexCoord7_Ddx"), Shader::EValueType::Float2);

	case EExternalInput::TexCoord0_Ddy: return FExternalInputDescription(TEXT("TexCoord0_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord1_Ddy: return FExternalInputDescription(TEXT("TexCoord1_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord2_Ddy: return FExternalInputDescription(TEXT("TexCoord2_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord3_Ddy: return FExternalInputDescription(TEXT("TexCoord3_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord4_Ddy: return FExternalInputDescription(TEXT("TexCoord4_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord5_Ddy: return FExternalInputDescription(TEXT("TexCoord5_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord6_Ddy: return FExternalInputDescription(TEXT("TexCoord6_Ddy"), Shader::EValueType::Float2);
	case EExternalInput::TexCoord7_Ddy: return FExternalInputDescription(TEXT("TexCoord7_Ddy"), Shader::EValueType::Float2);

	case EExternalInput::LightmapTexCoord: return FExternalInputDescription(TEXT("LightmapTexCoord"), Shader::EValueType::Float2, EExternalInput::LightmapTexCoord_Ddx, EExternalInput::LightmapTexCoord_Ddy);
	case EExternalInput::LightmapTexCoord_Ddx: return FExternalInputDescription(TEXT("LightmapTexCoord_Ddx"), Shader::EValueType::Float2);
	case EExternalInput::LightmapTexCoord_Ddy: return FExternalInputDescription(TEXT("LightmapTexCoord_Ddy"), Shader::EValueType::Float2);

	case EExternalInput::TwoSidedSign: return FExternalInputDescription(TEXT("TwoSidedSign"), Shader::EValueType::Float1);
	case EExternalInput::VertexColor: return FExternalInputDescription(TEXT("VertexColor"), Shader::EValueType::Float4, EExternalInput::VertexColor_Ddx, EExternalInput::VertexColor_Ddy);
	case EExternalInput::VertexColor_Ddx: return FExternalInputDescription(TEXT("VertexColor_Ddx"), Shader::EValueType::Float4);
	case EExternalInput::VertexColor_Ddy: return FExternalInputDescription(TEXT("VertexColor_Ddy"), Shader::EValueType::Float4);

	case EExternalInput::WorldPosition: return FExternalInputDescription(TEXT("WorldPosition"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevWorldPosition);
	case EExternalInput::WorldPosition_NoOffsets: return FExternalInputDescription(TEXT("WorldPosition_NoOffsets"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevWorldPosition_NoOffsets);
	case EExternalInput::TranslatedWorldPosition: return FExternalInputDescription(TEXT("TranslatedWorldPosition"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevTranslatedWorldPosition);
	case EExternalInput::TranslatedWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("TranslatedWorldPosition_NoOffsets"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevTranslatedWorldPosition_NoOffsets);
	case EExternalInput::ActorWorldPosition: return FExternalInputDescription(TEXT("TranslatedWorldPosition_NoOffsets"), Shader::EValueType::Double3);

	case EExternalInput::PrevWorldPosition: return FExternalInputDescription(TEXT("PrevWorldPosition"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::PrevWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("PrevWorldPosition_NoOffsets"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::PrevTranslatedWorldPosition: return FExternalInputDescription(TEXT("PrevTranslatedWorldPosition"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::PrevTranslatedWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("PrevTranslatedWorldPosition_NoOffsets"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);

	case EExternalInput::WorldPosition_Ddx: return FExternalInputDescription(TEXT("WorldPosition_Ddx"), Shader::EValueType::Float3);
	case EExternalInput::WorldPosition_Ddy: return FExternalInputDescription(TEXT("WorldPosition_Ddx"), Shader::EValueType::Float3);

	case EExternalInput::WorldNormal: return FExternalInputDescription(TEXT("WorldNormal"), Shader::EValueType::Float3);
	case EExternalInput::WorldReflection: return FExternalInputDescription(TEXT("WorldReflection"), Shader::EValueType::Float3);
	case EExternalInput::WorldVertexNormal: return FExternalInputDescription(TEXT("WorldVertexNormal"), Shader::EValueType::Float3);
	case EExternalInput::WorldVertexTangent: return FExternalInputDescription(TEXT("WorldVertexTangent"), Shader::EValueType::Float3);

	case EExternalInput::ViewportUV: return FExternalInputDescription(TEXT("ViewportUV"), Shader::EValueType::Float2);
	case EExternalInput::PixelPosition: return FExternalInputDescription(TEXT("PixelPosition"), Shader::EValueType::Float2);
	case EExternalInput::ViewSize: return FExternalInputDescription(TEXT("ViewSize"), Shader::EValueType::Float2);
	case EExternalInput::RcpViewSize: return FExternalInputDescription(TEXT("RcpViewSize"), Shader::EValueType::Float2);
	case EExternalInput::FieldOfView: return FExternalInputDescription(TEXT("FieldOfView"), Shader::EValueType::Float1, EExternalInput::None, EExternalInput::None, EExternalInput::PrevFieldOfView);
	case EExternalInput::TanHalfFieldOfView: return FExternalInputDescription(TEXT("TanHalfFieldOfView"), Shader::EValueType::Float2, EExternalInput::None, EExternalInput::None, EExternalInput::PrevTanHalfFieldOfView);
	case EExternalInput::CotanHalfFieldOfView: return FExternalInputDescription(TEXT("CotanHalfFieldOfView"), Shader::EValueType::Float2, EExternalInput::None, EExternalInput::None, EExternalInput::PrevCotanHalfFieldOfView);
	case EExternalInput::TemporalSampleCount: return FExternalInputDescription(TEXT("TemporalSampleCount"), Shader::EValueType::Float1);
	case EExternalInput::TemporalSampleIndex: return FExternalInputDescription(TEXT("TemporalSampleIndex"), Shader::EValueType::Float1);
	case EExternalInput::TemporalSampleOffset: return FExternalInputDescription(TEXT("TemporalSampleOffset"), Shader::EValueType::Float2);
	case EExternalInput::PreExposure: return FExternalInputDescription(TEXT("PreExposure"), Shader::EValueType::Float1);
	case EExternalInput::RcpPreExposure: return FExternalInputDescription(TEXT("RcpPreExposure"), Shader::EValueType::Float1);
	case EExternalInput::RuntimeVirtualTextureOutputLevel: return FExternalInputDescription(TEXT("RuntimeVirtualTextureOutputLevel"), Shader::EValueType::Float1);
	case EExternalInput::RuntimeVirtualTextureOutputDerivative: return FExternalInputDescription(TEXT("RuntimeVirtualTextureOutputDerivative"), Shader::EValueType::Float2);
	case EExternalInput::RuntimeVirtualTextureMaxLevel: return FExternalInputDescription(TEXT("RuntimeVirtualTextureMaxLevel"), Shader::EValueType::Float1);

	case EExternalInput::CameraVector: return FExternalInputDescription(TEXT("CameraVector"), Shader::EValueType::Float3);
	case EExternalInput::CameraWorldPosition: return FExternalInputDescription(TEXT("CameraWorldPosition"), Shader::EValueType::Double3, EExternalInput::None, EExternalInput::None, EExternalInput::PrevCameraWorldPosition);
	case EExternalInput::ViewWorldPosition: return FExternalInputDescription(TEXT("ViewWorldPosition"), Shader::EValueType::Double3, EExternalInput::None, EExternalInput::None, EExternalInput::PrevViewWorldPosition);
	case EExternalInput::PreViewTranslation: return FExternalInputDescription(TEXT("PreViewTranslation"), Shader::EValueType::Double3, EExternalInput::None, EExternalInput::None, EExternalInput::PrevPreViewTranslation);
	case EExternalInput::TangentToWorld: return FExternalInputDescription(TEXT("TangentToWorld"), Shader::EValueType::Float4x4);
	case EExternalInput::LocalToWorld: return FExternalInputDescription(TEXT("LocalToWorld"), Shader::EValueType::Double4x4, EExternalInput::None, EExternalInput::None, EExternalInput::PrevLocalToWorld);
	case EExternalInput::WorldToLocal: return FExternalInputDescription(TEXT("WorldToLocal"), Shader::EValueType::DoubleInverse4x4, EExternalInput::None, EExternalInput::None, EExternalInput::PrevWorldToLocal);
	case EExternalInput::TranslatedWorldToCameraView: return FExternalInputDescription(TEXT("TranslatedWorldToCameraView"), Shader::EValueType::Float4x4, EExternalInput::None, EExternalInput::None, EExternalInput::PrevTranslatedWorldToCameraView);
	case EExternalInput::TranslatedWorldToView: return FExternalInputDescription(TEXT("TranslatedWorldToView"), Shader::EValueType::Float4x4, EExternalInput::None, EExternalInput::None, EExternalInput::PrevTranslatedWorldToView);
	case EExternalInput::CameraViewToTranslatedWorld: return FExternalInputDescription(TEXT("CameraViewToTranslatedWorld"), Shader::EValueType::Float4x4, EExternalInput::None, EExternalInput::None, EExternalInput::PrevCameraViewToTranslatedWorld);
	case EExternalInput::ViewToTranslatedWorld: return FExternalInputDescription(TEXT("ViewToTranslatedWorld"), Shader::EValueType::Float4x4, EExternalInput::None, EExternalInput::None, EExternalInput::PrevViewToTranslatedWorld);
	case EExternalInput::WorldToParticle: return FExternalInputDescription(TEXT("WorldToParticle"), Shader::EValueType::DoubleInverse4x4);
	case EExternalInput::WorldToInstance: return FExternalInputDescription(TEXT("WorldToInstance"), Shader::EValueType::DoubleInverse4x4);
	case EExternalInput::ParticleToWorld: return FExternalInputDescription(TEXT("ParticleToWorld"), Shader::EValueType::Double4x4);
	case EExternalInput::InstanceToWorld: return FExternalInputDescription(TEXT("InstanceToWorld"), Shader::EValueType::Double4x4);

	case EExternalInput::PrevFieldOfView: return FExternalInputDescription(TEXT("PrevFieldOfView"), Shader::EValueType::Float2);
	case EExternalInput::PrevTanHalfFieldOfView: return FExternalInputDescription(TEXT("PrevTanHalfFieldOfView"), Shader::EValueType::Float2);
	case EExternalInput::PrevCotanHalfFieldOfView: return FExternalInputDescription(TEXT("PrevCotanHalfFieldOfView"), Shader::EValueType::Float2);
	case EExternalInput::PrevCameraWorldPosition: return FExternalInputDescription(TEXT("PrevCameraWorldPosition"), Shader::EValueType::Double3);
	case EExternalInput::PrevViewWorldPosition: return FExternalInputDescription(TEXT("PrevViewWorldPosition"), Shader::EValueType::Double3);
	case EExternalInput::PrevPreViewTranslation: return FExternalInputDescription(TEXT("PrevPreViewTranslation"), Shader::EValueType::Double3);
	case EExternalInput::PrevLocalToWorld: return FExternalInputDescription(TEXT("PrevLocalToWorld"), Shader::EValueType::Double4x4);
	case EExternalInput::PrevWorldToLocal: return FExternalInputDescription(TEXT("PrevWorldToLocal"), Shader::EValueType::DoubleInverse4x4);
	case EExternalInput::PrevTranslatedWorldToCameraView: return FExternalInputDescription(TEXT("PrevTranslatedWorldToCameraView"), Shader::EValueType::Float4x4);
	case EExternalInput::PrevTranslatedWorldToView: return FExternalInputDescription(TEXT("PrevTranslatedWorldToView"), Shader::EValueType::Float4x4);
	case EExternalInput::PrevCameraViewToTranslatedWorld: return FExternalInputDescription(TEXT("PrevCameraViewToTranslatedWorld"), Shader::EValueType::Float4x4);
	case EExternalInput::PrevViewToTranslatedWorld: return FExternalInputDescription(TEXT("PrevViewToTranslatedWorld"), Shader::EValueType::Float4x4);

	case EExternalInput::PixelDepth: return FExternalInputDescription(TEXT("PixelDepth"), Shader::EValueType::Float1, EExternalInput::PixelDepth_Ddx, EExternalInput::PixelDepth_Ddy);
	case EExternalInput::PixelDepth_Ddx: return FExternalInputDescription(TEXT("PixelDepth_Ddx"), Shader::EValueType::Float1);
	case EExternalInput::PixelDepth_Ddy: return FExternalInputDescription(TEXT("PixelDepth_Ddy"), Shader::EValueType::Float1);

	case EExternalInput::GameTime: return FExternalInputDescription(TEXT("GameTime"), Shader::EValueType::Float1, EExternalInput::None, EExternalInput::None, EExternalInput::PrevGameTime);
	case EExternalInput::RealTime: return FExternalInputDescription(TEXT("RealTime"), Shader::EValueType::Float1, EExternalInput::None, EExternalInput::None, EExternalInput::PrevRealTime);
	case EExternalInput::DeltaTime: return FExternalInputDescription(TEXT("DeltaTime"), Shader::EValueType::Float1);

	case EExternalInput::PrevGameTime: return FExternalInputDescription(TEXT("PrevGameTime"), Shader::EValueType::Float1);
	case EExternalInput::PrevRealTime: return FExternalInputDescription(TEXT("PrevRealTime"), Shader::EValueType::Float1);

	case EExternalInput::ParticleColor: return FExternalInputDescription(TEXT("ParticleColor"), Shader::EValueType::Float4);
	case EExternalInput::ParticleTranslatedWorldPosition: return FExternalInputDescription(TEXT("ParticleTranslatedWorldPosition"), Shader::EValueType::Float3);
	case EExternalInput::ParticleRadius: return FExternalInputDescription(TEXT("ParticleRadius"), Shader::EValueType::Float1);

	default: checkNoEntry(); return FExternalInputDescription(TEXT("Invalid"), Shader::EValueType::Void);
	}
}

FSwizzleParameters::FSwizzleParameters(int8 InR, int8 InG, int8 InB, int8 InA) : NumComponents(0)
{
	ComponentIndex[0] = InR;
	ComponentIndex[1] = InG;
	ComponentIndex[2] = InB;
	ComponentIndex[3] = InA;

	if (InA >= 0)
	{
		check(InA <= 3);
		++NumComponents;
		check(InB >= 0);
	}
	if (InB >= 0)
	{
		check(InB <= 3);
		++NumComponents;
		check(InG >= 0);
	}

	if (InG >= 0)
	{
		check(InG <= 3);
		++NumComponents;
	}

	// At least one proper index
	check(InR >= 0 && InR <= 3);
	++NumComponents;
}

FRequestedType FSwizzleParameters::GetRequestedInputType(const FRequestedType& RequestedType) const
{
	FRequestedType RequestedInputType;
	for (int32 Index = 0; Index < NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			const int32 SwizzledComponentIndex = ComponentIndex[Index];
			RequestedInputType.SetComponentRequest(SwizzledComponentIndex);
		}
	}
	return RequestedInputType;
}

FSwizzleParameters MakeSwizzleMask(bool bInR, bool bInG, bool bInB, bool bInA)
{
	int8 ComponentIndex[4] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	int8 CurrentComponent = 0;
	if (bInR)
	{
		ComponentIndex[CurrentComponent++] = 0;
	}
	if (bInG)
	{
		ComponentIndex[CurrentComponent++] = 1;
	}
	if (bInB)
	{
		ComponentIndex[CurrentComponent++] = 2;
	}
	if (bInA)
	{
		ComponentIndex[CurrentComponent++] = 3;
	}
	return FSwizzleParameters(ComponentIndex[0], ComponentIndex[1], ComponentIndex[2], ComponentIndex[3]);
}

FExpression* FTree::NewConstant(const Shader::FValue& Value)
{
	return NewExpression<FExpressionConstant>(Value);
}

void FExpressionConstant::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const Shader::FType DerivativeType = Value.Type.GetDerivativeType();
	if (!DerivativeType.IsVoid())
	{
		const Shader::FValue ZeroValue(DerivativeType);
		OutResult.ExpressionDdx = Tree.NewConstant(ZeroValue);
		OutResult.ExpressionDdy = OutResult.ExpressionDdx;
	}
}

bool FExpressionConstant::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Constant, Value.Type);
}

void FExpressionConstant::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	Context.PreshaderStackPosition++;
	OutResult.Type = Value.Type;
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
}

void FExpressionMaterialShadingModel::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const Shader::FValue ZeroValue(Shader::EValueType::Float1);
	OutResult.ExpressionDdx = Tree.NewConstant(ZeroValue);
	OutResult.ExpressionDdy = OutResult.ExpressionDdx;
}

bool FExpressionMaterialShadingModel::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Constant, Shader::EValueType::Int1);
}

void FExpressionMaterialShadingModel::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	Context.ShadingModelsFromCompilation.AddShadingModel(ShadingModel);

	Context.PreshaderStackPosition++;
	OutResult.Type = Shader::EValueType::Int1;
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Shader::FValue((int32)ShadingModel));
}

void FExpressionMaterialParameter::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const Shader::FType Type = GetShaderValueType(ParameterType);
	const Shader::FType DerivativeType = Type.GetDerivativeType();
	if (!DerivativeType.IsVoid())
	{
		const Shader::FValue ZeroValue(DerivativeType);
		OutResult.ExpressionDdx = Tree.NewConstant(ZeroValue);
		OutResult.ExpressionDdy = OutResult.ExpressionDdx;
	}
}

bool FExpressionMaterialParameter::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	EExpressionEvaluation Evaluation = EExpressionEvaluation::Shader;
	if (IsStaticMaterialParameter(ParameterType))
	{
		Evaluation = EExpressionEvaluation::Constant;
	}
	else if (ParameterType == EMaterialParameterType::Scalar ||
		ParameterType == EMaterialParameterType::Vector ||
		ParameterType == EMaterialParameterType::DoubleVector)
	{
		Evaluation = EExpressionEvaluation::Preshader;
	}
	return OutResult.SetType(Context, RequestedType, Evaluation, DefaultValue.Type);
}

void FExpressionMaterialParameter::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	if (ParameterType == EMaterialParameterType::Texture)
	{
		const Shader::FTextureValue* TextureValue = DefaultValue.AsTexture();
		check(TextureValue);

		const Shader::EValueType TextureType = TextureValue->GetType();
		const TCHAR* ConstructorName = nullptr;
		const TCHAR* TextureTypeName = nullptr;
		int32 ParameterIndex = INDEX_NONE;

		if (TextureType == Shader::EValueType::TextureExternal)
		{
			check(TextureValue->SamplerType == SAMPLERTYPE_External);

			ConstructorName = TEXT("MakeTextureExternal");
			TextureTypeName = TEXT("ExternalTexture");

			FMaterialExternalTextureParameterInfo TextureParameterInfo;
			TextureParameterInfo.ParameterName = NameToScriptName(ParameterName);
			TextureParameterInfo.ExternalTextureGuid = TextureValue->ExternalTextureGuid;
			if (TextureValue->Texture)
			{
				TextureParameterInfo.SourceTextureIndex = Context.Material->GetReferencedTextures().Find(TextureValue->Texture);
			}
			ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddExternalTextureParameter(TextureParameterInfo);
		}
		else
		{
			EMaterialTextureParameterType TextureParameterType = EMaterialTextureParameterType::Count;
			switch (TextureType)
			{
			case Shader::EValueType::Texture2D:
				TextureParameterType = EMaterialTextureParameterType::Standard2D;
				ConstructorName = TEXT("MakeTexture2D");
				TextureTypeName = TEXT("Texture2D");
				break;
			case Shader::EValueType::Texture2DArray:
				TextureParameterType = EMaterialTextureParameterType::Array2D;
				ConstructorName = TEXT("MakeTexture2DArray");
				TextureTypeName = TEXT("Texture2DArray");
				break;
			case Shader::EValueType::TextureCube:
				TextureParameterType = EMaterialTextureParameterType::Cube;
				ConstructorName = TEXT("MakeTextureCube");
				TextureTypeName = TEXT("TextureCube");
				break;
			case Shader::EValueType::TextureCubeArray:
				TextureParameterType = EMaterialTextureParameterType::ArrayCube;
				ConstructorName = TEXT("MakeTextureCubeArray");
				TextureTypeName = TEXT("TextureCubeArray");
				break;
			case Shader::EValueType::Texture3D:
				TextureParameterType = EMaterialTextureParameterType::Volume;
				ConstructorName = TEXT("MakeTexture3D");
				TextureTypeName = TEXT("VolumeTexture");
				break;
			default:
				checkNoEntry();
				break;
			}

			FMaterialTextureParameterInfo TextureParameterInfo;
			TextureParameterInfo.ParameterInfo = ParameterName;
			TextureParameterInfo.TextureIndex = Context.Material->GetReferencedTextures().Find(TextureValue->Texture);
			TextureParameterInfo.SamplerSource = SSM_FromTextureAsset; // TODO - Is this needed?
			check(TextureParameterInfo.TextureIndex != INDEX_NONE);
			ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddTextureParameter(TextureParameterType, TextureParameterInfo);
		}

		TStringBuilder<256> FormattedCode;
		FormattedCode.Appendf(TEXT("%s(Material.%s_%d, Material.%s_%dSampler, %d)"), ConstructorName, TextureTypeName, ParameterIndex, TextureTypeName, ParameterIndex, (int32)TextureValue->SamplerType);
		OutResult.Code = Context.EmitExpression(Scope, TextureType, FormattedCode.ToView());
	}
}

void FExpressionMaterialParameter::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	Context.PreshaderStackPosition++;
	OutResult.Type = GetShaderValueType(ParameterType);
	if (ParameterType == EMaterialParameterType::StaticSwitch)
	{
		const FMaterialParameterInfo ParameterInfo(ParameterName);
		Shader::FValue Value = DefaultValue;
		for (const FStaticSwitchParameter& Parameter : Context.StaticParameters->StaticSwitchParameters)
		{
			if (Parameter.ParameterInfo == ParameterInfo)
			{
				Value = Parameter.Value;
				break;
			}
		}
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Constant).Write(Value);
	}
	else
	{
		const uint32* PrevDefaultOffset = Context.DefaultUniformValues.Find(DefaultValue);
		uint32 DefaultOffset;
		if (PrevDefaultOffset)
		{
			DefaultOffset = *PrevDefaultOffset;
		}
		else
		{
			DefaultOffset = Context.MaterialCompilationOutput->UniformExpressionSet.AddDefaultParameterValue(DefaultValue);
			Context.DefaultUniformValues.Add(DefaultValue, DefaultOffset);
		}
		const int32 ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddNumericParameter(ParameterType, ParameterName, DefaultOffset);
		check(ParameterIndex >= 0 && ParameterIndex <= 0xffff);
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::Parameter).Write((uint16)ParameterIndex);
	}
}

void FExpressionExternalInput::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);
	if (InputDesc.Ddx != EExternalInput::None)
	{
		check(InputDesc.Ddy != EExternalInput::None);
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionExternalInput>(InputDesc.Ddx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionExternalInput>(InputDesc.Ddy);
	}
	else
	{
		switch (InputType)
		{
		case EExternalInput::ViewportUV:
		{
			// Ddx = float2(RcpViewSize.x, 0.0f)
			// Ddy = float2(0.0f, RcpViewSize.y)
			FExpression* RcpViewSize = Tree.NewExpression<FExpressionExternalInput>(EExternalInput::RcpViewSize);
			FExpression* Constant0 = Tree.NewConstant(0.0f);
			OutResult.ExpressionDdx = Tree.NewExpression<FExpressionAppend>(Tree.NewExpression<FExpressionSwizzle>(MakeSwizzleMask(true, false, false, false), RcpViewSize), Constant0);
			OutResult.ExpressionDdy = Tree.NewExpression<FExpressionAppend>(Constant0, Tree.NewExpression<FExpressionSwizzle>(MakeSwizzleMask(false, true, false, false), RcpViewSize));
			break;
		}
		default:
			break;
		}
	}
}

FExpression* FExpressionExternalInput::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	if (InputType == EExternalInput::ActorWorldPosition)
	{
		return Tree.NewBinaryOp(EOperation::VecMulMatrix3, Tree.NewBinaryOp(EOperation::VecMulMatrix3,
			Tree.NewExpression<FExpressionExternalInput>(EExternalInput::ActorWorldPosition),
			Tree.NewExpression< FExpressionExternalInput>(EExternalInput::WorldToLocal)),
			Tree.NewExpression< FExpressionExternalInput>(EExternalInput::PrevLocalToWorld));
	}

	const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);
	if (InputDesc.PreviousFrame != EExternalInput::None)
	{
		return Tree.NewExpression<FExpressionExternalInput>(InputDesc.PreviousFrame);
	}
	return nullptr;
}

bool FExpressionExternalInput::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);

	switch (InputType)
	{
	case EExternalInput::WorldNormal:
	case EExternalInput::WorldReflection:
		Context.bReadMaterialNormal = true;
		break;
	default:
		break;
	}

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, InputDesc.Type);
}

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const int32 TypeIndex = (int32)InputType;
	Context.ExternalInputMask[Context.ShaderFrequency][TypeIndex] = true;
	if (IsTexCoord(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0;
		OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float2, TEXT("Parameters.TexCoords[%].xy"), TexCoordIndex);
	}
	else if (IsTexCoord_Ddx(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0_Ddx;
		OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDX[%].xy"), TexCoordIndex);
	}
	else if (IsTexCoord_Ddy(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0_Ddy;
		OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float2, TEXT("Parameters.TexCoords_DDY[%].xy"), TexCoordIndex);
	}
	else
	{
		const FExternalInputDescription InputDesc = GetExternalInputDescription(InputType);
		const TCHAR* Code = nullptr;
		switch (InputType)
		{
		case EExternalInput::LightmapTexCoord: Code = TEXT("GetLightmapUVs(Parameters)"); break;
		case EExternalInput::LightmapTexCoord_Ddx: Code = TEXT("GetLightmapUVs_DDX(Parameters)"); break;
		case EExternalInput::LightmapTexCoord_Ddy: Code = TEXT("GetLightmapUVs_DDY(Parameters)"); break;
		case EExternalInput::TwoSidedSign: Code = TEXT("Parameters.TwoSidedSign"); break;
		case EExternalInput::VertexColor: Code = TEXT("Parameters.VertexColor"); Context.bUsesVertexColor |= (Context.ShaderFrequency != SF_Vertex); break;
		case EExternalInput::VertexColor_Ddx: Code = TEXT("Parameters.VertexColor_DDX"); Context.bUsesVertexColor |= (Context.ShaderFrequency != SF_Vertex); break;
		case EExternalInput::VertexColor_Ddy: Code = TEXT("Parameters.VertexColor_DDY"); Context.bUsesVertexColor |= (Context.ShaderFrequency != SF_Vertex); break;
		case EExternalInput::WorldPosition: Code = TEXT("GetWorldPosition(Parameters)"); break;
		case EExternalInput::WorldPosition_NoOffsets: Code = TEXT("GetWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::TranslatedWorldPosition: Code = TEXT("GetTranslatedWorldPosition(Parameters)"); break;
		case EExternalInput::TranslatedWorldPosition_NoOffsets: Code = TEXT("GetTranslatedWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::ActorWorldPosition: Code = TEXT("GetActorWorldPosition(Parameters)"); break;
		case EExternalInput::PrevWorldPosition: Code = TEXT("GetPrevWorldPosition(Parameters)"); break;
		case EExternalInput::PrevWorldPosition_NoOffsets: Code = TEXT("GetPrevWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::PrevTranslatedWorldPosition: Code = TEXT("GetPrevTranslatedWorldPosition(Parameters)"); break;
		case EExternalInput::PrevTranslatedWorldPosition_NoOffsets: Code = TEXT("GetPrevTranslatedWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::WorldPosition_Ddx: Code = TEXT("Parameters.WorldPosition_DDX"); break;
		case EExternalInput::WorldPosition_Ddy: Code = TEXT("Parameters.WorldPosition_DDY"); break;

		case EExternalInput::WorldNormal: Code = TEXT("Parameters.WorldNormal"); break;
		case EExternalInput::WorldReflection: Code = TEXT("Parameters.ReflectionVector"); break;
		case EExternalInput::WorldVertexNormal: Code = TEXT("Parameters.TangentToWorld[2]"); break;
		case EExternalInput::WorldVertexTangent: Code = TEXT("Parameters.TangentToWorld[0]"); break;

		case EExternalInput::ViewportUV: Code = TEXT("GetViewportUV(Parameters)"); break;
		case EExternalInput::PixelPosition: Code = TEXT("GetPixelPosition(Parameters)"); break;
		case EExternalInput::ViewSize: Code = TEXT("View.ViewSizeAndInvSize.xy"); break;
		case EExternalInput::RcpViewSize: Code = TEXT("View.ViewSizeAndInvSize.zw"); break;

		case EExternalInput::FieldOfView: Code = TEXT("View.FieldOfViewWideAngles"); break;
		case EExternalInput::TanHalfFieldOfView: Code = TEXT("GetTanHalfFieldOfView()"); break;
		case EExternalInput::CotanHalfFieldOfView: Code = TEXT("GetCotanHalfFieldOfView()"); break;
		case EExternalInput::TemporalSampleCount: Code = TEXT("View.TemporalAAParams.y"); break;
		case EExternalInput::TemporalSampleIndex: Code = TEXT("View.TemporalAAParams.x"); break;
		case EExternalInput::TemporalSampleOffset: Code = TEXT("View.TemporalAAParams.zw"); break;
		case EExternalInput::PreExposure: Code = TEXT("View.PreExposure.x"); break;
		case EExternalInput::RcpPreExposure: Code = TEXT("View.OneOverPreExposure.x"); break;
		case EExternalInput::RuntimeVirtualTextureOutputLevel:  Code = TEXT("View.RuntimeVirtualTextureMipLevel.x"); break;
		case EExternalInput::RuntimeVirtualTextureOutputDerivative: Code = TEXT("View.RuntimeVirtualTextureMipLevel.zw"); break;
		case EExternalInput::RuntimeVirtualTextureMaxLevel:  Code = TEXT("View.RuntimeVirtualTextureMipLevel.y"); break;

		case EExternalInput::CameraVector: Code = TEXT("Parameters.CameraVector"); break;
		case EExternalInput::CameraWorldPosition: Code = TEXT("ResolvedView.WorldCameraOrigin"); break;
		case EExternalInput::ViewWorldPosition: Code = TEXT("ResolvedView.WorldViewOrigin"); break;
		case EExternalInput::PreViewTranslation: Code = TEXT("ResolvedView.PreViewTranslation"); break;
		case EExternalInput::TangentToWorld: Code = TEXT("Parameters.TangentToWorld"); break;
		case EExternalInput::LocalToWorld: Code = TEXT("GetLocalToWorld(Parameters)"); break;
		case EExternalInput::WorldToLocal: Code = TEXT("GetPrimitiveData(Parameters).WorldToLocal"); break;
		case EExternalInput::TranslatedWorldToCameraView: Code = TEXT("ResolvedView.TranslatedWorldToCameraView"); break;
		case EExternalInput::TranslatedWorldToView: Code = TEXT("ResolvedView.TranslatedWorldToView"); break;
		case EExternalInput::CameraViewToTranslatedWorld: Code = TEXT("ResolvedView.CameraViewToTranslatedWorld"); break;
		case EExternalInput::ViewToTranslatedWorld: Code = TEXT("ResolvedView.ViewToTranslatedWorld"); break;
		case EExternalInput::WorldToParticle: Code = TEXT("Parameters.Particle.WorldToParticle"); break;
		case EExternalInput::WorldToInstance: Code = TEXT("GetWorldToInstance(Parameters)"); break;
		case EExternalInput::ParticleToWorld: Code = TEXT("Parameters.Particle.ParticleToWorld"); break;
		case EExternalInput::InstanceToWorld: Code = TEXT("GetInstanceToWorld(Parameters)"); break;

		case EExternalInput::PrevFieldOfView: Code = TEXT("View.PrevFieldOfViewWideAngles"); break;
		case EExternalInput::PrevTanHalfFieldOfView: Code = TEXT("GetPrevTanHalfFieldOfView()"); break;
		case EExternalInput::PrevCotanHalfFieldOfView: Code = TEXT("GetPrevCotanHalfFieldOfView()"); break;
		case EExternalInput::PrevCameraWorldPosition: Code = TEXT("ResolvedView.PrevWorldCameraOrigin"); break;
		case EExternalInput::PrevViewWorldPosition: Code = TEXT("ResolvedView.PrevWorldViewOrigin"); break;
		case EExternalInput::PrevPreViewTranslation: Code = TEXT("ResolvedView.PrevPreViewTranslation"); break;
		case EExternalInput::PrevLocalToWorld: Code = TEXT("GetPrevLocalToWorld(Parameters)"); break;
		case EExternalInput::PrevWorldToLocal: Code = TEXT("GetPrimitiveData(Parameters).PreviousWorldToLocal"); break;
		case EExternalInput::PrevTranslatedWorldToCameraView: Code = TEXT("ResolvedView.PrevTranslatedWorldToCameraView"); break;
		case EExternalInput::PrevTranslatedWorldToView: Code = TEXT("ResolvedView.PrevTranslatedWorldToView"); break;
		case EExternalInput::PrevCameraViewToTranslatedWorld: Code = TEXT("ResolvedView.PrevCameraViewToTranslatedWorld"); break;
		case EExternalInput::PrevViewToTranslatedWorld: Code = TEXT("ResolvedView.PrevViewToTranslatedWorld"); break;

		case EExternalInput::PixelDepth: Code = TEXT("GetPixelDepth(Parameters)"); break;
		case EExternalInput::PixelDepth_Ddx: Code = TEXT("Parameters.ScreenPosition_DDX.w"); break;
		case EExternalInput::PixelDepth_Ddy: Code = TEXT("Parameters.ScreenPosition_DDY.w"); break;
		case EExternalInput::GameTime: Code = TEXT("View.GameTime"); break;
		case EExternalInput::RealTime: Code = TEXT("View.RealTime"); break;
		case EExternalInput::DeltaTime: Code = TEXT("View.DeltaTime"); break;
		case EExternalInput::PrevGameTime: Code = TEXT("View.PrevFrameGameTime"); break;
		case EExternalInput::PrevRealTime: Code = TEXT("View.PrevFrameRealTime"); break;

		case EExternalInput::ParticleColor: Code = TEXT("Parameters.Particle.Color"); Context.bUsesParticleColor |= (Context.ShaderFrequency != SF_Vertex); break;
		case EExternalInput::ParticleTranslatedWorldPosition: Code = TEXT("Parameters.Particle.TranslatedWorldPositionAndSize.xyz"); Context.bNeedsParticlePosition = true; break;
		case EExternalInput::ParticleRadius: Code = TEXT("Parameters.Particle.TranslatedWorldPositionAndSize.w"); Context.bNeedsParticlePosition = true; break;

		default:
			checkNoEntry();
			break;
		}
		OutResult.Code = Context.EmitInlineExpression(Scope, InputDesc.Type, Code);
	}
}

bool FExpressionMaterialSceneTexture::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.PrepareExpression(TexCoordExpression, Scope, ERequestedType::Vector2);

	Context.MaterialCompilationOutput->bNeedsSceneTextures = true;
	Context.MaterialCompilationOutput->SetIsSceneTextureUsed((ESceneTextureId)SceneTextureId);

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionMaterialSceneTexture::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const bool bSupportedOnMobile = SceneTextureId == PPI_PostProcessInput0 ||
		SceneTextureId == PPI_CustomDepth ||
		SceneTextureId == PPI_SceneDepth ||
		SceneTextureId == PPI_CustomStencil;

	FEmitShaderExpression* EmitTexCoord = nullptr;
	if (TexCoordExpression)
	{
		EmitTexCoord = TexCoordExpression->GetValueShader(Context, Scope, Shader::EValueType::Float2);
		EmitTexCoord = Context.EmitExpression(Scope, Shader::EValueType::Float2, TEXT("ClampSceneTextureUV(ViewportUVToSceneTextureUV(%, %), %)"), EmitTexCoord, (int)SceneTextureId, (int)SceneTextureId);
	}
	else
	{
		EmitTexCoord = Context.EmitExpression(Scope, Shader::EValueType::Float2, TEXT("GetDefaultSceneTextureUV(Parameters, %)"), (int)SceneTextureId);
	}

	FEmitShaderExpression* EmitLookup = nullptr;
	if (Context.Material->GetFeatureLevel() >= ERHIFeatureLevel::SM5)
	{
		EmitLookup = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("SceneTextureLookup(%, %, %)"), EmitTexCoord, (int)SceneTextureId, bFiltered);
	}
	else
	{
		EmitLookup = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("MobileSceneTextureLookup(Parameters, %, %, %)"), (int)SceneTextureId, EmitTexCoord);
	}

	if (SceneTextureId == PPI_PostProcessInput0 && Context.Material->GetMaterialDomain() == MD_PostProcess && Context.Material->GetBlendableLocation() != BL_AfterTonemapping)
	{
		EmitLookup = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("(float4(View.OneOverPreExposure.xxx, 1) * %)"), EmitLookup);
	}
	
	OutResult.Code = EmitLookup;
}

bool FExpressionMaterialNoise::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& PositionType = Context.PrepareExpression(PositionExpression, Scope, ERequestedType::Vector3);
	const FPreparedType& FilterWidthType = Context.PrepareExpression(FilterWidthExpression, Scope, ERequestedType::Scalar);
	if (PositionType.IsVoid() || FilterWidthType.IsVoid())
	{
		return false;
	}
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float1);
}

void FExpressionMaterialNoise::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	bool bIsLWC = Shader::IsLWCType(PositionExpression->GetPreparedType().ValueComponentType);
	FEmitShaderExpression* EmitPosition = PositionExpression->GetValueShader(Context, Scope, bIsLWC ? Shader::EValueType::Double3 : Shader::EValueType::Float3);
	FEmitShaderExpression* EmitFilterWidth = FilterWidthExpression->GetValueShader(Context, Scope, Shader::EValueType::Float1);

	if (bIsLWC)
	{
		// If Noise is driven by a LWC position, just take the offset within the current tile
		// Will generate discontinuity in noise at tile boudaries
		// Could potentially add noise functions that operate directly on LWC values, but that would be very expensive
		EmitPosition = Context.EmitExpression(Scope, Shader::EValueType::Float3, TEXT("LWCNormalizeTile(%).Offset"), EmitPosition);
	}

	OutResult.Code = Context.EmitExpression(Scope, Shader::EValueType::Float1, TEXT("MaterialExpressionNoise(%,%,%,%,%,%,%,%,%,%,%,%)"),
		EmitPosition,
		Parameters.Scale,
		Parameters.Quality,
		Parameters.NoiseFunction,
		Parameters.bTurbulence,
		Parameters.Levels,
		Parameters.OutputMin,
		Parameters.OutputMax,
		Parameters.LevelScale,
		EmitFilterWidth,
		Parameters.bTiling,
		Parameters.RepeatSize);
}

namespace Private
{
Shader::EValueType GetTexCoordType(Shader::EValueType TextureType)
{
	switch (TextureType)
	{
	case Shader::EValueType::Texture2D:
	case Shader::EValueType::TextureExternal: return Shader::EValueType::Float2;
	case Shader::EValueType::Texture2DArray:
	case Shader::EValueType::TextureCube:
	case Shader::EValueType::Texture3D: return Shader::EValueType::Float3;
	case Shader::EValueType::TextureCubeArray: return Shader::EValueType::Float4;
	default: checkNoEntry(); return Shader::EValueType::Void;
	}
}
}

bool FExpressionTextureSample::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& TextureType = Context.PrepareExpression(TextureExpression, Scope, ERequestedType::Texture);
	if (!Shader::IsTextureType(TextureType.ValueComponentType))
	{
		return Context.Errors->AddError(TEXT("Expected texture"));
	}

	const FRequestedType RequestedTexCoordType = Private::GetTexCoordType(TextureType.GetType());
	const FPreparedType& TexCoordType = Context.PrepareExpression(TexCoordExpression, Scope, RequestedTexCoordType);
	if (TexCoordType.IsVoid())
	{
		return false;
	}

	Context.PrepareExpression(TexCoordDerivatives.ExpressionDdx, Scope, RequestedTexCoordType);
	Context.PrepareExpression(TexCoordDerivatives.ExpressionDdy, Scope, RequestedTexCoordType);

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* EmitTexture = TextureExpression->GetValueShader(Context, Scope);
	const Shader::EValueType TextureType = EmitTexture->Type;
	check(Shader::IsTextureType(TextureType));

	bool bVirtualTexture = false;
	const TCHAR* SampleFunctionName = nullptr;
	switch (TextureType)
	{
	case Shader::EValueType::Texture2D:
		SampleFunctionName = TEXT("Texture2DSample");
		break;
	case Shader::EValueType::TextureCube:
		SampleFunctionName = TEXT("TextureCubeSample");
		break;
	case Shader::EValueType::Texture2DArray:
		SampleFunctionName = TEXT("Texture2DArraySample");
		break;
	case Shader::EValueType::Texture3D:
		SampleFunctionName = TEXT("Texture3DSample");
		break;
	case Shader::EValueType::TextureExternal:
		SampleFunctionName = TEXT("TextureExternalSample");
		break;
	/*case MCT_TextureVirtual:
		// TODO
		SampleFunctionName = TEXT("TextureVirtualSample");
		bVirtualTexture = true;
		break;*/
	default:
		checkNoEntry();
		break;
	}

	const bool AutomaticViewMipBias = false; // TODO
	TStringBuilder<256> FormattedSampler;
	switch (SamplerSource)
	{
	case SSM_FromTextureAsset:
		FormattedSampler.Appendf(TEXT("%s.Sampler"), EmitTexture->Reference);
		break;
	case SSM_Wrap_WorldGroupSettings:
		FormattedSampler.Appendf(TEXT("GetMaterialSharedSampler(%s.Sampler,%s)"),
			EmitTexture->Reference,
			AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearWrapedSampler") : TEXT("Material.Wrap_WorldGroupSettings"));
		break;
	case SSM_Clamp_WorldGroupSettings:
		FormattedSampler.Appendf(TEXT("GetMaterialSharedSampler(%s.Sampler,%s)"),
			EmitTexture->Reference,
			AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearClampedSampler") : TEXT("Material.Clamp_WorldGroupSettings"));
		break;
	default:
		checkNoEntry();
		break;
	}

	const Shader::EValueType TexCoordType = Private::GetTexCoordType(TextureType);
	FEmitShaderExpression* TexCoordValue = TexCoordExpression->GetValueShader(Context, Scope, TexCoordType);
	FEmitShaderExpression* TexCoordValueDdx = nullptr;
	FEmitShaderExpression* TexCoordValueDdy = nullptr;
	FEmitShaderExpression* TextureResult = nullptr;
	if (TexCoordDerivatives.IsValid())
	{
		TexCoordValueDdx = TexCoordDerivatives.ExpressionDdx->GetValueShader(Context, Scope, TexCoordType);
		TexCoordValueDdy = TexCoordDerivatives.ExpressionDdy->GetValueShader(Context, Scope, TexCoordType);
		TextureResult = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%Grad(%.Texture, %, %, %, %)"),
			SampleFunctionName,
			EmitTexture,
			FormattedSampler.ToString(),
			TexCoordValue,
			TexCoordValueDdx,
			TexCoordValueDdy);
	}
	else
	{
		TextureResult = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%(%.Texture, %, %)"),
			SampleFunctionName,
			EmitTexture,
			FormattedSampler.ToString(),
			TexCoordValue);
	}

	OutResult.Code = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("ApplyMaterialSamplerType(%, %.SamplerType)"), TextureResult, EmitTexture);
}

void FExpressionGetStructField::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives StructDerivatives = Tree.GetAnalyticDerivatives(StructExpression);
	if (StructDerivatives.IsValid())
	{
		const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
		check(DerivativeStructType);
		const Shader::FStructField* DerivativeField = DerivativeStructType->FindFieldByName(Field->Name);
		check(DerivativeField);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionGetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionGetStructField::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);
	return Tree.NewExpression<FExpressionGetStructField>(StructType, Field, Tree.GetPreviousFrame(StructExpression, RequestedStructType));
}

bool FExpressionGetStructField::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);

	FPreparedType StructPreparedType = Context.PrepareExpression(StructExpression, Scope, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	return OutResult.SetType(Context, RequestedType, StructPreparedType.GetFieldType(Field));
}

void FExpressionGetStructField::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);

	FEmitShaderExpression* StructValue = StructExpression->GetValueShader(Context, Scope, RequestedStructType);

	OutResult.Code = Context.EmitInlineExpression(Scope, Field->Type, TEXT("%.%"),
		StructValue,
		Field->Name);
}

void FExpressionGetStructField::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	FRequestedType RequestedStructType;
	RequestedStructType.SetField(Field, RequestedType);

	StructExpression->GetValuePreshader(Context, Scope, RequestedStructType, OutResult.Preshader);
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::GetField).Write(Field->Type).Write(Field->ComponentIndex);
	OutResult.Type = Field->Type;
}

void FExpressionSetStructField::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives StructDerivatives = Tree.GetAnalyticDerivatives(StructExpression);
	const FExpressionDerivatives FieldDerivatives = Tree.GetAnalyticDerivatives(FieldExpression);

	if (StructDerivatives.IsValid() && FieldDerivatives.IsValid())
	{
		const Shader::FStructType* DerivativeStructType = StructType->DerivativeType;
		check(DerivativeStructType);
		const Shader::FStructField* DerivativeField = DerivativeStructType->FindFieldByName(Field->Name);
		check(DerivativeField);

		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdx, FieldDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSetStructField>(DerivativeStructType, DerivativeField, StructDerivatives.ExpressionDdy, FieldDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionSetStructField::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	FExpression* PrevStructExpression = Tree.GetPreviousFrame(StructExpression, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	FExpression* PrevFieldExpression = Tree.GetPreviousFrame(FieldExpression, RequestedFieldType);

	return Tree.NewExpression<FExpressionSetStructField>(StructType, Field, PrevStructExpression, PrevFieldExpression);
}

bool FExpressionSetStructField::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);

	FPreparedType StructPreparedType = Context.PrepareExpression(StructExpression, Scope, RequestedStructType);
	if (!StructPreparedType.IsVoid() && StructPreparedType.StructType != StructType)
	{
		return Context.Errors->AddErrorf(TEXT("Expected type %s"), StructType->Name);
	}

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	FPreparedType FieldPreparedType = Context.PrepareExpression(FieldExpression, Scope, RequestedFieldType);

	FPreparedType ResultType(StructPreparedType);
	if (ResultType.IsVoid())
	{
		ResultType = StructType;
	}
	ResultType.SetField(Field, FieldPreparedType);
	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSetStructField::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(Scope, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(Scope, RequestedFieldType);

	FEmitShaderExpression* StructValue = (StructEvaluation != EExpressionEvaluation::None) ? StructExpression->GetValueShader(Context, Scope, RequestedStructType) : nullptr;
	FEmitShaderExpression* FieldValue = (FieldEvaluation != EExpressionEvaluation::None) ? FieldExpression->GetValueShader(Context, Scope, RequestedFieldType, Field->Type) : nullptr;

	if (StructEvaluation == EExpressionEvaluation::None && FieldEvaluation == EExpressionEvaluation::None)
	{
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("((%)0)"), StructType->Name);
	}
	else if (StructEvaluation == EExpressionEvaluation::None)
	{
		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%((%)0, %)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldValue);
	}
	else if (FieldEvaluation == EExpressionEvaluation::None)
	{
		// Don't need field, can just forward the struct value
		OutResult.Code = StructValue;
	}
	else
	{
		check(StructValue->Type.StructType == StructType);
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%(%, %)"),
			StructType->Name,
			Field->Name,
			StructValue,
			FieldValue);
	}
}

void FExpressionSetStructField::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	FRequestedType RequestedStructType(RequestedType);
	RequestedStructType.ClearFieldRequested(Field);
	const EExpressionEvaluation StructEvaluation = StructExpression->GetEvaluation(Scope, RequestedStructType);

	const FRequestedType RequestedFieldType = RequestedType.GetField(Field);
	const EExpressionEvaluation FieldEvaluation = FieldExpression->GetEvaluation(Scope, RequestedFieldType);

	if (StructEvaluation != EExpressionEvaluation::None)
	{
		StructExpression->GetValuePreshader(Context, Scope, RequestedStructType, OutResult.Preshader);
	}
	else
	{
		Context.PreshaderStackPosition++;
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(Shader::FType(StructType));
	}

	if (FieldEvaluation != EExpressionEvaluation::None)
	{
		FieldExpression->GetValuePreshader(Context, Scope, RequestedFieldType, OutResult.Preshader);

		check(Context.PreshaderStackPosition > 0);
		Context.PreshaderStackPosition--;

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::SetField).Write(Field->ComponentIndex).Write(Field->GetNumComponents());
	}
	OutResult.Type = StructType;
}

bool FExpressionSelect::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& ConditionType = Context.PrepareExpression(ConditionExpression, Scope, ERequestedType::Scalar);
	const EExpressionEvaluation ConditionEvaluation = ConditionType.GetEvaluation(Scope, ERequestedType::Scalar);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		FPreparedType ResultType = Context.PrepareExpression(bCondition ? TrueExpression : FalseExpression, Scope, RequestedType);
		ResultType.MergeEvaluation(EExpressionEvaluation::Shader); // TODO - support preshader
		return OutResult.SetType(Context, RequestedType, ResultType);
	}

	const FPreparedType& LhsType = Context.PrepareExpression(FalseExpression, Scope, RequestedType);
	const FPreparedType& RhsType = Context.PrepareExpression(TrueExpression, Scope, RequestedType);
	
	if (LhsType.ValueComponentType != RhsType.ValueComponentType ||
		LhsType.StructType != RhsType.StructType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	FPreparedType ResultType = MergePreparedTypes(LhsType, RhsType);
	ResultType.MergeEvaluation(ConditionEvaluation);
	ResultType.MergeEvaluation(EExpressionEvaluation::Shader); // TODO - support preshader
	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const EExpressionEvaluation ConditionEvaluation = ConditionExpression->GetEvaluation(Scope, ERequestedType::Scalar);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		FExpression* InputExpression = bCondition ? TrueExpression : FalseExpression;
		OutResult.Code = InputExpression->GetValueShader(Context, Scope, RequestedType);
	}
	else
	{
		const Shader::FType LocalType = GetType();
		FEmitShaderExpression* TrueValue = TrueExpression->GetValueShader(Context, Scope, RequestedType, LocalType);
		FEmitShaderExpression* FalseValue = FalseExpression->GetValueShader(Context, Scope, RequestedType, LocalType);

		OutResult.Code = Context.EmitExpression(Scope, LocalType, TEXT("(% ? % : %)"),
			ConditionExpression->GetValueShader(Context, Scope, Shader::EValueType::Bool1),
			TrueValue,
			FalseValue);
	}
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	// TODO
	Context.PreshaderStackPosition++;
	OutResult.Type = GetType();
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);
}

FExpression* FTree::NewUnaryOp(EOperation Op, FExpression* Input)
{
	FExpression* Inputs[1] = { Input };
	return NewExpression<FExpressionOperation>(Op, Inputs);
}

FExpression* FTree::NewBinaryOp(EOperation Op, FExpression* Lhs, FExpression* Rhs)
{
	FExpression* Inputs[2] = { Lhs, Rhs };
	return NewExpression<FExpressionOperation>(Op, Inputs);
}

FExpression* FTree::NewCross(FExpression* Lhs, FExpression* Rhs)
{
	//c_P[0] = v_A[1] * v_B[2] - v_A[2] * v_B[1];
	//c_P[1] = -(v_A[0] * v_B[2] - v_A[2] * v_B[0]);
	//c_P[2] = v_A[0] * v_B[1] - v_A[1] * v_B[0];
	FExpression* Lhs0 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(1, 0, 0), Lhs);
	FExpression* Lhs1 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(2, 2, 1), Lhs);
	FExpression* Rhs0 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(2, 2, 1), Rhs);
	FExpression* Rhs1 = NewExpression<FExpressionSwizzle>(FSwizzleParameters(1, 0, 0), Rhs);
	return NewSub(NewMul(NewMul(Lhs0, Rhs0), NewConstant(FVector3f(1.0f, -1.0f, 1.0f))), NewMul(Lhs1, Rhs1));
}

FExpressionOperation::FExpressionOperation(EOperation InOp, TConstArrayView<FExpression*> InInputs) : Op(InOp)
{
	const FOperationDescription OpDesc = GetOperationDescription(InOp);
	check(OpDesc.NumInputs == InInputs.Num());
	check(InInputs.Num() <= MaxInputs);

	for (int32 i = 0; i < OpDesc.NumInputs; ++i)
	{
		Inputs[i] = InInputs[i];
		check(Inputs[i]);
	}
}

namespace Private
{
struct FOperationRequestedTypes
{
	FRequestedType InputType[FExpressionOperation::MaxInputs];
	bool bIsMatrixOperation = false;
};
struct FOperationTypes
{
	
	Shader::EValueType InputType[FExpressionOperation::MaxInputs];
	Shader::EValueType ResultType;
	bool bIsLWC = false;
};

FOperationRequestedTypes GetOperationRequestedTypes(EOperation Op, const FRequestedType& RequestedType)
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FOperationRequestedTypes Types;
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		Types.InputType[Index] = RequestedType;
	}
	switch (Op)
	{
	case EOperation::Length:
	case EOperation::Normalize:
		Types.InputType[0] = ERequestedType::Vector4;
		break;
	case EOperation::Dot:
		Types.InputType[0] = ERequestedType::Vector4;
		Types.InputType[1] = ERequestedType::Vector4;
		break;
	case EOperation::VecMulMatrix3:
		Types.bIsMatrixOperation = true;
		Types.InputType[0] = ERequestedType::Vector3;
		Types.InputType[1] = ERequestedType::Matrix4x4;
		break;
	case EOperation::VecMulMatrix4:
		Types.bIsMatrixOperation = true;
		Types.InputType[0] = ERequestedType::Vector3;
		Types.InputType[1] = ERequestedType::Matrix4x4;
		break;
	case EOperation::Matrix3MulVec:
	case EOperation::Matrix4MulVec:
		// No LWC for transpose matrices
		Types.bIsMatrixOperation = true;
		Types.InputType[0] = ERequestedType::Matrix4x4;
		Types.InputType[1] = ERequestedType::Vector3;
		break;
	default:
		break;
	}
	return Types;
}

FOperationTypes GetOperationTypes(EOperation Op, TConstArrayView<Shader::EValueType> InputTypes)
{
	Shader::EValueComponentType ComponentType = Shader::EValueComponentType::Void;
	int32 NumComponents = 0;
	for (int32 Index = 0; Index < InputTypes.Num(); ++Index)
	{
		const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputTypes[Index]);
		ComponentType = Shader::CombineComponentTypes(ComponentType, InputTypeDesc.ComponentType);
		NumComponents = FMath::Max<int32>(NumComponents, InputTypeDesc.NumComponents);
	}

	Shader::EValueType IntermediateType = Shader::MakeValueType(ComponentType, NumComponents);

	FOperationTypes Types;
	for (int32 Index = 0; Index < InputTypes.Num(); ++Index)
	{
		Types.InputType[Index] = IntermediateType;
	}
	Types.ResultType = IntermediateType;
	Types.bIsLWC = (ComponentType == Shader::EValueComponentType::Double);
	switch (Op)
	{
	case EOperation::Length:
		Types.ResultType = Shader::MakeValueType(ComponentType, 1);
		break;
	case EOperation::Normalize:
		Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
		break;
	case EOperation::Frac:
	case EOperation::Rcp:
	case EOperation::Sqrt:
	case EOperation::Sign:
	case EOperation::Saturate:
	case EOperation::Sin:
	case EOperation::Cos:
	case EOperation::Tan:
	case EOperation::Asin:
	case EOperation::AsinFast:
	case EOperation::Acos:
	case EOperation::AcosFast:
	case EOperation::Atan:
	case EOperation::AtanFast:
		Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
		break;
	case EOperation::Log2:
		// No LWC support yet
		Types.InputType[0] = Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
		break;
	case EOperation::Less:
	case EOperation::Greater:
	case EOperation::LessEqual:
	case EOperation::GreaterEqual:
		Types.ResultType = Shader::MakeValueType(Shader::EValueComponentType::Bool, NumComponents);
		break;
	case EOperation::Fmod:
		Types.InputType[1] = Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
		break;
	case EOperation::PowPositiveClamped:
	case EOperation::Atan2:
	case EOperation::Atan2Fast:
		// No LWC support yet
		Types.InputType[0] = Types.InputType[1] = Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
		break;
	case EOperation::Dot:
		Types.ResultType = Shader::MakeValueType(ComponentType, 1);
		break;
	case EOperation::VecMulMatrix3:
		// No LWC for matrix3
		Types.InputType[0] = Shader::EValueType::Float3;
		Types.InputType[1] = Shader::EValueType::Float4x4;
		Types.ResultType = Shader::EValueType::Float3;
		break;
	case EOperation::VecMulMatrix4:
		Types.InputType[0] = Shader::EValueType::Float3;
		Types.InputType[1] = Shader::EValueType::Float4x4;
		Types.ResultType = Shader::EValueType::Float3;
		break;
	case EOperation::Matrix3MulVec:
	case EOperation::Matrix4MulVec:
		// No LWC for transpose matrices
		Types.InputType[0] = Shader::EValueType::Float4x4;
		Types.InputType[1] = Shader::EValueType::Float3;
		Types.ResultType = Shader::EValueType::Float3;
		break;
	default:
		break;
	}
	return Types;
}
} // namespace Private

void FExpressionOperation::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// Operations with constant derivatives
	switch (Op)
	{
	case EOperation::Less:
	case EOperation::Greater:
	case EOperation::LessEqual:
	case EOperation::GreaterEqual:
		OutResult.ExpressionDdx = Tree.NewConstant(0.0f);
		OutResult.ExpressionDdx = OutResult.ExpressionDdy;
		break;
	default:
		break;
	}

	if (OutResult.IsValid())
	{
		return;
	}

	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FExpressionDerivatives InputDerivatives[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputDerivatives[Index] = Tree.GetAnalyticDerivatives(Inputs[Index]);
		if (!InputDerivatives[Index].IsValid())
		{
			return;
		}
	}

	switch (Op)
	{
	case EOperation::Neg:
		OutResult.ExpressionDdx = Tree.NewNeg(InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewNeg(InputDerivatives[0].ExpressionDdy);
		break;
	case EOperation::Rcp:
	{
		FExpression* Result = Tree.NewRcp(Inputs[0]);
		FExpression* dFdA = Tree.NewNeg(Tree.NewMul(Result, Result));
		OutResult.ExpressionDdx = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy);
		break;
	}
	case EOperation::Frac:
		OutResult = InputDerivatives[0];
		break;
	case EOperation::Length:
	case EOperation::Normalize:
	case EOperation::Floor:
	case EOperation::Ceil:
	case EOperation::Round:
	case EOperation::Trunc:
	case EOperation::Sign:
	case EOperation::Abs:
	case EOperation::Saturate:
	case EOperation::PowPositiveClamped:
	case EOperation::Sin:
	case EOperation::Cos:
	case EOperation::Tan:
	case EOperation::Asin:
	case EOperation::AsinFast:
	case EOperation::Acos:
	case EOperation::AcosFast:
	case EOperation::Atan:
	case EOperation::AtanFast:
	case EOperation::Atan2:
	case EOperation::Atan2Fast:
	case EOperation::Sqrt:
	case EOperation::Log2:
		// TODO
		break;
	case EOperation::Add:
		OutResult.ExpressionDdx = Tree.NewAdd(InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewAdd(InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	case EOperation::Sub:
		OutResult.ExpressionDdx = Tree.NewSub(InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewSub(InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	case EOperation::Mul:
		OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(InputDerivatives[0].ExpressionDdx, Inputs[1]), Tree.NewMul(InputDerivatives[1].ExpressionDdx, Inputs[0]));
		OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(InputDerivatives[0].ExpressionDdy, Inputs[1]), Tree.NewMul(InputDerivatives[1].ExpressionDdy, Inputs[0]));
		break;
	case EOperation::Div:
	{
		FExpression* Denom = Tree.NewRcp(Tree.NewMul(Inputs[1], Inputs[1]));
		FExpression* dFdA = Tree.NewMul(Inputs[1], Denom);
		FExpression* dFdB = Tree.NewNeg(Tree.NewMul(Inputs[0], Denom));
		OutResult.ExpressionDdx = Tree.NewAdd(Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdx), Tree.NewMul(dFdB, InputDerivatives[1].ExpressionDdx));
		OutResult.ExpressionDdy = Tree.NewAdd(Tree.NewMul(dFdA, InputDerivatives[0].ExpressionDdy), Tree.NewMul(dFdB, InputDerivatives[1].ExpressionDdy));
		break;
	}
	case EOperation::Fmod:
		// Only valid when B derivatives are zero.
		// We can't really do anything meaningful in the non-zero case.
		OutResult = InputDerivatives[0];
		break;
	case EOperation::Dot:
	{
		// Dot means multiply the values, then sum the resulting components
		FExpression* MulDdx = Tree.NewAdd(Tree.NewMul(InputDerivatives[0].ExpressionDdx, Inputs[1]), Tree.NewMul(InputDerivatives[1].ExpressionDdx, Inputs[0]));
		FExpression* MulDdy = Tree.NewAdd(Tree.NewMul(InputDerivatives[0].ExpressionDdy, Inputs[1]), Tree.NewMul(InputDerivatives[1].ExpressionDdy, Inputs[0]));
		// Dot the products with 1 to sum them
		FExpression* Const1 = Tree.NewConstant(FVector4f(1.0f, 1.0f, 1.0f, 1.0f));
		OutResult.ExpressionDdx = Tree.NewDot(MulDdx, Const1);
		OutResult.ExpressionDdy = Tree.NewDot(MulDdy, Const1);
		break;
	}
	case EOperation::Min:
	{
		FExpression* Cond = Tree.NewLess(Inputs[0], Inputs[1]);
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	}
	case EOperation::Max:
	{
		FExpression* Cond = Tree.NewGreater(Inputs[0], Inputs[1]);
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdx, InputDerivatives[1].ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSelect>(Cond, InputDerivatives[0].ExpressionDdy, InputDerivatives[1].ExpressionDdy);
		break;
	}
	case EOperation::VecMulMatrix3:
	case EOperation::VecMulMatrix4:
	case EOperation::Matrix3MulVec:
	case EOperation::Matrix4MulVec:
		// TODO
		break;
	default:
		checkNoEntry();
		break;
	}
}

FExpression* FExpressionOperation::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FExpression* PrevFrameInputs[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		PrevFrameInputs[Index] = Tree.GetPreviousFrame(Inputs[Index], RequestedTypes.InputType[Index]);
	}

	return Tree.NewExpression<FExpressionOperation>(Op, MakeArrayView(PrevFrameInputs, OpDesc.NumInputs));
}

bool FExpressionOperation::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);

	FPreparedType InputType[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputType[Index] = Context.PrepareExpression(Inputs[Index], Scope, RequestedTypes.InputType[Index]);
		if (InputType[Index].IsVoid())
		{
			return false;
		}

		if (!InputType[Index].IsNumeric())
		{
			return Context.Errors->AddError(TEXT("Invalid arithmetic between non-numeric types"));
		}
	}

	FPreparedType ResultType;
	if (RequestedTypes.bIsMatrixOperation)
	{
		const EExpressionEvaluation Evaluation = CombineEvaluations(InputType[0].GetEvaluation(Scope), InputType[1].GetEvaluation(Scope));
		const Shader::EValueComponentType ComponentType = Shader::CombineComponentTypes(InputType[0].ValueComponentType, InputType[1].ValueComponentType);
		ResultType = FPreparedType(Shader::MakeValueType(ComponentType, 3), Evaluation);
	}
	else if (OpDesc.NumInputs == 1)
	{
		switch (Op)
		{
		case EOperation::Length:
			ResultType = FPreparedType(Shader::MakeValueType(InputType[0].ValueComponentType, 1), InputType[0].GetEvaluation(Scope));
			break;
		default:
			ResultType = InputType[0];
			if (Op == EOperation::Normalize)
			{
				ResultType.ValueComponentType = Shader::MakeNonLWCType(ResultType.ValueComponentType);
			}
			break;
		}
	}
	else if(OpDesc.NumInputs == 2)
	{
		/*if (InputType[0].GetNumComponents() != 1 && InputType[1].GetNumComponents() != 1 && InputType[0].GetNumComponents() != InputType[1].GetNumComponents())
		{
			return Context.Errors->AddErrorf(TEXT("Invalid arithmetic between %s and %s"), InputType[0].GetType().GetName(), InputType[1].GetType().GetName());
		}*/
		ResultType = MergePreparedTypes(InputType[0], InputType[1]);
		if (Op == EOperation::Less || Op == EOperation::Greater || Op == EOperation::LessEqual || Op == EOperation::GreaterEqual)
		{
			ResultType.ValueComponentType = Shader::EValueComponentType::Bool;
		}
		if (Op == EOperation::Min || Op == EOperation::Max)
		{
			const Shader::FComponentBounds LhsBounds = InputType[0].GetBounds(RequestedType);
			const Shader::FComponentBounds RhsBounds = InputType[1].GetBounds(RequestedType);
			const Shader::FComponentBounds Bounds = (Op == EOperation::Min) ? Shader::MinBound(LhsBounds, RhsBounds) : Shader::MaxBound(LhsBounds, RhsBounds);
			ResultType.UpdateBounds(RequestedType, Bounds);
		}
	}

	if (OpDesc.PreshaderOpcode == Shader::EPreshaderOpcode::Nop)
	{
		// No preshader support
		ResultType.SetEvaluation(EExpressionEvaluation::Shader);
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionOperation::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	Shader::EValueType InputTypes[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputTypes[Index] = Inputs[Index]->GetType();
	}
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);
	const Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(InputTypes, OpDesc.NumInputs));
	FEmitShaderExpression* InputValue[MaxInputs] = { nullptr };
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputValue[Index] = Inputs[Index]->GetValueShader(Context, Scope, RequestedTypes.InputType[Index], Types.InputType[Index]);
	}

	switch (Op)
	{
	case EOperation::Abs:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCAbs(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("abs(%)"), InputValue[0]);
		}
		break;
	case EOperation::Neg:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCNegate(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("(-%)"), InputValue[0]);
		}
		break;
	case EOperation::Rcp:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCRcp(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("rcp(%)"), InputValue[0]);
		}
		break;
	case EOperation::Sqrt: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCSqrt(%)") : TEXT("sqrt(%)"), InputValue[0]); break;
	case EOperation::Log2: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("log2(%)"), InputValue[0]); break;
	case EOperation::Frac:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCFrac(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("frac(%)"), InputValue[0]);
		}
		break;

	case EOperation::Floor:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCFloor(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("floor(%)"), InputValue[0]);
		}
		break;
	case EOperation::Ceil:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCCeil(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("ceil(%)"), InputValue[0]);
		}
		break;
	case EOperation::Round:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCRound(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("round(%)"), InputValue[0]);
		}
		break;
	case EOperation::Trunc:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCTrunc(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("trunc(%)"), InputValue[0]);
		}
		break;
	case EOperation::Saturate:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCSaturate(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("saturate(%)"), InputValue[0]);
		}
		break;
	case EOperation::Sign:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCSign(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("sign(%)"), InputValue[0]);
		}
		break;
	case EOperation::Length:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCLength(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("length(%)"), InputValue[0]);
		}
		break;
	case EOperation::Normalize:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCNormalize(%)"), InputValue[0]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("normalize(%)"), InputValue[0]);
		}
		break;
	case EOperation::Sin: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCSin(%)") : TEXT("sin(%)"), InputValue[0]); break;
	case EOperation::Cos: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCCos(%)") : TEXT("cos(%)"), InputValue[0]); break;
	case EOperation::Tan: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCTan(%)") : TEXT("tan(%)"), InputValue[0]); break;
	case EOperation::Asin: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCAsin(%)") : TEXT("asin(%)"), InputValue[0]); break;
	case EOperation::AsinFast: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCAsin(%)") : TEXT("asinFast(%)"), InputValue[0]); break;
	case EOperation::Acos: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCAcos(%)") : TEXT("acos(%)"), InputValue[0]); break;
	case EOperation::AcosFast: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCAcos(%)") : TEXT("acosFast(%)"), InputValue[0]); break;
	case EOperation::Atan: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCAtan(%)") : TEXT("atan(%)"), InputValue[0]); break;
	case EOperation::AtanFast: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, Types.bIsLWC ? TEXT("LWCAtan(%)") : TEXT("atanFast(%)"), InputValue[0]); break;
	case EOperation::Add:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCAdd(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% + %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Sub:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCSubtract(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% - %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Mul:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMultiply(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% * %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Div:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCDivide(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% / %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Fmod:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCFmod(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("fmod(%, %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::PowPositiveClamped:
		OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("PositiveClampedPow(%, %)"), InputValue[0], InputValue[1]);
		break;
	case EOperation::Atan2: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("atan2(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Atan2Fast: OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("atan2Fast(%, %)"), InputValue[0], InputValue[1]); break;
	case EOperation::Dot:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCDot(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("dot(%, %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Min:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMin(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("min(%, %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Max:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMax(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("max(%, %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Less:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCLess(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% < %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Greater:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCGreater(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% > %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::LessEqual:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCLessEqual(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% <= %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::GreaterEqual:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCGreaterEqual(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("(% >= %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::VecMulMatrix3:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMultiply(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("mul(%, (float3x3)%)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::VecMulMatrix4:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMultiply(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("mul(%, %)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::Matrix3MulVec:
		OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("mul((float3x3)%, %)"), InputValue[0], InputValue[1]);
		break;
	case EOperation::Matrix4MulVec:
		OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("mul(%, %)"), InputValue[0], InputValue[1]);
		break;
	default:
		checkNoEntry();
		break;
	}
}

void FExpressionOperation::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	Shader::EValueType InputTypes[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputTypes[Index] = Inputs[Index]->GetType();
	}
	const Private::FOperationRequestedTypes RequestedTypes = Private::GetOperationRequestedTypes(Op, RequestedType);
	const Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(InputTypes, OpDesc.NumInputs));
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		Inputs[Index]->GetValuePreshader(Context, Scope, RequestedTypes.InputType[Index], OutResult.Preshader);
	}

	const int32 NumInputsToPop = OpDesc.NumInputs - 1;
	if (NumInputsToPop > 0)
	{
		check(Context.PreshaderStackPosition >= NumInputsToPop);
		Context.PreshaderStackPosition -= NumInputsToPop;
	}

	OutResult.Preshader.WriteOpcode(OpDesc.PreshaderOpcode);
	OutResult.Type = Types.ResultType;
}

void FExpressionDerivative::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// TODO
}

FExpression* FExpressionDerivative::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	return Tree.NewExpression<FExpressionDerivative>(Coord, Tree.GetPreviousFrame(Input, RequestedType));
}

bool FExpressionDerivative::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	FPreparedType ResultType = Context.PrepareExpression(Input, Scope, RequestedType);
	if (ResultType.IsVoid())
	{
		return false;
	}

	ResultType.ValueComponentType = Shader::MakeNonLWCType(ResultType.ValueComponentType);

	const EExpressionEvaluation InputEvaluation = ResultType.GetEvaluation(Scope, RequestedType);
	if (InputEvaluation != EExpressionEvaluation::Shader)
	{
		ResultType.SetEvaluation(EExpressionEvaluation::Constant);
	}
	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionDerivative::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	FEmitShaderExpression* EmitInput = Input->GetValueShader(Context, Scope, RequestedType);
	const bool bIsLWC = Shader::IsLWCType(EmitInput->Type);
	const TCHAR* FunctionName = nullptr;
	switch (Coord)
	{
	case EDerivativeCoordinate::Ddx: FunctionName = bIsLWC ? TEXT("LWCDdx") : TEXT("ddx"); break;
	case EDerivativeCoordinate::Ddy: FunctionName = bIsLWC ? TEXT("LWCDdy") : TEXT("ddy"); break;
	default: checkNoEntry(); break;
	}
	OutResult.Code = Context.EmitExpression(Scope, Shader::MakeNonLWCType(EmitInput->Type), TEXT("%(%)"), FunctionName, EmitInput);
}

void FExpressionDerivative::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	// Derivative of a constant is 0
	Context.PreshaderStackPosition++;
	OutResult.Type = GetType();
	OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);
}

void FExpressionSwizzle::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives InputDerivatives = Tree.GetAnalyticDerivatives(Input);
	if (InputDerivatives.IsValid())
	{
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionSwizzle>(Parameters, InputDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionSwizzle>(Parameters, InputDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionSwizzle::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	return Tree.NewExpression<FExpressionSwizzle>(Parameters, Tree.GetPreviousFrame(Input, RequestedInputType));
}

bool FExpressionSwizzle::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);

	FPreparedType ResultType;
	if (RequestedInputType.IsVoid())
	{
		// All the requested components are outside the swizzle, so just return 0
		ResultType.ValueComponentType = Shader::EValueComponentType::Float;
		for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
		{
			ResultType.SetComponent(ComponentIndex, EExpressionEvaluation::ConstantZero);
		}
	}
	else
	{
		const FPreparedType& InputType = Context.PrepareExpression(Input, Scope, RequestedInputType);

		ResultType.ValueComponentType = InputType.ValueComponentType;
		for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
		{
			if (RequestedType.IsComponentRequested(ComponentIndex))
			{
				const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
				ResultType.SetComponent(ComponentIndex, InputType.GetComponent(SwizzledComponentIndex));
			}
			else
			{
				ResultType.SetComponent(ComponentIndex, EExpressionEvaluation::ConstantZero);
			}
		}
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	TCHAR LWCSwizzle[10] = TEXT("");
	bool bHasSwizzleReorder = false;

	const int32 NumComponents = FMath::Min(RequestedType.GetNumComponents(), Parameters.NumComponents);
	FRequestedType RequestedInputType;
	for (int32 ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
	{
		// If component wasn't requested, we just refer to 'x' component of input, since that should always be present
		int32 SwizzledComponentIndex = 0;

		if (RequestedType.IsComponentRequested(ComponentIndex))
		{
			SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
			RequestedInputType.SetComponentRequest(SwizzledComponentIndex);
		}

		Swizzle[ComponentIndex] = ComponentName[SwizzledComponentIndex];
		LWCSwizzle[ComponentIndex * 2 + 0] = TEXT(',');
		LWCSwizzle[ComponentIndex * 2 + 1] = TEXT('0') + SwizzledComponentIndex;

		if (SwizzledComponentIndex != ComponentIndex)
		{
			bHasSwizzleReorder = true;
		}
	}

	FEmitShaderExpression* InputValue = Input->GetValueShader(Context, Scope, RequestedInputType);
	const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputValue->Type);

	if (bHasSwizzleReorder || NumComponents != InputTypeDesc.NumComponents)
	{
		const Shader::EValueType ResultType = Shader::MakeValueType(InputTypeDesc.ComponentType, NumComponents);
		const int32 NumRequestedComponents = RequestedInputType.GetNumComponents();
		check(NumRequestedComponents > 0);
	
		if (NumRequestedComponents > InputTypeDesc.NumComponents)
		{
			// Zero-extend our input if needed, so we can acccess all the given components
			InputValue = Context.EmitCast(Scope, InputValue, Shader::MakeValueType(InputTypeDesc.ComponentType, NumRequestedComponents), EEmitCastFlags::ZeroExtendScalar);
		}

		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			check(LWCSwizzle[0]);
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("LWCSwizzle(%%)"),
				InputValue,
				LWCSwizzle);
		}
		else
		{
			check(Swizzle[0]);
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("%.%"),
				InputValue,
				Swizzle);
		}
	}
	else
	{
		OutResult.Code = InputValue;
	}
}

void FExpressionSwizzle::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);
	if (RequestedInputType.IsVoid())
	{
		Context.PreshaderStackPosition++;
		OutResult.Type = Shader::MakeValueType(Shader::EValueComponentType::Float, Parameters.NumComponents);
		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ConstantZero).Write(OutResult.Type);
	}
	else
	{
		const Shader::FType InputType = Input->GetValuePreshader(Context, Scope, RequestedInputType, OutResult.Preshader);
		const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputType);

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::ComponentSwizzle)
			.Write((uint8)Parameters.NumComponents)
			.Write((uint8)Parameters.ComponentIndex[0])
			.Write((uint8)Parameters.ComponentIndex[1])
			.Write((uint8)Parameters.ComponentIndex[2])
			.Write((uint8)Parameters.ComponentIndex[3]);
		OutResult.Type = Shader::MakeValueType(InputTypeDesc.ComponentType, Parameters.NumComponents);
	}
}

void FExpressionAppend::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	const FExpressionDerivatives LhsDerivatives = Tree.GetAnalyticDerivatives(Lhs);
	const FExpressionDerivatives RhsDerivatives = Tree.GetAnalyticDerivatives(Rhs);
	if (LhsDerivatives.IsValid() && RhsDerivatives.IsValid())
	{
		OutResult.ExpressionDdx = Tree.NewExpression<FExpressionAppend>(LhsDerivatives.ExpressionDdx, RhsDerivatives.ExpressionDdx);
		OutResult.ExpressionDdy = Tree.NewExpression<FExpressionAppend>(LhsDerivatives.ExpressionDdy, RhsDerivatives.ExpressionDdy);
	}
}

FExpression* FExpressionAppend::ComputePreviousFrame(FTree& Tree, const FRequestedType& RequestedType) const
{
	// TODO - requested type?
	return Tree.NewExpression<FExpressionAppend>(Tree.GetPreviousFrame(Lhs, RequestedType), Tree.GetPreviousFrame(Rhs, RequestedType));
}

bool FExpressionAppend::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FPreparedType& LhsType = Context.PrepareExpression(Lhs, Scope, RequestedType);
	const int32 NumRequestedComponents = RequestedType.GetNumComponents();
	const int32 NumLhsComponents = FMath::Min(LhsType.GetNumComponents(), NumRequestedComponents);

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponent(Index, LhsType.GetComponent(Index));
	}

	FRequestedType RhsRequestedType;
	for (int32 Index = NumLhsComponents; Index < NumRequestedComponents; ++Index)
	{
		RhsRequestedType.SetComponentRequest(Index - NumLhsComponents, RequestedType.IsComponentRequested(Index));
	}

	if (!RhsRequestedType.IsVoid())
	{
		const FPreparedType& RhsType = Context.PrepareExpression(Rhs, Scope, RhsRequestedType);
		if (LhsType.ValueComponentType != RhsType.ValueComponentType)
		{
			return Context.Errors->AddError(TEXT("Type mismatch"));
		}

		const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);
		for (int32 Index = 0; Index < NumRhsComponents; ++Index)
		{
			ResultType.SetComponent(NumLhsComponents + Index, RhsType.GetComponent(Index));
		}
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

namespace Private
{
struct FAppendTypes
{
	Shader::EValueType ResultType;
	Shader::EValueType LhsType;
	Shader::EValueType RhsType;
	FRequestedType LhsRequestedType;
	FRequestedType RhsRequestedType;
	bool bIsLWC;
};
FAppendTypes GetAppendTypes(const FRequestedType& RequestedType, Shader::EValueType LhsType, Shader::EValueType RhsType)
{
	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	const Shader::EValueComponentType ComponentType = Shader::CombineComponentTypes(LhsTypeDesc.ComponentType, RhsTypeDesc.ComponentType);
	const int32 NumComponents = FMath::Min(LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents, 4);

	FAppendTypes Types;
	for (int32 Index = 0; Index < LhsTypeDesc.NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Types.LhsRequestedType.SetComponentRequest(Index);
		}
	}
	for (int32 Index = LhsTypeDesc.NumComponents; Index < NumComponents; ++Index)
	{
		if (RequestedType.IsComponentRequested(Index))
		{
			Types.RhsRequestedType.SetComponentRequest(Index - LhsTypeDesc.NumComponents);
		}
	}

	Types.ResultType = Shader::MakeValueType(ComponentType, NumComponents);
	Types.LhsType = Shader::MakeValueType(ComponentType, LhsTypeDesc.NumComponents);
	Types.RhsType = Shader::MakeValueType(ComponentType, NumComponents - LhsTypeDesc.NumComponents);
	Types.bIsLWC = ComponentType == Shader::EValueComponentType::Double;
	return Types;
}
} // namespace Private

void FExpressionAppend::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetType(), Rhs->GetType());
	FEmitShaderExpression* LhsValue = Lhs->GetValueShader(Context, Scope, Types.LhsRequestedType, Types.LhsType);

	if (Types.RhsType == Shader::EValueType::Void)
	{
		OutResult.Code = LhsValue;
	}
	else
	{
		FEmitShaderExpression* RhsValue = Rhs->GetValueShader(Context, Scope, Types.RhsRequestedType, Types.RhsType);
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("MakeLWCVector(%, %)"),
				LhsValue,
				RhsValue);
		}
		else
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, Types.ResultType, TEXT("%(%, %)"),
				Shader::GetValueTypeDescription(Types.ResultType).Name,
				LhsValue,
				RhsValue);
		}
	}
}

void FExpressionAppend::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(RequestedType, Lhs->GetType(), Rhs->GetType());
	Lhs->GetValuePreshader(Context, Scope, Types.LhsRequestedType, OutResult.Preshader);
	if (Types.RhsType != Shader::EValueType::Void)
	{
		Rhs->GetValuePreshader(Context, Scope, Types.RhsRequestedType, OutResult.Preshader);

		check(Context.PreshaderStackPosition > 0);
		Context.PreshaderStackPosition--;

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
	}
	OutResult.Type = Types.ResultType;
}

bool FExpressionInlineCustomHLSL::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::FType(ResultType));
}

void FExpressionInlineCustomHLSL::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	OutResult.Code = Context.EmitExpression(Scope, ResultType, Code);
}

bool FExpressionCustomHLSL::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	for (const FCustomHLSLInput& Input : Inputs)
	{
		const FPreparedType& InputType = Context.PrepareExpression(Input.Expression, Scope, ERequestedType::Vector4);
		if (InputType.IsVoid())
		{
			return false;
		}
	}

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::FType(OutputStructType));
}

void FExpressionCustomHLSL::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	OutResult.Code = Context.EmitCustomHLSL(Scope, DeclarationCode, FunctionCode, Inputs, OutputStructType);
}

bool FStatementBreak::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	return true;
}

void FStatementBreak::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	Context.EmitStatement(Scope, TEXT("break;"));
}

void FStatementBreak::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	FPreshaderLoopScope* LoopScope = Context.PreshaderLoopScopes.Last();
	check(!LoopScope->BreakStatement);
	LoopScope->BreakStatement = this;
	LoopScope->BreakLabel = OutPreshader.WriteJump(Shader::EPreshaderOpcode::Jump);
}

bool FStatementReturn::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	return true;
}

void FStatementReturn::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	Context.EmitStatement(Scope, TEXT("return %;"), Expression->GetValueShader(Context, Scope));
}

bool FStatementIf::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	const FPreparedType& ConditionType = Context.PrepareExpression(ConditionExpression, Scope, ERequestedType::Scalar);
	if (ConditionType.IsVoid())
	{
		return false;
	}

	const EExpressionEvaluation ConditionEvaluation = ConditionType.GetEvaluation(Scope, ERequestedType::Scalar);
	check(ConditionEvaluation != EExpressionEvaluation::None);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		if (bCondition)
		{
			Context.MarkScopeEvaluation(Scope, ThenScope, EExpressionEvaluation::Constant);
			Context.MarkScopeDead(Scope, ElseScope);
		}
		else
		{
			Context.MarkScopeDead(Scope, ThenScope);
			Context.MarkScopeEvaluation(Scope, ElseScope, EExpressionEvaluation::Constant);
		}
	}
	else
	{
		Context.MarkScopeEvaluation(Scope, ThenScope, ConditionEvaluation);
		Context.MarkScopeEvaluation(Scope, ElseScope, ConditionEvaluation);
	}

	return true;
}

void FStatementIf::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	FEmitShaderNode* Dependency = nullptr;
	const EExpressionEvaluation ConditionEvaluation = ConditionExpression->GetEvaluation(Scope, ERequestedType::Scalar);
	if (ConditionEvaluation == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		if (bCondition)
		{
			Dependency = Context.EmitNextScope(Scope, ThenScope);
		}
		else if(!bCondition)
		{
			Dependency = Context.EmitNextScope(Scope, ElseScope);
		}
	}
	else if(ConditionEvaluation != EExpressionEvaluation::None)
	{
		FEmitShaderExpression* ConditionValue = ConditionExpression->GetValueShader(Context, Scope, Shader::EValueType::Bool1);
		Dependency = Context.EmitNestedScopes(Scope, ThenScope, ElseScope, TEXT("if (%)"), TEXT("else"), ConditionValue);
	}

	Context.EmitNextScopeWithDependency(Scope, Dependency, NextScope);
}

void FStatementIf::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	ConditionExpression->GetValuePreshader(Context, Scope, ERequestedType::Scalar, OutPreshader);

	check(Context.PreshaderStackPosition > 0);
	Context.PreshaderStackPosition--;
	const Shader::FPreshaderLabel Label0 = OutPreshader.WriteJump(Shader::EPreshaderOpcode::JumpIfFalse);

	Context.EmitPreshaderScope(ThenScope, RequestedType, Scopes, OutPreshader);

	const Shader::FPreshaderLabel Label1 = OutPreshader.WriteJump(Shader::EPreshaderOpcode::Jump);
	OutPreshader.SetLabel(Label0);
	
	Context.EmitPreshaderScope(ElseScope, RequestedType, Scopes, OutPreshader);

	OutPreshader.SetLabel(Label1);
}

bool FStatementLoop::Prepare(FEmitContext& Context, FEmitScope& Scope) const
{
	FEmitScope* BreakScope = Context.PrepareScope(&BreakStatement->GetParentScope());
	if (!BreakScope)
	{
		return false;
	}

	Context.MarkScopeEvaluation(Scope, LoopScope, BreakScope->Evaluation);
	return true;
}

void FStatementLoop::EmitShader(FEmitContext& Context, FEmitScope& Scope) const
{
	FEmitShaderNode* Dependency = Context.EmitNestedScope(Scope, LoopScope, TEXT("while (true)"));
	Context.EmitNextScopeWithDependency(Scope, Dependency, NextScope);
}

void FStatementLoop::EmitPreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, TArrayView<const FEmitPreshaderScope> Scopes, Shader::FPreshaderData& OutPreshader) const
{
	FPreshaderLoopScope PreshaderLoopScope;
	Context.PreshaderLoopScopes.Add(&PreshaderLoopScope);

	const Shader::FPreshaderLabel Label = OutPreshader.GetLabel();
	Context.EmitPreshaderScope(LoopScope, RequestedType, Scopes, OutPreshader);
	OutPreshader.WriteJump(Shader::EPreshaderOpcode::Jump, Label);

	verify(Context.PreshaderLoopScopes.Pop() == &PreshaderLoopScope);
	check(PreshaderLoopScope.BreakStatement == BreakStatement);
	OutPreshader.SetLabel(PreshaderLoopScope.BreakLabel);
}

} // namespace HLSLTree
} // namespace UE
