// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"

#if WITH_EDITOR

#include "MaterialHLSLGenerator.h"
#include "Misc/MemStackUtility.h"
#include "HLSLTree/HLSLTree.h"
#include "HLSLTree/HLSLTreeCommon.h"

#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"
#include "Materials/MaterialExpressionGenericConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionStaticBool.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionDoubleVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionDeltaTime.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionSceneTexelSize.h"
#include "Materials/MaterialExpressionViewSize.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionFeatureLevelSwitch.h"
#include "Materials/MaterialExpressionGetLocal.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionBinaryOp.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSetLocal.h"
#include "Materials/MaterialExpressionIfThenElse.h"
#include "Materials/MaterialExpressionForLoop.h"
#include "Materials/MaterialExpressionWhileLoop.h"
#include "Materials/MaterialFunctionInterface.h"

bool UMaterialExpression::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	return Generator.GetErrors().AddError(TEXT("Node does not support expressions"));
}

bool UMaterialExpression::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	return Generator.GetErrors().AddError(TEXT("Node does not support statements"));
}

bool UMaterialExpression::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	return Generator.GetErrors().AddError(TEXT("Node does not support textures"));
}

bool UMaterialExpressionGenericConstant::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(GetConstantValue());
	return true;
}

bool UMaterialExpressionConstant::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(R);
	return true;
}

bool UMaterialExpressionConstant2Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(UE::Shader::FValue(R, G));
	return true;
}

bool UMaterialExpressionConstant3Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(UE::Shader::FValue(Constant.R, Constant.G, Constant.B));
	return true;
}

bool UMaterialExpressionConstant4Vector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant(UE::Shader::FValue(Constant.R, Constant.G, Constant.B, Constant.A));
	return true;
}

bool UMaterialExpressionStaticBool::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewConstant((bool)Value);
	return true;
}

bool UMaterialExpressionStaticSwitch::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ConditionExpression = Value.AcquireHLSLExpressionOrConstant(Generator, Scope, (bool)DefaultValue);
	UE::HLSLTree::FExpression* TrueExpression = A.TryAcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* FalseExpression = B.TryAcquireHLSLExpression(Generator, Scope);

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSelect>(ConditionExpression, TrueExpression, FalseExpression);
	return true;
}

bool UMaterialExpressionFeatureLevelSwitch::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	const ERHIFeatureLevel::Type FeatureLevelToCompile = Generator.GetCompileTarget().FeatureLevel;
	check(FeatureLevelToCompile < UE_ARRAY_COUNT(Inputs));
	FExpressionInput& FeatureInput = Inputs[FeatureLevelToCompile];

	if (!Default.GetTracedInput().Expression)
	{
		return Generator.GetErrors().AddError(TEXT("Feature Level switch missing default input"));
	}

	if (FeatureInput.GetTracedInput().Expression)
	{
		OutExpression = FeatureInput.AcquireHLSLExpression(Generator, Scope);
	}
	else
	{
		OutExpression = Default.AcquireHLSLExpression(Generator, Scope);
	}
	return OutExpression != nullptr;
}

bool UMaterialExpressionGetLocal::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().AcquireLocal(Scope, LocalName);
	if (!OutExpression)
	{
		return Generator.GetErrors().AddError(TEXT("Local accessed before assigned"));
	}
	return true;
}

bool UMaterialExpressionVectorParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::Vector, ParameterName, DefaultValue);
	return true;
}

bool UMaterialExpressionDoubleVectorParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::DoubleVector, ParameterName, DefaultValue);
	return true;
}

bool UMaterialExpressionScalarParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::Scalar, ParameterName, DefaultValue);
	return true;
}

bool UMaterialExpressionStaticBoolParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionMaterialParameter>(EMaterialParameterType::StaticSwitch, ParameterName, (bool)DefaultValue);
	return true;
}

bool UMaterialExpressionPixelDepth::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(EExternalInput::PixelDepth);
	return true;
}

bool UMaterialExpressionWorldPosition::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	EExternalInput InputType = EExternalInput::None;
	switch (WorldPositionShaderOffset)
	{
	case WPT_Default: InputType = EExternalInput::WorldPosition; break;
	case WPT_ExcludeAllShaderOffsets: InputType = EExternalInput::WorldPosition_NoOffsets; break;
	case WPT_CameraRelative: InputType = EExternalInput::TranslatedWorldPosition; break;
	case WPT_CameraRelativeNoOffsets: InputType = EExternalInput::TranslatedWorldPosition_NoOffsets; break;
	default: checkNoEntry(); break;
	}

	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(InputType);
	return true;
}

bool UMaterialExpressionCameraPositionWS::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(EExternalInput::CameraWorldPosition);
	return true;
}

bool UMaterialExpressionTime::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	if (bOverride_Period && Period == 0.0f)
	{
		OutExpression = Generator.NewConstant(0.0f);
		return true;
	}

	EExternalInput InputType = bIgnorePause ? EExternalInput::RealTime : EExternalInput::GameTime;
	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(InputType);
	if (bOverride_Period)
	{
		OutExpression = Generator.GetTree().NewFmod(OutExpression, Generator.NewConstant(Period));
	}
	return true;
}

bool UMaterialExpressionDeltaTime::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionExternalInput>(UE::HLSLTree::EExternalInput::DeltaTime);
	return true;
}

bool UMaterialExpressionScreenPosition::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	const EExternalInput InputType = (OutputIndex == 1) ? EExternalInput::PixelPosition : EExternalInput::ViewportUV;
	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(InputType);
	return true;
}

bool UMaterialExpressionSceneTexelSize::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	// To make sure any material that were correctly handling BufferUV != ViewportUV, we just lie to material
	// to make it believe ViewSize == BufferSize, so they are still compatible with SceneTextureLookup().
	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(EExternalInput::RcpViewSize);
	return true;
}

bool UMaterialExpressionViewSize::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	OutExpression = Generator.GetTree().NewExpression<FExpressionExternalInput>(EExternalInput::ViewSize);
	return true;
}

bool UMaterialExpressionPanner::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	FExpression* ExpressionTime = Time.TryAcquireHLSLExpression(Generator, Scope);
	if (!ExpressionTime)
	{
		ExpressionTime = Generator.GetTree().NewExpression<FExpressionExternalInput>(EExternalInput::GameTime);
	}
	FExpression* ExpressionSpeed = Speed.AcquireHLSLExpressionOrConstant(Generator, Scope, FVector2f(SpeedX, SpeedY));
	FExpression* ExpressionOffset = Generator.GetTree().NewMul(ExpressionSpeed, ExpressionTime);
	if (bFractionalPart)
	{
		ExpressionOffset = Generator.GetTree().NewFrac(ExpressionOffset);
	}
	FExpression* ExpressionTexCoord = Coordinate.TryAcquireHLSLExpression(Generator, Scope);
	if (!ExpressionTexCoord)
	{
		ExpressionTexCoord = Generator.NewTexCoord(ConstCoordinate);
	}

	OutExpression = Generator.GetTree().NewAdd(ExpressionTexCoord, ExpressionOffset);
	return true;
}

bool UMaterialExpressionTextureCoordinate::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.NewTexCoord(CoordinateIndex);

	// TODO - unmirror

	// Depending on whether we have U and V scale values that differ, we can perform a multiply by either
	// a scalar or a float2.  These tiling values are baked right into the shader node, so they're always
	// known at compile time.
	// Avoid emitting the multiply by 1.0f if possible
	// This should make generated HLSL a bit cleaner, but more importantly will help avoid generating redundant virtual texture stacks
	if (FMath::Abs(UTiling - VTiling) > SMALL_NUMBER)
	{
		OutExpression = Generator.GetTree().NewMul(OutExpression, Generator.NewConstant(FVector2f(UTiling, VTiling)));
	}
	else if (FMath::Abs(1.0f - UTiling) > SMALL_NUMBER)
	{
		OutExpression = Generator.GetTree().NewMul(OutExpression, Generator.NewConstant(UTiling));
	}

	return true;
}

bool UMaterialExpressionTextureObject::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
	OutTexture = Generator.AcquireTextureDeclaration(TextureDesc);
	return true;
}

bool UMaterialExpressionTextureObjectParameter::GenerateHLSLTexture(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FTextureParameterDeclaration*& OutTexture)
{
	const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
	OutTexture = Generator.AcquireTextureParameterDeclaration(ParameterName, TextureDesc);
	return true;
}

bool UMaterialExpressionTextureSample::GenerateHLSLExpressionBase(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	if (!TextureDeclaration)
	{
		return Generator.GetErrors().AddError(TEXT("Missing input texture"));
	}

	FExpression* TexCoordExpression = Coordinates.GetTracedInput().Expression ? Coordinates.TryAcquireHLSLExpression(Generator, Scope) : Generator.NewTexCoord(ConstCoordinate);
	const FExpressionDerivatives TexCoordDerivatives;// = Generator.GetTree().GetAnalyticDerivatives(TexCoordExpression);
	OutExpression = Generator.GetTree().NewExpression<FExpressionTextureSample>(TextureDeclaration, TexCoordExpression, TexCoordDerivatives, SamplerSource, MipValueMode);
	return true;
}

bool UMaterialExpressionTextureSample::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration = nullptr;
	if (TextureObject.GetTracedInput().Expression)
	{
		TextureDeclaration = TextureObject.AcquireHLSLTexture(Generator, Scope);
	}
	else if (Texture)
	{
		const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
		TextureDeclaration = Generator.AcquireTextureDeclaration(TextureDesc);
	}

	return GenerateHLSLExpressionBase(Generator, Scope, TextureDeclaration, OutExpression);
}

bool UMaterialExpressionTextureSampleParameter::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FTextureParameterDeclaration* TextureDeclaration = nullptr;
	if (ParameterName.IsNone() && TextureObject.GetTracedInput().Expression)
	{
		TextureDeclaration = TextureObject.AcquireHLSLTexture(Generator, Scope);
	}
	else if (Texture)
	{
		const UE::HLSLTree::FTextureDescription TextureDesc(Texture, SamplerType);
		TextureDeclaration = Generator.AcquireTextureParameterDeclaration(ParameterName, TextureDesc);
	}

	return GenerateHLSLExpressionBase(Generator, Scope, TextureDeclaration, OutExpression);
}

bool UMaterialExpressionSceneTexture::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	if (OutputIndex == 0)
	{
		FExpression* ExpressionTexCoord = nullptr;
		if (Coordinates.GetTracedInput().Expression)
		{
			ExpressionTexCoord = Coordinates.AcquireHLSLExpression(Generator, Scope);
		}
		OutExpression = Generator.GetTree().NewExpression<FExpressionMaterialSceneTexture>(ExpressionTexCoord, SceneTextureId, bFiltered);
		return true;
	}
	else if (OutputIndex == 1 || OutputIndex == 2)
	{
		//return Compiler->GetSceneTextureViewSize(SceneTextureId, /* InvProperty = */ OutputIndex == 2);
		return false; // TODO
	}

	return Generator.GetErrors().AddError(TEXT("Invalid input parameter"));
}

bool UMaterialExpressionOneMinus::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* InputExpression = Input.AcquireHLSLExpression(Generator, Scope);
	if (!InputExpression)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewSub(Generator.NewConstant(1.0f), InputExpression);
	return true;
}

bool UMaterialExpressionBinaryOp::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}

	OutExpression = Generator.GetTree().NewBinaryOp(GetBinaryOp(), Lhs, Rhs);
	return true;
}

bool UMaterialExpressionAdd::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewAdd(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionSubtract::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewSub(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionMultiply::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewMul(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionDivide::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewDiv(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionDotProduct::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpression(Generator, Scope);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewDot(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionMin::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewMin(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionMax::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewMax(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionClamp::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ExpressionMin = Min.AcquireHLSLExpressionOrConstant(Generator, Scope, MinDefault);
	UE::HLSLTree::FExpression* ExpressionMax = Min.AcquireHLSLExpressionOrConstant(Generator, Scope, MaxDefault);
	UE::HLSLTree::FExpression* ExpressionInput = Input.AcquireHLSLExpression(Generator, Scope);
	if (!ExpressionMin || !ExpressionMax || !ExpressionInput)
	{
		return false;
	}

	if (ClampMode == CMODE_ClampMin || ClampMode == CMODE_Clamp)
	{
		ExpressionInput = Generator.GetTree().NewMax(ExpressionInput, ExpressionMin);
	}
	if (ClampMode == CMODE_ClampMax || ClampMode == CMODE_Clamp)
	{
		ExpressionInput = Generator.GetTree().NewMin(ExpressionInput, ExpressionMax);
	}

	OutExpression = ExpressionInput;
	return true;
}

bool UMaterialExpressionLinearInterpolate::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ExpressionA = A.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstA);
	UE::HLSLTree::FExpression* ExpressionB = B.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstB);
	UE::HLSLTree::FExpression* ExpressionAlpha = Alpha.AcquireHLSLExpressionOrConstant(Generator, Scope, ConstAlpha);
	if (!ExpressionA || !ExpressionB || !ExpressionAlpha)
	{
		return false;
	}

	// A + (B - A) * Alpha
	OutExpression = Generator.GetTree().NewAdd(ExpressionA, Generator.GetTree().NewMul(Generator.GetTree().NewSub(ExpressionB, ExpressionA), ExpressionAlpha));
	return true;
}

bool UMaterialExpressionDistance::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ExpressionA = A.AcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* ExpressionB = B.AcquireHLSLExpression(Generator, Scope);
	if (!ExpressionA || !ExpressionB)
	{
		return false;
	}

	OutExpression = Generator.GetTree().NewLength(Generator.GetTree().NewSub(ExpressionA, ExpressionB));
	return true;
}

bool UMaterialExpressionNormalize::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* ExpressionInput = VectorInput.AcquireHLSLExpression(Generator, Scope);
	if (!ExpressionInput)
	{
		return false;
	}

	OutExpression = Generator.GetTree().NewNormalize(ExpressionInput);
	return true;
}

bool UMaterialExpressionAppendVector::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* Lhs = A.AcquireHLSLExpression(Generator, Scope);
	UE::HLSLTree::FExpression* Rhs = B.AcquireHLSLExpression(Generator, Scope);
	if (!Lhs || !Rhs)
	{
		return false;
	}
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionAppend>(Lhs, Rhs);
	return true;
}

bool UMaterialExpressionComponentMask::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* InputExpression = Input.AcquireHLSLExpression(Generator, Scope);
	if (!InputExpression)
	{
		return false;
	}

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSwizzle>(UE::HLSLTree::MakeSwizzleMask(!!R, !!G, !!B, !!A), InputExpression);
	return true;
}


bool UMaterialExpressionGetMaterialAttributes::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* AttributesExpression = MaterialAttributes.AcquireHLSLExpression(Generator, Scope);
	if (!AttributesExpression)
	{
		return false;
	}
	if (OutputIndex == 0)
	{
		OutExpression = AttributesExpression;
		return true;
	}
	const int32 AttributeIndex = OutputIndex - 1;
	if (!AttributeGetTypes.IsValidIndex(AttributeIndex))
	{
		return Generator.GetErrors().AddError(TEXT("Invalid attribute"));
	}

	const FGuid& AttributeID = AttributeGetTypes[AttributeIndex];
	const FString& AttributeName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
	const UE::Shader::FStructField* AttributeField = Generator.GetMaterialAttributesType()->FindFieldByName(*AttributeName);
	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionGetStructField>(Generator.GetMaterialAttributesType(), AttributeField, AttributesExpression);

	return true;

}

bool UMaterialExpressionSetMaterialAttributes::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	UE::HLSLTree::FExpression* AttributesExpression = Inputs[0].AcquireHLSLExpressionOrConstant(Generator, Scope, Generator.GetMaterialAttributesDefaultValue());
	
	for (int32 PinIndex = 0; PinIndex < AttributeSetTypes.Num(); ++PinIndex)
	{
		const FExpressionInput& AttributeInput = Inputs[PinIndex + 1];
		if (AttributeInput.GetTracedInput().Expression)
		{
			const FGuid& AttributeID = AttributeSetTypes[PinIndex];
			// Only compile code to set attributes of the current shader frequency
			const EShaderFrequency AttributeFrequency = FMaterialAttributeDefinitionMap::GetShaderFrequency(AttributeID);
			const FString& AttributeName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
			//if (AttributeFrequency == Compiler->GetCurrentShaderFrequency())
			{
				UE::HLSLTree::FExpression* ValueExpression = AttributeInput.TryAcquireHLSLExpression(Generator, Scope);
				if (ValueExpression)
				{
					const UE::Shader::FStructField* AttributeField = Generator.GetMaterialAttributesType()->FindFieldByName(*AttributeName);
					AttributesExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionSetStructField>(Generator.GetMaterialAttributesType(), AttributeField, AttributesExpression, ValueExpression);
				}
			}
		}
	}

	OutExpression = AttributesExpression;
	return true;
}

bool UMaterialExpressionReflectionVectorWS::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	check(!CustomWorldNormal.GetTracedInput().Expression); // TODO

	OutExpression = Generator.GetTree().NewExpression<UE::HLSLTree::FExpressionReflectionVector>();
	return true;
}

bool UMaterialExpressionFunctionOutput::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	// This should only be called when editing/previewing the function directly
	OutExpression = A.AcquireHLSLExpression(Generator, Scope);
	return true;
}

bool UMaterialExpressionFunctionInput::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.AcquireFunctionInputExpression(Scope, this);
	return true;
}

bool UMaterialExpressionMaterialFunctionCall::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	OutExpression = Generator.GenerateFunctionCall(Scope, MaterialFunction, FunctionInputs, OutputIndex);
	return true;
}

static UE::HLSLTree::FExpression* TransformBase(UE::HLSLTree::FTree& Tree,
	EMaterialCommonBasis SourceCoordBasis,
	EMaterialCommonBasis DestCoordBasis,
	UE::HLSLTree::FExpression* Input,
	bool bWComponent)
{
	using namespace UE::HLSLTree;
	if (!Input)
	{
		// unable to compile
		return nullptr;
	}

	if (SourceCoordBasis == DestCoordBasis)
	{
		// no transformation needed
		return Input;
	}

	FExpression* Result = nullptr;
	EMaterialCommonBasis IntermediaryBasis = MCB_World;
	EOperation Op = bWComponent ? EOperation::VecMulMatrix4 : EOperation::VecMulMatrix3;
	switch (SourceCoordBasis)
	{
	case MCB_Tangent:
	{
		check(!bWComponent);
		if (DestCoordBasis == MCB_World)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::TangentToWorld));
			//CodeStr = TEXT("mul(<A>, Parameters.TangentToWorld)");
		}
		// else use MCB_World as intermediary basis
		break;
	}
	case MCB_Local:
	{
		if (DestCoordBasis == MCB_World)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::LocalToWorld));
			//CodeStr = TEXT("TransformLocal<TO><PREV>World(Parameters, <A>)");
		}
		// else use MCB_World as intermediary basis
		break;
	}
	case MCB_TranslatedWorld:
	{
		if (DestCoordBasis == MCB_World)
		{
			if (bWComponent)
			{
				Result = Tree.NewSub(Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::PreViewTranslation));
				//CodeStr = TEXT("LWCSubtract(<A>, ResolvedView.<PREV>PreViewTranslation)");
			}
			else
			{
				Result = Input;
				//CodeStr = TEXT("<A>");
			}
		}
		else if (DestCoordBasis == MCB_Camera)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::TranslatedWorldToCameraView));
			//CodeStr = MultiplyMatrix(TEXT("<A>"), TEXT("ResolvedView.<PREV>TranslatedWorldToCameraView"), AWComponent);
		}
		else if (DestCoordBasis == MCB_View)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::TranslatedWorldToView));
			//CodeStr = MultiplyMatrix(TEXT("<A>"), TEXT("ResolvedView.<PREV>TranslatedWorldToView"), AWComponent);
		}
		// else use MCB_World as intermediary basis
		break;
	}
	case MCB_World:
	{
		if (DestCoordBasis == MCB_Tangent)
		{
			Result = Tree.NewBinaryOp(Op, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::TangentToWorld), Input);
			//CodeStr = MultiplyTransposeMatrix(TEXT("Parameters.TangentToWorld"), TEXT("<A>"), AWComponent);
		}
		else if (DestCoordBasis == MCB_Local)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::WorldToLocal));
			/*const EMaterialDomain Domain = (const EMaterialDomain)Material->GetMaterialDomain();

			if (Domain != MD_Surface && Domain != MD_Volume)
			{
				// TODO: for decals we could support it
				Errorf(TEXT("This transformation is only supported in the 'Surface' material domain."));
				return INDEX_NONE;
			}

			// TODO: inconsistent with TransformLocal<TO>World with instancing
			CodeStr = LWCMultiplyMatrix(TEXT("<A>"), TEXT("GetPrimitiveData(Parameters).<PREVIOUS>WorldToLocal"), AWComponent);*/
		}
		else if (DestCoordBasis == MCB_TranslatedWorld)
		{
			if (bWComponent)
			{
				// TODO - explicit cast to float
				Result = Tree.NewAdd(Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::PreViewTranslation));
				//CodeStr = TEXT("LWCToFloat(LWCAdd(<A>, ResolvedView.<PREV>PreViewTranslation))");
			}
			else
			{
				Result = Input;
				//CodeStr = TEXT("<A>");
			}
		}
		else if (DestCoordBasis == MCB_MeshParticle)
		{
			//CodeStr = LWCMultiplyMatrix(TEXT("<A>"), TEXT("Parameters.Particle.WorldToParticle"), AWComponent);
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::WorldToParticle));
			//bUsesParticleWorldToLocal = true;
		}
		else if (DestCoordBasis == MCB_Instance)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::WorldToInstance));
			//CodeStr = LWCMultiplyMatrix(TEXT("<A>"), TEXT("GetWorldToInstance(Parameters)"), AWComponent);
			//bUsesInstanceWorldToLocalPS = ShaderFrequency == SF_Pixel;
		}

		// else use MCB_TranslatedWorld as intermediary basis
		IntermediaryBasis = MCB_TranslatedWorld;
		break;
	}
	case MCB_Camera:
	{
		if (DestCoordBasis == MCB_TranslatedWorld)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::CameraViewToTranslatedWorld));
			//CodeStr = MultiplyMatrix(TEXT("<A>"), TEXT("ResolvedView.<PREV>CameraViewToTranslatedWorld"), AWComponent);
		}
		// else use MCB_TranslatedWorld as intermediary basis
		IntermediaryBasis = MCB_TranslatedWorld;
		break;
	}
	case MCB_View:
	{
		if (DestCoordBasis == MCB_TranslatedWorld)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::ViewToTranslatedWorld));
			//CodeStr = MultiplyMatrix(TEXT("<A>"), TEXT("ResolvedView.<PREV>ViewToTranslatedWorld"), AWComponent);
		}
		// else use MCB_TranslatedWorld as intermediary basis
		IntermediaryBasis = MCB_TranslatedWorld;
		break;
	}
	case MCB_MeshParticle:
	{
		if (DestCoordBasis == MCB_World)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::ParticleToWorld));
			//CodeStr = LWCMultiplyMatrix(TEXT("<A>"), TEXT("Parameters.Particle.ParticleToWorld"), AWComponent);
			//bUsesParticleLocalToWorld = true;
		}
		// use World as an intermediary base
		break;
	}
	case MCB_Instance:
	{
		if (DestCoordBasis == MCB_World)
		{
			Result = Tree.NewBinaryOp(Op, Input, Tree.NewExpression<FExpressionExternalInput>(EExternalInput::InstanceToWorld));
			//CodeStr = LWCMultiplyMatrix(TEXT("<A>"), TEXT("GetInstanceToWorld(Parameters)"), AWComponent);
			//bUsesInstanceLocalToWorldPS = ShaderFrequency == SF_Pixel;
		}
		// use World as an intermediary base
		break;
	}

	default:
		check(0);
		break;
	}

	if (!Result)
	{
		// check intermediary basis so we don't have infinite recursion
		check(IntermediaryBasis != SourceCoordBasis);
		check(IntermediaryBasis != DestCoordBasis);

		// use intermediary basis
		FExpression* IntermediaryExpression = TransformBase(Tree, SourceCoordBasis, IntermediaryBasis, Input, bWComponent);
		return TransformBase(Tree, IntermediaryBasis, DestCoordBasis, IntermediaryExpression, bWComponent);
	}

	return Result;
}

bool UMaterialExpressionTransform::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	static const EMaterialCommonBasis kTable[TRANSFORM_MAX] = {
		MCB_Tangent,					// TRANSFORM_Tangent
		MCB_Local,						// TRANSFORM_Local
		MCB_World,						// TRANSFORM_World
		MCB_View,						// TRANSFORM_View
		MCB_Camera,						// TRANSFORM_Camera
		MCB_MeshParticle,				// TRANSFORM_Particle
		MCB_Instance,					// TRANSFORM_Instance
	};

	FExpression* ExpressionInput = Input.AcquireHLSLExpression(Generator, Scope);
	if (!ExpressionInput)
	{
		return false;
	}

	OutExpression = TransformBase(Generator.GetTree(), kTable[TransformSourceType], kTable[TransformType], ExpressionInput, false);
	return true;
}


bool UMaterialExpressionTransformPosition::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;

	static const EMaterialCommonBasis kTable[TRANSFORMPOSSOURCE_MAX] = {
		MCB_Local,						// TRANSFORMPOSSOURCE_Local
		MCB_World,						// TRANSFORMPOSSOURCE_World
		MCB_TranslatedWorld,			// TRANSFORMPOSSOURCE_TranslatedWorld
		MCB_View,						// TRANSFORMPOSSOURCE_View
		MCB_Camera,						// TRANSFORMPOSSOURCE_Camera
		MCB_MeshParticle,				// TRANSFORMPOSSOURCE_Particle
		MCB_Instance,					// TRANSFORMPOSSOURCE_Instance
	};

	FExpression* ExpressionInput = Input.AcquireHLSLExpression(Generator, Scope);
	if (!ExpressionInput)
	{
		return false;
	}

	OutExpression = TransformBase(Generator.GetTree(), kTable[TransformSourceType], kTable[TransformType], ExpressionInput, true);
	return true;
}

static UE::Shader::FType GetCustomOutputType(const FMaterialHLSLGenerator& Generator, ECustomMaterialOutputType Type)
{
	using namespace UE::Shader;
	switch (Type)
	{
	case CMOT_Float1: return EValueType::Float1;
	case CMOT_Float2: return EValueType::Float2;
	case CMOT_Float3: return EValueType::Float3;
	case CMOT_Float4: return EValueType::Float4;
	case CMOT_MaterialAttributes: return Generator.GetMaterialAttributesType();
	default: checkNoEntry(); return EValueType::Void;
	}
}

bool UMaterialExpressionCustom::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	using namespace UE::HLSLTree;
	using namespace UE::Shader;

	if (OutputIndex < 0 || OutputIndex > AdditionalOutputs.Num())
	{
		return Generator.GetErrors().AddErrorf(TEXT("Invalid output index %d"), OutputIndex);
	}

	FMemStackBase& Allocator = Generator.GetTree().GetAllocator();

	TArray<FCustomHLSLInput, TInlineAllocator<8>> LocalInputs;
	LocalInputs.Reserve(Inputs.Num());
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const FCustomInput& Input = Inputs[Index];
		if (!Input.InputName.IsNone())
		{
			FExpression* Expression = Input.Input.AcquireHLSLExpression(Generator, Scope);
			if (!Expression)
			{
				return false;
			}
			const FStringView InputName = UE::MemStack::AllocateStringView(Allocator, Input.InputName.ToString());
			LocalInputs.Emplace(InputName, Expression);
		}
	}

	TArray<FStructFieldInitializer, TInlineAllocator<8>> OutputFieldInitializers;
	TArray<FString, TInlineAllocator<8>> OutputNames;
	OutputFieldInitializers.Reserve(AdditionalOutputs.Num() + 1);
	OutputNames.Reserve(AdditionalOutputs.Num());

	const FType ReturnType = GetCustomOutputType(Generator, OutputType);
	OutputFieldInitializers.Emplace(TEXT("Default"), ReturnType);
	for (int32 Index = 0; Index < AdditionalOutputs.Num(); ++Index)
	{
		const FCustomOutput& Output = AdditionalOutputs[Index];
		OutputNames.Add(Output.OutputName.ToString());
		OutputFieldInitializers.Emplace(OutputNames.Last(), GetCustomOutputType(Generator, Output.OutputType));
	}

	FString OutputStructName = TEXT("FCustomOutput") + GetName();
	FStructTypeInitializer OutputStructInitializer;
	OutputStructInitializer.Name = OutputStructName;
	OutputStructInitializer.Fields = OutputFieldInitializers;
	const UE::Shader::FStructType* OutputStructType = Generator.GetTypeRegistry().NewType(OutputStructInitializer);

	TStringBuilder<8 * 1024> DeclarationCode;
	for (FCustomDefine DefineEntry : AdditionalDefines)
	{
		if (DefineEntry.DefineName.Len() > 0)
		{
			DeclarationCode.Appendf(TEXT("#ifndef %s\n#define %s %s\n#endif\n"), *DefineEntry.DefineName, *DefineEntry.DefineName, *DefineEntry.DefineValue);
		}
	}

	for (FString IncludeFile : IncludeFilePaths)
	{
		if (IncludeFile.Len() > 0)
		{
			DeclarationCode.Appendf(TEXT("#include \"%s\"\n"), *IncludeFile);
		}
	}

	FStringView FunctionCode;
	if (Code.Contains(TEXT("return")))
	{
		// Can just reference to 'Code' field directly, the UMaterialExpressionCustom lifetime will be longer than the resulting HLSLTree
		FunctionCode = Code;
	}
	else
	{
		TStringBuilder<8 * 1024> FormattedCode;
		FormattedCode.Appendf(TEXT("return %s;"), *Code);
		FunctionCode = UE::MemStack::AllocateStringView(Allocator, FormattedCode.ToView());
	}

	FExpression* ExpressionCustom = Generator.GetTree().NewExpression<FExpressionCustomHLSL>(
		UE::MemStack::AllocateStringView(Allocator, DeclarationCode.ToView()),
		FunctionCode,
		LocalInputs,
		OutputStructType);

	OutExpression = Generator.GetTree().NewExpression<FExpressionGetStructField>(OutputStructType, &OutputStructType->Fields[OutputIndex], ExpressionCustom);
	return true;
}

bool UMaterialExpressionExecBegin::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	Exec.GenerateHLSLStatements(Generator, Scope);
	return true;
}

bool UMaterialExpressionExecEnd::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	Generator.GenerateResult(Scope);
	return true;
}

bool UMaterialExpressionSetLocal::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FExpression* ValueExpression = Value.AcquireHLSLExpression(Generator, Scope);
	if (!ValueExpression)
	{
		return false;
	}

	Generator.GetTree().AssignLocal(Scope, LocalName, ValueExpression);
	Exec.GenerateHLSLStatements(Generator, Scope);
	return true;
}

bool UMaterialExpressionIfThenElse::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	UE::HLSLTree::FExpression* ConditionExpression = Condition.AcquireHLSLExpression(Generator, Scope);
	if (!ConditionExpression)
	{
		return false;
	}

	UE::HLSLTree::FStatementIf* IfStatement = Generator.GetTree().NewStatement<UE::HLSLTree::FStatementIf>(Scope);
	IfStatement->ConditionExpression = ConditionExpression;
	IfStatement->NextScope = Generator.NewJoinedScope(Scope);
	IfStatement->ThenScope = Then.NewOwnedScopeWithStatements(Generator, *IfStatement);
	IfStatement->ElseScope = Else.NewOwnedScopeWithStatements(Generator, *IfStatement);

	return true;
}

bool UMaterialExpressionWhileLoop::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	using namespace UE::HLSLTree;
	if (!Condition.IsConnected())
	{
		return Generator.GetErrors().AddError(TEXT("Missing condition connection"));
	}

	if (!LoopBody.GetExpression())
	{
		return Generator.GetErrors().AddError(TEXT("Missing LoopBody connection"));
	}

	FStatementLoop* LoopStatement = Generator.GetTree().NewStatement<FStatementLoop>(Scope);
	LoopStatement->LoopScope = Generator.NewOwnedScope(*LoopStatement);

	FStatementIf* IfStatement = Generator.GetTree().NewStatement<FStatementIf>(*LoopStatement->LoopScope);
	IfStatement->ThenScope = Generator.NewOwnedScope(*IfStatement);
	IfStatement->ElseScope = Generator.NewOwnedScope(*IfStatement);
	LoopStatement->NextScope = Generator.NewScope(Scope, EMaterialNewScopeFlag::NoPreviousScope);
	LoopStatement->LoopScope->AddPreviousScope(*IfStatement->ThenScope);
	LoopStatement->NextScope->AddPreviousScope(*IfStatement->ElseScope);

	LoopStatement->BreakStatement = Generator.GetTree().NewStatement<FStatementBreak>(*IfStatement->ElseScope);

	IfStatement->ConditionExpression = Condition.AcquireHLSLExpression(Generator, *LoopStatement->LoopScope);
	LoopBody.GenerateHLSLStatements(Generator, *IfStatement->ThenScope);
	Completed.GenerateHLSLStatements(Generator, *LoopStatement->NextScope);

	return true;
}

struct FGlobalExpressionDataForLoop
{
	int32 NumLoops = 0;
};
DECLARE_MATERIAL_HLSLGENERATOR_DATA(FGlobalExpressionDataForLoop);

struct FExpressionDataForLoop
{
	UE::HLSLTree::FScope* LoopScope = nullptr;
	FName LocalName;
};
DECLARE_MATERIAL_HLSLGENERATOR_DATA(FExpressionDataForLoop);

bool UMaterialExpressionForLoop::GenerateHLSLExpression(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope, int32 OutputIndex, UE::HLSLTree::FExpression*& OutExpression)
{
	FExpressionDataForLoop* ExpressionData = Generator.FindExpressionData<FExpressionDataForLoop>(this);
	if (!ExpressionData || !Scope.HasParentScope(*ExpressionData->LoopScope))
	{
		return Generator.GetErrors().AddError(TEXT("For loop index accessed outside loop scope"));
	}

	OutExpression = Generator.GetTree().AcquireLocal(Scope, ExpressionData->LocalName);
	return true;
}

bool UMaterialExpressionForLoop::GenerateHLSLStatements(FMaterialHLSLGenerator& Generator, UE::HLSLTree::FScope& Scope)
{
	using namespace UE::HLSLTree;
	if (!LoopBody.GetExpression())
	{
		return Generator.GetErrors().AddError(TEXT("Missing LoopBody connection"));
	}

	FExpression* StartExpression = StartIndex.AcquireHLSLExpression(Generator, Scope);
	if (!StartExpression)
	{
		return false;
	}

	FExpression* EndExpression = EndIndex.AcquireHLSLExpression(Generator, Scope);
	if (!EndExpression)
	{
		return false;
	}

	FGlobalExpressionDataForLoop* GlobalData = Generator.AcquireGlobalData<FGlobalExpressionDataForLoop>();
	FExpressionDataForLoop* ExpressionData = Generator.NewExpressionData<FExpressionDataForLoop>(this);
	ExpressionData->LocalName = *FString::Printf(TEXT("ForLoopControl%d"), GlobalData->NumLoops++);

	FExpression* StepExpression = IndexStep.AcquireHLSLExpressionOrConstant(Generator, Scope, int32(1));

	Generator.GetTree().AssignLocal(Scope, ExpressionData->LocalName, StartExpression);

	FStatementLoop* LoopStatement = Generator.GetTree().NewStatement<FStatementLoop>(Scope);
	LoopStatement->LoopScope = Generator.NewOwnedScope(*LoopStatement);
	ExpressionData->LoopScope = LoopStatement->LoopScope;

	FStatementIf* IfStatement = Generator.GetTree().NewStatement<FStatementIf>(*LoopStatement->LoopScope);
	IfStatement->ThenScope = Generator.NewOwnedScope(*IfStatement);
	IfStatement->ElseScope = Generator.NewOwnedScope(*IfStatement);
	LoopStatement->NextScope = Generator.NewScope(Scope, EMaterialNewScopeFlag::NoPreviousScope);
	LoopStatement->LoopScope->AddPreviousScope(*IfStatement->ThenScope);
	LoopStatement->NextScope->AddPreviousScope(*IfStatement->ElseScope);

	LoopStatement->BreakStatement = Generator.GetTree().NewStatement<FStatementBreak>(*IfStatement->ElseScope);

	FExpression* LocalExpression = Generator.GetTree().AcquireLocal(*LoopStatement->LoopScope, ExpressionData->LocalName);

	IfStatement->ConditionExpression = Generator.GetTree().NewLess(LocalExpression, EndExpression);
	LoopBody.GenerateHLSLStatements(Generator, *IfStatement->ThenScope);

	FExpression* NewLocalExpression = Generator.GetTree().NewAdd(Generator.GetTree().AcquireLocal(*IfStatement->ThenScope, ExpressionData->LocalName), StepExpression);
	Generator.GetTree().AssignLocal(*IfStatement->ThenScope, ExpressionData->LocalName, NewLocalExpression);

	Completed.GenerateHLSLStatements(Generator, *LoopStatement->NextScope);

	return true;
}

#endif // WITH_EDITOR
