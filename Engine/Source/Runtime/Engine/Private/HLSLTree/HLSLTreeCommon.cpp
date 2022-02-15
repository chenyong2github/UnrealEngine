// Copyright Epic Games, Inc. All Rights Reserved.
#include "HLSLTree/HLSLTreeCommon.h"
#include "HLSLTree/HLSLTreeEmit.h"
#include "Misc/StringBuilder.h"
#include "MaterialShared.h"
#include "MaterialSceneTextureId.h"
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

	case EExternalInput::WorldPosition: return FExternalInputDescription(TEXT("WorldPosition"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevWorldPosition);
	case EExternalInput::WorldPosition_NoOffsets: return FExternalInputDescription(TEXT("WorldPosition_NoOffsets"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevWorldPosition_NoOffsets);
	case EExternalInput::TranslatedWorldPosition: return FExternalInputDescription(TEXT("TranslatedWorldPosition"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevTranslatedWorldPosition);
	case EExternalInput::TranslatedWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("TranslatedWorldPosition_NoOffsets"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy, EExternalInput::PrevTranslatedWorldPosition_NoOffsets);

	case EExternalInput::PrevWorldPosition: return FExternalInputDescription(TEXT("PrevWorldPosition"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::PrevWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("PrevWorldPosition_NoOffsets"), Shader::EValueType::Double3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::PrevTranslatedWorldPosition: return FExternalInputDescription(TEXT("PrevTranslatedWorldPosition"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);
	case EExternalInput::PrevTranslatedWorldPosition_NoOffsets: return FExternalInputDescription(TEXT("PrevTranslatedWorldPosition_NoOffsets"), Shader::EValueType::Float3, EExternalInput::WorldPosition_Ddx, EExternalInput::WorldPosition_Ddy);

	case EExternalInput::WorldPosition_Ddx: return FExternalInputDescription(TEXT("WorldPosition_Ddx"), Shader::EValueType::Float3);
	case EExternalInput::WorldPosition_Ddy: return FExternalInputDescription(TEXT("WorldPosition_Ddx"), Shader::EValueType::Float3);

	case EExternalInput::ViewportUV: return FExternalInputDescription(TEXT("ViewportUV"), Shader::EValueType::Float2);
	case EExternalInput::PixelPosition: return FExternalInputDescription(TEXT("PixelPosition"), Shader::EValueType::Float2);
	case EExternalInput::ViewSize: return FExternalInputDescription(TEXT("ViewSize"), Shader::EValueType::Float2);
	case EExternalInput::RcpViewSize: return FExternalInputDescription(TEXT("RcpViewSize"), Shader::EValueType::Float2);

	case EExternalInput::CameraWorldPosition: return FExternalInputDescription(TEXT("CameraWorldPosition"), Shader::EValueType::Double3, EExternalInput::None, EExternalInput::None, EExternalInput::PrevCameraWorldPosition);
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

	case EExternalInput::PrevCameraWorldPosition: return FExternalInputDescription(TEXT("PrevCameraWorldPosition"), Shader::EValueType::Double3);
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
	const EExpressionEvaluation Evaluation = IsStaticMaterialParameter(ParameterType) ? EExpressionEvaluation::Constant : EExpressionEvaluation::Preshader;
	return OutResult.SetType(Context, RequestedType, Evaluation, GetShaderValueType(ParameterType));
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
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, InputDesc.Type);
}

void FExpressionExternalInput::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const int32 TypeIndex = (int32)InputType;
	if (IsTexCoord(InputType))
	{
		const int32 TexCoordIndex = TypeIndex - (int32)EExternalInput::TexCoord0;
		Context.TexCoordMask[Context.ShaderFrequency] |= (1u << TexCoordIndex);
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
		case EExternalInput::WorldPosition: Code = TEXT("GetWorldPosition(Parameters)"); break;
		case EExternalInput::WorldPosition_NoOffsets: Code = TEXT("GetWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::TranslatedWorldPosition: Code = TEXT("GetTranslatedWorldPosition(Parameters)"); break;
		case EExternalInput::TranslatedWorldPosition_NoOffsets: Code = TEXT("GetTranslatedWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::PrevWorldPosition: Code = TEXT("GetPrevWorldPosition(Parameters)"); break;
		case EExternalInput::PrevWorldPosition_NoOffsets: Code = TEXT("GetPrevWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::PrevTranslatedWorldPosition: Code = TEXT("GetPrevTranslatedWorldPosition(Parameters)"); break;
		case EExternalInput::PrevTranslatedWorldPosition_NoOffsets: Code = TEXT("GetPrevTranslatedWorldPosition_NoMaterialOffsets(Parameters)"); break;
		case EExternalInput::WorldPosition_Ddx: Code = TEXT("Parameters.WorldPosition_DDX"); break;
		case EExternalInput::WorldPosition_Ddy: Code = TEXT("Parameters.WorldPosition_DDY"); break;

		case EExternalInput::ViewportUV: Code = TEXT("GetViewportUV(Parameters)"); break;
		case EExternalInput::PixelPosition: Code = TEXT("GetPixelPosition(Parameters)"); break;
		case EExternalInput::ViewSize: Code = TEXT("View.ViewSizeAndInvSize.xy"); break;
		case EExternalInput::RcpViewSize: Code = TEXT("View.ViewSizeAndInvSize.zw"); break;

		case EExternalInput::CameraWorldPosition: Code = TEXT("ResolvedView.WorldCameraOrigin"); break;
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

		case EExternalInput::PrevCameraWorldPosition: Code = TEXT("ResolvedView.PrevWorldCameraOrigin"); break;
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

bool FExpressionTextureSample::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.PrepareExpression(TexCoordExpression, Scope, ERequestedType::Vector2);
	Context.PrepareExpression(TexCoordDerivatives.ExpressionDdx, Scope, ERequestedType::Vector2);
	Context.PrepareExpression(TexCoordDerivatives.ExpressionDdy, Scope, ERequestedType::Vector2);

	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float4);
}

void FExpressionTextureSample::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FTextureDescription& Desc = Declaration->Description;
	const EMaterialValueType MaterialValueType = Desc.Texture->GetMaterialType();
	EMaterialTextureParameterType TextureType = EMaterialTextureParameterType::Count;
	bool bVirtualTexture = false;
	const TCHAR* TextureTypeName = nullptr;
	const TCHAR* SampleFunctionName = nullptr;
	switch (MaterialValueType)
	{
	case MCT_Texture2D:
		TextureType = EMaterialTextureParameterType::Standard2D;
		TextureTypeName = TEXT("Texture2D");
		SampleFunctionName = TEXT("Texture2DSample");
		break;
	case MCT_TextureCube:
		TextureType = EMaterialTextureParameterType::Cube;
		TextureTypeName = TEXT("TextureCube");
		SampleFunctionName = TEXT("TextureCubeSample");
		break;
	case MCT_Texture2DArray:
		TextureType = EMaterialTextureParameterType::Array2D;
		TextureTypeName = TEXT("Texture2DArray");
		SampleFunctionName = TEXT("Texture2DArraySample");
		break;
	case MCT_VolumeTexture:
		TextureType = EMaterialTextureParameterType::Volume;
		TextureTypeName = TEXT("VolumeTexture");
		SampleFunctionName = TEXT("Texture3DSample");
		break;
	case MCT_TextureExternal:
		// TODO
		TextureTypeName = TEXT("ExternalTexture");
		SampleFunctionName = TEXT("TextureExternalSample");
		break;
	case MCT_TextureVirtual:
		// TODO
		TextureType = EMaterialTextureParameterType::Virtual;
		SampleFunctionName = TEXT("TextureVirtualSample");
		bVirtualTexture = true;
		break;
	default:
		checkNoEntry();
		break;
	}

	const TCHAR* SamplerTypeFunction = TEXT("");
	switch (Desc.SamplerType)
	{
	case SAMPLERTYPE_External:
		SamplerTypeFunction = TEXT("ProcessMaterialExternalTextureLookup");
		break;
	case SAMPLERTYPE_Color:
		SamplerTypeFunction = TEXT("ProcessMaterialColorTextureLookup");
		break;
	case SAMPLERTYPE_VirtualColor:
		// has a mobile specific workaround
		SamplerTypeFunction = TEXT("ProcessMaterialVirtualColorTextureLookup");
		break;
	case SAMPLERTYPE_LinearColor:
	case SAMPLERTYPE_VirtualLinearColor:
		SamplerTypeFunction = TEXT("ProcessMaterialLinearColorTextureLookup");
		break;
	case SAMPLERTYPE_Alpha:
	case SAMPLERTYPE_VirtualAlpha:
	case SAMPLERTYPE_DistanceFieldFont:
		SamplerTypeFunction = TEXT("ProcessMaterialAlphaTextureLookup");
		break;
	case SAMPLERTYPE_Grayscale:
	case SAMPLERTYPE_VirtualGrayscale:
		SamplerTypeFunction = TEXT("ProcessMaterialGreyscaleTextureLookup");
		break;
	case SAMPLERTYPE_LinearGrayscale:
	case SAMPLERTYPE_VirtualLinearGrayscale:
		SamplerTypeFunction = TEXT("ProcessMaterialLinearGreyscaleTextureLookup");
		break;
	case SAMPLERTYPE_Normal:
	case SAMPLERTYPE_VirtualNormal:
		// Normal maps need to be unpacked in the pixel shader.
		SamplerTypeFunction = TEXT("UnpackNormalMap");
		break;
	case SAMPLERTYPE_Masks:
	case SAMPLERTYPE_VirtualMasks:
	case SAMPLERTYPE_Data:
		SamplerTypeFunction = TEXT("");
		break;
	default:
		check(0);
		break;
	}

	FMaterialTextureParameterInfo TextureParameterInfo;
	TextureParameterInfo.ParameterInfo = Declaration->Name;
	TextureParameterInfo.TextureIndex = Context.Material->GetReferencedTextures().Find(Desc.Texture);
	TextureParameterInfo.SamplerSource = SamplerSource;
	check(TextureParameterInfo.TextureIndex != INDEX_NONE);

	const int32 ParameterIndex = Context.MaterialCompilationOutput->UniformExpressionSet.FindOrAddTextureParameter(TextureType, TextureParameterInfo);
	const FString TextureName = FString::Printf(TEXT("Material.%s_%u"), TextureTypeName, ParameterIndex);

	FString SamplerStateCode;
	bool AutomaticViewMipBias = false; // TODO
	bool RequiresManualViewMipBias = AutomaticViewMipBias;

	if (!bVirtualTexture) //VT does not have explict samplers (and always requires manual view mip bias)
	{
		if (SamplerSource == SSM_FromTextureAsset)
		{
			SamplerStateCode = FString::Printf(TEXT("%sSampler"), *TextureName);
		}
		else if (SamplerSource == SSM_Wrap_WorldGroupSettings)
		{
			// Use the shared sampler to save sampler slots
			const TCHAR* SharedSamplerName = AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearWrapedSampler") : TEXT("Material.Wrap_WorldGroupSettings");
			SamplerStateCode = FString::Printf(TEXT("GetMaterialSharedSampler(%sSampler,%s)"), *TextureName, SharedSamplerName);
			RequiresManualViewMipBias = false;
		}
		else if (SamplerSource == SSM_Clamp_WorldGroupSettings)
		{
			// Use the shared sampler to save sampler slots
			const TCHAR* SharedSamplerName = AutomaticViewMipBias ? TEXT("View.MaterialTextureBilinearClampedSampler") : TEXT("Material.Clamp_WorldGroupSettings");
			SamplerStateCode = FString::Printf(TEXT("GetMaterialSharedSampler(%sSampler,%s)"), *TextureName, SharedSamplerName);
			RequiresManualViewMipBias = false;
		}
	}

	FEmitShaderExpression* TexCoordValue = TexCoordExpression->GetValueShader(Context, Scope, Shader::EValueType::Float2);
	FEmitShaderExpression* TexCoordValueDdx = nullptr;
	FEmitShaderExpression* TexCoordValueDdy = nullptr;
	if (TexCoordDerivatives.IsValid())
	{
		TexCoordValueDdx = TexCoordDerivatives.ExpressionDdx->GetValueShader(Context, Scope, Shader::EValueType::Float2);
		TexCoordValueDdy = TexCoordDerivatives.ExpressionDdy->GetValueShader(Context, Scope, Shader::EValueType::Float2);
		OutResult.Code = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%(%Grad(%, %, %, %, %))"),
			SamplerTypeFunction,
			SampleFunctionName,
			*TextureName,
			*SamplerStateCode,
			TexCoordValue,
			TexCoordValueDdx,
			TexCoordValueDdy);
	}
	else
	{
		OutResult.Code = Context.EmitExpression(Scope, Shader::EValueType::Float4, TEXT("%(%(%, %, %))"),
			SamplerTypeFunction,
			SampleFunctionName,
			*TextureName,
			*SamplerStateCode,
			TexCoordValue);
	}
	
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

	//StructPreparedType.SetForwardValue(RequestedStructType, StructExpression);
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

	StructPreparedType.SetForwardValue(RequestedStructType, StructExpression);
	FieldPreparedType.SetForwardValue(RequestedFieldType, FieldExpression);

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
	FEmitShaderExpression* FieldValue = FieldExpression->GetValueShader(Context, Scope, RequestedFieldType, Field->Type);

	if (StructEvaluation == EExpressionEvaluation::None)
	{
		check(FieldEvaluation != EExpressionEvaluation::None);

		// StructExpression is not used, so default to a zero-initialized struct
		// This will happen if all the accessed struct fields are explicitly defined
		OutResult.Code = Context.EmitExpression(Scope, StructType, TEXT("%_Set%((%)0, %)"),
			StructType->Name,
			Field->Name,
			StructType->Name,
			FieldValue);
	}
	else
	{
		FEmitShaderExpression* StructValue = StructExpression->GetValueShader(Context, Scope, RequestedStructType);
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
	if (ConditionType.GetEvaluation(Scope, ERequestedType::Scalar) == EExpressionEvaluation::Constant)
	{
		const bool bCondition = ConditionExpression->GetValueConstant(Context, Scope, Shader::EValueType::Bool1).AsBoolScalar();
		return OutResult.SetForwardValue(Context, Scope, RequestedType, bCondition ? TrueExpression : FalseExpression);
	}

	const FPreparedType& LhsType = Context.PrepareExpression(FalseExpression, Scope, RequestedType);
	const FPreparedType& RhsType = Context.PrepareExpression(TrueExpression, Scope, RequestedType);
	
	if (LhsType.ValueComponentType != RhsType.ValueComponentType ||
		LhsType.StructType != RhsType.StructType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	return OutResult.SetType(Context, RequestedType, MergePreparedTypes(LhsType, RhsType));
}

void FExpressionSelect::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const Shader::FType LocalType = GetType();
	FEmitShaderExpression* TrueValue = TrueExpression->GetValueShader(Context, Scope, RequestedType, LocalType);
	FEmitShaderExpression* FalseValue = FalseExpression->GetValueShader(Context, Scope, RequestedType, LocalType);

	OutResult.Code = Context.EmitExpression(Scope, LocalType, TEXT("(% ? % : %)"),
		ConditionExpression->GetValueShader(Context, Scope, Shader::EValueType::Bool1),
		TrueValue,
		FalseValue);
}

void FExpressionSelect::EmitValuePreshader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValuePreshaderResult& OutResult) const
{
	check(false); // TODO
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

FExpressionOperation::FExpressionOperation(EOperation InOp, TConstArrayView<FExpression*> InInputs) : Op(InOp)
{
	const FOperationDescription OpDesc = GetOperationDescription(InOp);
	check(OpDesc.NumInputs == InInputs.Num());
	for (int32 i = 0; i < OpDesc.NumInputs; ++i)
	{
		Inputs[i] = InInputs[i];
		check(Inputs[i]);
	}
}

void FExpressionOperation::ComputeAnalyticDerivatives(FTree& Tree, FExpressionDerivatives& OutResult) const
{
	// Operations with constant derivatives
	switch (Op)
	{
	case EOperation::Less:
	case EOperation::Greater:
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
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FExpression* PrevFrameInputs[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		PrevFrameInputs[Index] = Tree.GetPreviousFrame(Inputs[Index], RequestedType);
	}

	return Tree.NewExpression<FExpressionOperation>(Op, MakeArrayView(PrevFrameInputs, OpDesc.NumInputs));
}

bool FExpressionOperation::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	FRequestedType InputRequestedType[MaxInputs];
	bool bIsMatrixOperation = false;
	switch (Op)
	{
	case EOperation::Length:
	case EOperation::Normalize:
		// Each component of result is influenced by all components of input
		InputRequestedType[0] = ERequestedType::Vector4;
		break;
	case EOperation::VecMulMatrix3:
	case EOperation::VecMulMatrix4:
		InputRequestedType[0] = ERequestedType::Vector3;
		InputRequestedType[1] = ERequestedType::Matrix4x4;
		bIsMatrixOperation = true;
		break;
	case EOperation::Matrix3MulVec:
	case EOperation::Matrix4MulVec:
		InputRequestedType[0] = ERequestedType::Matrix4x4;
		InputRequestedType[1] = ERequestedType::Vector3;
		bIsMatrixOperation = true;
		break;
	case EOperation::Dot:
		InputRequestedType[0] = ERequestedType::Vector4;
		InputRequestedType[1] = ERequestedType::Vector4;
		break;
	default:
		for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
		{
			InputRequestedType[Index] = RequestedType;
		}
		break;
	}

	FPreparedType InputType[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputType[Index] = Context.PrepareExpression(Inputs[Index], Scope, InputRequestedType[Index]);
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
	if (bIsMatrixOperation)
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
		if (InputType[0].GetNumComponents() != 1 && InputType[1].GetNumComponents() != 1 && InputType[0].GetNumComponents() != InputType[1].GetNumComponents())
		{
			return Context.Errors->AddErrorf(TEXT("Invalid arithmetic between %s and %s"), InputType[0].GetType().GetName(), InputType[1].GetType().GetName());
		}
		ResultType = MergePreparedTypes(InputType[0], InputType[1]);
		if (Op == EOperation::Less || Op == EOperation::Greater)
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

namespace Private
{
struct FOperationTypes
{
	Shader::EValueType InputType[FExpressionOperation::MaxInputs];
	Shader::EValueType ResultType;
	bool bIsLWC;
};
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
	case EOperation::Frac:
	case EOperation::Rcp:
		Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
		break;
	case EOperation::Less:
	case EOperation::Greater:
		Types.ResultType = Shader::MakeValueType(Shader::EValueComponentType::Bool, NumComponents);
		break;
	case EOperation::Fmod:
		Types.InputType[1] = Types.ResultType = Shader::MakeNonLWCType(IntermediateType);
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

void FExpressionOperation::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FOperationDescription OpDesc = GetOperationDescription(Op);
	Shader::EValueType InputTypes[MaxInputs];
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputTypes[Index] = Inputs[Index]->GetType();
	}
	const Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(InputTypes, OpDesc.NumInputs));
	FEmitShaderExpression* InputValue[MaxInputs] = { nullptr };
	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		InputValue[Index] = Inputs[Index]->GetValueShader(Context, Scope, Types.InputType[Index]);
	}

	switch (Op)
	{
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
	case EOperation::VecMulMatrix3:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMlutiplyVector(%, %)"), InputValue[0], InputValue[1]);
		}
		else
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("mul(%, (float3x3)%)"), InputValue[0], InputValue[1]);
		}
		break;
	case EOperation::VecMulMatrix4:
		if (Types.bIsLWC)
		{
			OutResult.Code = Context.EmitExpression(Scope, Types.ResultType, TEXT("LWCMlutiply(%, %)"), InputValue[0], InputValue[1]);
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
	const Private::FOperationTypes Types = Private::GetOperationTypes(Op, MakeArrayView(InputTypes, OpDesc.NumInputs));
	check(OpDesc.PreshaderOpcode != Shader::EPreshaderOpcode::Nop);

	for (int32 Index = 0; Index < OpDesc.NumInputs; ++Index)
	{
		Inputs[Index]->GetValuePreshader(Context, Scope, Types.InputType[Index], OutResult.Preshader);
	}

	check(Context.PreshaderStackPosition > 0);
	Context.PreshaderStackPosition--;

	OutResult.Preshader.WriteOpcode(OpDesc.PreshaderOpcode);
	OutResult.Type = Types.ResultType;
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
	const FPreparedType& InputType = Context.PrepareExpression(Input, Scope, RequestedInputType);

	FPreparedType ResultType(InputType.ValueComponentType);
	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
		ResultType.SetComponent(ComponentIndex, InputType.GetComponent(SwizzledComponentIndex));
	}

	return OutResult.SetType(Context, RequestedType, ResultType);
}

void FExpressionSwizzle::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const FRequestedType RequestedInputType = Parameters.GetRequestedInputType(RequestedType);

	static const TCHAR ComponentName[] = { 'x', 'y', 'z', 'w' };
	TCHAR Swizzle[5] = TEXT("");
	TCHAR LWCSwizzle[10] = TEXT("");
	bool bHasSwizzleReorder = false;

	for (int32 ComponentIndex = 0; ComponentIndex < Parameters.NumComponents; ++ComponentIndex)
	{
		const int32 SwizzledComponentIndex = Parameters.ComponentIndex[ComponentIndex];
		Swizzle[ComponentIndex] = ComponentName[SwizzledComponentIndex];
		LWCSwizzle[ComponentIndex * 2 + 0] = TEXT(',');
		LWCSwizzle[ComponentIndex * 2 + 1] = TEXT('0') + SwizzledComponentIndex;

		if (SwizzledComponentIndex != ComponentIndex)
		{
			bHasSwizzleReorder = true;
		}
	}

	FEmitShaderExpression* InputValue = Input->GetValueShader(Context, Scope, RequestedInputType);
	if (bHasSwizzleReorder || Parameters.NumComponents != InputValue->Type.GetNumComponents())
	{
		const Shader::FValueTypeDescription InputTypeDesc = Shader::GetValueTypeDescription(InputValue->Type);
		const Shader::EValueType ResultType = Shader::MakeValueType(InputTypeDesc.ComponentType, Parameters.NumComponents);
		if (InputTypeDesc.ComponentType == Shader::EValueComponentType::Double)
		{
			OutResult.Code = Context.EmitInlineExpression(Scope, ResultType, TEXT("LWCSwizzle(%%)"),
				InputValue,
				LWCSwizzle);
		}
		else
		{
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

	FRequestedType RhsRequestedType;
	for (int32 Index = NumLhsComponents; Index < NumRequestedComponents; ++Index)
	{
		RhsRequestedType.SetComponentRequest(Index - NumLhsComponents, RequestedType.IsComponentRequested(Index));
	}

	const FPreparedType& RhsType = Context.PrepareExpression(Rhs, Scope, RhsRequestedType);
	const int32 NumRhsComponents = FMath::Min(RhsType.GetNumComponents(), NumRequestedComponents - NumLhsComponents);

	if (LhsType.ValueComponentType != RhsType.ValueComponentType)
	{
		return Context.Errors->AddError(TEXT("Type mismatch"));
	}

	FPreparedType ResultType(LhsType.ValueComponentType);
	for (int32 Index = 0; Index < NumLhsComponents; ++Index)
	{
		ResultType.SetComponent(Index, LhsType.GetComponent(Index));
	}
	for (int32 Index = 0; Index < NumRhsComponents; ++Index)
	{
		ResultType.SetComponent(NumLhsComponents + Index, LhsType.GetComponent(Index));
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
	bool bIsLWC;
};
FAppendTypes GetAppendTypes(Shader::EValueType LhsType, Shader::EValueType RhsType)
{
	const Shader::FValueTypeDescription LhsTypeDesc = Shader::GetValueTypeDescription(LhsType);
	const Shader::FValueTypeDescription RhsTypeDesc = Shader::GetValueTypeDescription(RhsType);
	const Shader::EValueComponentType ComponentType = Shader::CombineComponentTypes(LhsTypeDesc.ComponentType, RhsTypeDesc.ComponentType);
	const int32 NumComponents = FMath::Min(LhsTypeDesc.NumComponents + RhsTypeDesc.NumComponents, 4);

	FAppendTypes Types;
	Types.ResultType = Shader::MakeValueType(ComponentType, NumComponents);
	Types.LhsType = Shader::MakeValueType(ComponentType, LhsTypeDesc.NumComponents);
	Types.RhsType = Shader::MakeValueType(ComponentType, FMath::Min<int32>(RhsTypeDesc.NumComponents, 4 - LhsTypeDesc.NumComponents));
	Types.bIsLWC = ComponentType == Shader::EValueComponentType::Double;
	return Types;
}
} // namespace Private

void FExpressionAppend::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	const Private::FAppendTypes Types = Private::GetAppendTypes(Lhs->GetType(), Rhs->GetType());
	FEmitShaderExpression* LhsValue = Lhs->GetValueShader(Context, Scope, Types.LhsType);

	if (Types.RhsType == Shader::EValueType::Void)
	{
		OutResult.Code = LhsValue;
	}
	else
	{
		FEmitShaderExpression* RhsValue = Rhs->GetValueShader(Context, Scope, Types.RhsType);
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
	const Private::FAppendTypes Types = Private::GetAppendTypes(Lhs->GetType(), Rhs->GetType());
	Lhs->GetValuePreshader(Context, Scope, Types.LhsType, OutResult.Preshader);
	if (Types.RhsType != Shader::EValueType::Void)
	{
		Rhs->GetValuePreshader(Context, Scope, Types.RhsType, OutResult.Preshader);

		check(Context.PreshaderStackPosition > 0);
		Context.PreshaderStackPosition--;

		OutResult.Preshader.WriteOpcode(Shader::EPreshaderOpcode::AppendVector);
	}
	OutResult.Type = Types.ResultType;
}

bool FExpressionReflectionVector::PrepareValue(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FPrepareValueResult& OutResult) const
{
	Context.bReadMaterialNormal = true;
	return OutResult.SetType(Context, RequestedType, EExpressionEvaluation::Shader, Shader::EValueType::Float3);
}

void FExpressionReflectionVector::EmitValueShader(FEmitContext& Context, FEmitScope& Scope, const FRequestedType& RequestedType, FEmitValueShaderResult& OutResult) const
{
	OutResult.Code = Context.EmitInlineExpression(Scope, Shader::EValueType::Float3, TEXT("Parameters.ReflectionVector"));
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
