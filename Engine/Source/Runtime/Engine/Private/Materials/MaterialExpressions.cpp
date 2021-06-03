// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialExpressions.cpp - Material expressions implementation.
=============================================================================*/

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Misc/Guid.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/EditorObjectVersion.h"
#include "Misc/App.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectAnnotation.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineGlobals.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "MaterialShared.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureCube.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureRenderTargetCube.h"
#include "Engine/VolumeTexture.h"
#include "Styling/CoreStyle.h"
#include "VT/RuntimeVirtualTexture.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionActorPositionWS.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionArccosine.h"
#include "Materials/MaterialExpressionArccosineFast.h"
#include "Materials/MaterialExpressionArcsine.h"
#include "Materials/MaterialExpressionArcsineFast.h"
#include "Materials/MaterialExpressionArctangent.h"
#include "Materials/MaterialExpressionArctangentFast.h"
#include "Materials/MaterialExpressionArctangent2.h"
#include "Materials/MaterialExpressionArctangent2Fast.h"
#include "Materials/MaterialExpressionAtmosphericFogColor.h"
#include "Materials/MaterialExpressionBentNormalCustomOutput.h"
#include "Materials/MaterialExpressionBlackBody.h"
#include "Materials/MaterialExpressionBlendMaterialAttributes.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionBumpOffset.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionChannelMaskParameter.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionConstantBiasScale.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDDX.h"
#include "Materials/MaterialExpressionDDY.h"
#include "Materials/MaterialExpressionDecalDerivative.h"
#include "Materials/MaterialExpressionDecalLifetimeOpacity.h"
#include "Materials/MaterialExpressionDecalMipmapLevel.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionDepthOfFieldFunction.h"
#include "Materials/MaterialExpressionDeriveNormalZ.h"
#include "Materials/MaterialExpressionDesaturation.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDistanceCullFade.h"
#include "Materials/MaterialExpressionDistanceFieldsRenderingSwitch.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionCloudLayer.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionEyeAdaptation.h"
#include "Materials/MaterialExpressionFeatureLevelSwitch.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFmod.h"
#include "Materials/MaterialExpressionFontSample.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionGIReplace.h"
#include "Materials/MaterialExpressionRayTracingQualitySwitch.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionHairAttributes.h"
#include "Materials/MaterialExpressionHairColor.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionInverseLinearInterpolate.h"
#include "Materials/MaterialExpressionLightmapUVs.h"
#include "Materials/MaterialExpressionPrecomputedAOMask.h"
#include "Materials/MaterialExpressionLightmassReplace.h"
#include "Materials/MaterialExpressionLightVector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionLogarithm2.h"
#include "Materials/MaterialExpressionLogarithm10.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionMaterialProxyReplace.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionObjectBounds.h"
#include "Materials/MaterialExpressionObjectOrientation.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionObjectRadius.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionPreSkinnedLocalBounds.h"
#include "Materials/MaterialExpressionPreviousFrameSwitch.h"
#include "Materials/MaterialExpressionRerouteBase.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionSign.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionParticleDirection.h"
#include "Materials/MaterialExpressionParticleMacroUV.h"
#include "Materials/MaterialExpressionParticleMotionBlurFade.h"
#include "Materials/MaterialExpressionParticleRandom.h"
#include "Materials/MaterialExpressionParticlePositionWS.h"
#include "Materials/MaterialExpressionParticleRadius.h"
#include "Materials/MaterialExpressionParticleRelativeTime.h"
#include "Materials/MaterialExpressionParticleSize.h"
#include "Materials/MaterialExpressionParticleSpeed.h"
#include "Materials/MaterialExpressionPerInstanceFadeAmount.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSkinningVertexOffsets.h"
#include "Materials/MaterialExpressionPreSkinnedNormal.h"
#include "Materials/MaterialExpressionPreSkinnedPosition.h"
#include "Materials/MaterialExpressionQualitySwitch.h"
#include "Materials/MaterialExpressionShadingPathSwitch.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionRotateAboutAxis.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionRound.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureReplace.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionVirtualTextureFeatureSwitch.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionSceneDepthWithoutWater.h"
#include "Materials/MaterialExpressionSceneTexelSize.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionShadingModel.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionSobol.h"
#include "Materials/MaterialExpressionSpeedTree.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionSphericalParticleOpacity.h"
#include "Materials/MaterialExpressionSquareRoot.h"
#include "Materials/MaterialExpressionStaticBool.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTangent.h"
#include "Materials/MaterialExpressionTangentOutput.h"
#include "Materials/MaterialExpressionTemporalSobol.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureProperty.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionParticleSubUV.h"
#include "Materials/MaterialExpressionParticleSubUVProperties.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionAntialiasedTextureMask.h"
#include "Materials/MaterialExpressionTextureSampleParameterSubUV.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionTextureSampleParameterVolume.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionDeltaTime.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionTruncate.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorNoise.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionViewProperty.h"
#include "Materials/MaterialExpressionViewSize.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialInput.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionDistanceToNearestSurface.h"
#include "Materials/MaterialExpressionDistanceFieldGradient.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionAtmosphericLightVector.h"
#include "Materials/MaterialExpressionAtmosphericLightColor.h"
#include "Materials/MaterialExpressionSkyAtmosphereLightIlluminance.h"
#include "Materials/MaterialExpressionSkyAtmosphereLightDirection.h"
#include "Materials/MaterialExpressionSkyAtmosphereViewLuminance.h"
#include "Materials/MaterialExpressionMaterialLayerOutput.h"
#include "Materials/MaterialExpressionCurveAtlasRowParameter.h"
#include "Materials/MaterialExpressionMapARPassthroughCameraUV.h"
#include "Materials/MaterialExpressionShaderStageSwitch.h"
#include "Materials/MaterialExpressionReflectionCapturePassSwitch.h"
#include "Materials/MaterialUniformExpressions.h"
#include "Materials/MaterialExpressionReflectionCapturePassSwitch.h"
#include "Materials/MaterialExpressionSamplePhysicsField.h"
#include "EditorSupportDelegates.h"
#include "MaterialCompiler.h"
#if WITH_EDITOR
#include "MaterialGraph/MaterialGraphNode_Comment.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#endif //WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

#define LOCTEXT_NAMESPACE "MaterialExpression"

#define SWAP_REFERENCE_TO( ExpressionInput, ToBeRemovedExpression, ToReplaceWithExpression )	\
if( ExpressionInput.Expression == ToBeRemovedExpression )										\
{																								\
	ExpressionInput.Expression = ToReplaceWithExpression;										\
}

#if WITH_EDITOR
FUObjectAnnotationSparseBool GMaterialFunctionsThatNeedExpressionsFlipped;
FUObjectAnnotationSparseBool GMaterialFunctionsThatNeedCoordinateCheck;
FUObjectAnnotationSparseBool GMaterialFunctionsThatNeedCommentFix;
FUObjectAnnotationSparseBool GMaterialFunctionsThatNeedSamplerFixup;
#endif // #if WITH_EDITOR

/** Returns whether the given expression class is allowed. */
bool IsAllowedExpressionType(const UClass* const Class, const bool bMaterialFunction)
{
	static const auto AllowTextureArrayAssetCreationVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowTexture2DArrayCreation"));

	// Exclude comments from the expression list, as well as the base parameter expression, as it should not be used directly
	const bool bSharedAllowed = Class != UMaterialExpressionComment::StaticClass() 
		&& Class != UMaterialExpressionParameter::StaticClass()
		&& (Class != UMaterialExpressionTextureSampleParameter2DArray::StaticClass() || AllowTextureArrayAssetCreationVar->GetValueOnGameThread() != 0);

	if (bMaterialFunction)
	{
		return bSharedAllowed;
	}
	else
	{
		return bSharedAllowed
			&& Class != UMaterialExpressionFunctionInput::StaticClass()
			&& Class != UMaterialExpressionFunctionOutput::StaticClass();
	}
}

/** Parses a string into multiple lines, for use with tooltips. */
void ConvertToMultilineToolTip(const FString& InToolTip, const int32 TargetLineLength, TArray<FString>& OutToolTip)
{
	int32 CurrentPosition = 0;
	int32 LastPosition = 0;
	OutToolTip.Empty(1);

	while (CurrentPosition < InToolTip.Len())
	{
		// Move to the target position
		CurrentPosition += TargetLineLength;

		if (CurrentPosition < InToolTip.Len())
		{
			// Keep moving until we get to a space, or the end of the string
			while (CurrentPosition < InToolTip.Len() && InToolTip[CurrentPosition] != TCHAR(' '))
			{
				CurrentPosition++;
			}

			// Move past the space
			if (CurrentPosition < InToolTip.Len() && InToolTip[CurrentPosition] == TCHAR(' '))
			{
				CurrentPosition++;
			}

			// Add a new line, ending just after the space we just found
			OutToolTip.Add(InToolTip.Mid(LastPosition, CurrentPosition - LastPosition));
			LastPosition = CurrentPosition;
		}
		else
		{
			// Add a new line, right up to the end of the input string
			OutToolTip.Add(InToolTip.Mid(LastPosition, InToolTip.Len() - LastPosition));
		}
	}
}

void GetMaterialValueTypeDescriptions(const uint32 MaterialValueType, TArray<FText>& OutDescriptions)
{
	// Get exact float type if possible
	uint32 MaskedFloatType = MaterialValueType & MCT_Float;
	if (MaskedFloatType)
	{
		switch (MaskedFloatType)
		{
			case MCT_Float:
			case MCT_Float1:
				OutDescriptions.Add(LOCTEXT("Float", "Float"));
				break;
			case MCT_Float2:
				OutDescriptions.Add(LOCTEXT("Float2", "Float 2"));
				break;
			case MCT_Float3:
				OutDescriptions.Add(LOCTEXT("Float3", "Float 3"));
				break;
			case MCT_Float4:
				OutDescriptions.Add(LOCTEXT("Float4", "Float 4"));
				break;
			default:
				break;
		}
	}

	// Get exact texture type if possible
	uint32 MaskedTextureType = MaterialValueType & MCT_Texture;
	if (MaskedTextureType)
	{
		switch (MaskedTextureType)
		{
			case MCT_Texture2D:
				OutDescriptions.Add(LOCTEXT("Texture2D", "Texture 2D"));
				break;
			case MCT_TextureCube:
				OutDescriptions.Add(LOCTEXT("TextureCube", "Texture Cube"));
				break;
			case MCT_Texture2DArray:
				OutDescriptions.Add(LOCTEXT("Texture2DArray", "Texture 2D Array"));
				break;
			case MCT_VolumeTexture:
				OutDescriptions.Add(LOCTEXT("VolumeTexture", "Volume Texture"));
				break;
			case MCT_Texture:
				OutDescriptions.Add(LOCTEXT("Texture", "Texture"));
				break;
			default:
				break;
		}
	}

	if (MaterialValueType & MCT_StaticBool)
		OutDescriptions.Add(LOCTEXT("StaticBool", "Bool"));
	if (MaterialValueType & MCT_MaterialAttributes)
		OutDescriptions.Add(LOCTEXT("MaterialAttributes", "Material Attributes"));
	if (MaterialValueType & MCT_ShadingModel)
		OutDescriptions.Add(LOCTEXT("ShadingModel", "Shading Model"));
	if (MaterialValueType & MCT_Unknown)
		OutDescriptions.Add(LOCTEXT("Unknown", "Unknown"));
}

bool CanConnectMaterialValueTypes(const uint32 InputType, const uint32 OutputType)
{
	if (InputType & MCT_Unknown)
	{
		// can plug anything into unknown inputs
		return true;
	}
	if (OutputType & MCT_Unknown)
	{
		// TODO: Decide whether these should connect to everything
		// Usually means that inputs haven't been connected yet so makes workflow easier
		return true;
	}
	if (InputType & OutputType)
	{
		return true;
	}
	// Need to do more checks here to see whether types can be cast
	// just check if both are float for now
	if (InputType & MCT_Float && OutputType & MCT_Float)
	{
		return true;
	}
	return false;
}

#if WITH_EDITOR


void ValidateParameterNameInternal(class UMaterialExpression* ExpressionToValidate, class UMaterial* OwningMaterial, const bool bAllowDuplicateName)
{
	if (OwningMaterial != nullptr)
	{
		int32 NameIndex = 1;
		bool bFoundValidName = false;
		FName PotentialName;

		// Find an available unique name
		while (!bFoundValidName)
		{
			PotentialName = ExpressionToValidate->GetParameterName();

			// Parameters cannot be named Name_None, use the default name instead
			if (PotentialName == NAME_None)
			{
				PotentialName = UMaterialExpressionParameter::ParameterDefaultName;
			}

			if (!bAllowDuplicateName)
			{
				if (NameIndex != 1)
				{
					PotentialName.SetNumber(NameIndex);
				}

				bFoundValidName = true;

				for (const UMaterialExpression* Expression : OwningMaterial->Expressions)
				{
					if (Expression != nullptr && Expression->HasAParameterName())
					{
						// Validate that the new name doesn't violate the expression's rules (by default, same name as another of the same class)
						if (Expression != ExpressionToValidate && Expression->GetParameterName() == PotentialName && Expression->HasClassAndNameCollision(ExpressionToValidate))
						{
							bFoundValidName = false;
							break;
						}
					}
				}

				++NameIndex;
			}
			else
			{
				bFoundValidName = true;
			}
		}

		if (bAllowDuplicateName)
		{
			// Check for any matching values
			for (UMaterialExpression* Expression : OwningMaterial->Expressions)
			{
				if (Expression != nullptr && Expression->HasAParameterName())
				{
					// Name are unique per class type
					if (Expression != ExpressionToValidate && Expression->GetParameterName() == PotentialName && Expression->GetClass() == ExpressionToValidate->GetClass())
					{
						ExpressionToValidate->SetValueToMatchingExpression(Expression);
						break;
					}
				}
			}
		}

		ExpressionToValidate->SetParameterName(PotentialName);
	}
}

/**
 * Helper function that wraps the supplied texture coordinates in the necessary math to transform them for external textures
 *
 * @param Compiler                The compiler to add code to
 * @param TexCoordCodeIndex       Index to the code chunk that supplies the vanilla texture coordinates
 * @param TextureReferenceIndex   Index of the texture within the material (used to access the external texture transform at runtime)
 * @param ParameterName           (Optional) Parameter name of the texture parameter that's assigned to the sample (used to access the external texture transform at runtime)
 * @return Index to a new code chunk that supplies the transformed UV coordinates
 */
int32 CompileExternalTextureCoordinates(FMaterialCompiler* Compiler, const int32 TexCoordCodeIndex, const int32 TextureReferenceIndex, const TOptional<FName> ParameterName = TOptional<FName>())
{
	if (TexCoordCodeIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const int32 ScaleRotationCode = Compiler->ExternalTextureCoordinateScaleRotation(TextureReferenceIndex, ParameterName);
	const int32 OffsetCode = Compiler->ExternalTextureCoordinateOffset(TextureReferenceIndex, ParameterName);

	return Compiler->RotateScaleOffsetTexCoords(TexCoordCodeIndex, ScaleRotationCode, OffsetCode);
}

/**
 * Compile a texture sample taking into consideration external textures (which may use different sampling code in the shader on some platforms)
 *
 * @param Compiler                The compiler to add code to
 * @param Texture                 UTexture pointer used for the compiler
 * @param TexCoordCodeIndex       Index to the code chunk that supplies the vanilla texture coordinates
 * @param SamplerType             The type of sampler that is to be used
 * @param ParameterName           (Optional) Parameter name of the texture parameter that's assigned to the sample
 * @param MipValue0Index          (Optional) Mip value (0) code index when mips are being used
 * @param MipValue1Index          (Optional) Mip value (1) code index when mips are being used
 * @param MipValueMode            (Optional) Texture MIP value mode
 * @param SamplerSource           (Optional) Sampler source override
 * @return Index to a new code chunk that samples the texture
 */
int32 CompileTextureSample(
	FMaterialCompiler* Compiler,
	UTexture* Texture,
	int32 TexCoordCodeIndex,
	const EMaterialSamplerType SamplerType,
	const TOptional<FName> ParameterName = TOptional<FName>(),
	const int32 MipValue0Index=INDEX_NONE,
	const int32 MipValue1Index=INDEX_NONE,
	const ETextureMipValueMode MipValueMode=TMVM_None,
	const ESamplerSourceMode SamplerSource=SSM_FromTextureAsset,
	const bool AutomaticViewMipBias=false
	)
{
	int32 TextureReferenceIndex = INDEX_NONE;
	int32 TextureCodeIndex = INDEX_NONE;
	if (SamplerType == SAMPLERTYPE_External)
	{
		// External sampler, so generate the necessary external uniform expression based on whether we're using a parameter name or not
		TextureCodeIndex = ParameterName.IsSet() ? Compiler->ExternalTextureParameter(ParameterName.GetValue(), Texture, TextureReferenceIndex) : Compiler->ExternalTexture(Texture, TextureReferenceIndex);

		// External textures need an extra transform applied to the UV coordinates
		TexCoordCodeIndex = CompileExternalTextureCoordinates(Compiler, TexCoordCodeIndex, TextureReferenceIndex, ParameterName);
	}
	else
	{
		TextureCodeIndex = ParameterName.IsSet() ? Compiler->TextureParameter(ParameterName.GetValue(), Texture, TextureReferenceIndex, SamplerType, SamplerSource) : Compiler->Texture(Texture, TextureReferenceIndex, SamplerType, SamplerSource, MipValueMode);
	}

	return Compiler->TextureSample(
					TextureCodeIndex,
					TexCoordCodeIndex,
					SamplerType,
					MipValue0Index,
					MipValue1Index,
					MipValueMode,
					SamplerSource,
					TextureReferenceIndex,
					AutomaticViewMipBias);
}
#endif // WITH_EDITOR

/**
 * Compile a select "blend" between ShadingModels
 *
 * @param Compiler				The compiler to add code to
 * @param A						Select A if Alpha is less than 0.5f
 * @param B						Select B if Alpha is greater or equal to 0.5f
 * @param Alpha					Bland factor [0..1]
 * @return						Index to a new code chunk
 */
int32 CompileShadingModelBlendFunction(FMaterialCompiler* Compiler, const int32 A, const int32 B, const int32 Alpha)
{
	if (A == INDEX_NONE || B == INDEX_NONE || Alpha == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const int32 MidPoint = Compiler->Constant(0.5f);

	return Compiler->If(Alpha, MidPoint, B, INDEX_NONE, A, INDEX_NONE);
}

UMaterialExpression::UMaterialExpression(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, GraphNode(nullptr)
#endif // WITH_EDITORONLY_DATA
{
#if WITH_EDITORONLY_DATA
	Outputs.Add(FExpressionOutput(TEXT("")));

	bShowInputs = true;
	bShowOutputs = true;
	bCollapsed = true;
	bShowMaskColorsOnPin = true;
#endif // WITH_EDITORONLY_DATA
}

UObject* UMaterialExpression::GetAssetOwner() const
{
	return Function ? (UObject*)Function : (UObject*)Material;
}

FString UMaterialExpression::GetAssetPathName() const
{
	UObject* Asset = GetAssetOwner();
	return Asset ? Asset->GetPathName() : FString();
}

#if WITH_EDITOR
void UMaterialExpression::CopyMaterialExpressions(const TArray<UMaterialExpression*>& SrcExpressions, const TArray<UMaterialExpressionComment*>& SrcExpressionComments, 
	UMaterial* Material, UMaterialFunction* EditFunction, TArray<UMaterialExpression*>& OutNewExpressions, TArray<UMaterialExpression*>& OutNewComments)
{
	OutNewExpressions.Empty();
	OutNewComments.Empty();

	UObject* ExpressionOuter = Material;
	if (EditFunction)
	{
		ExpressionOuter = EditFunction;
	}

	TMap<UMaterialExpression*,UMaterialExpression*> SrcToDestMap;

	// Duplicate source expressions into the editor's material copy buffer.
	for( int32 SrcExpressionIndex = 0 ; SrcExpressionIndex < SrcExpressions.Num() ; ++SrcExpressionIndex )
	{
		UMaterialExpression*	SrcExpression		= SrcExpressions[SrcExpressionIndex];
		UMaterialExpressionMaterialFunctionCall* FunctionExpression = Cast<UMaterialExpressionMaterialFunctionCall>(SrcExpression);
		bool bIsValidFunctionExpression = true;

		if (EditFunction 
			&& FunctionExpression 
			&& FunctionExpression->MaterialFunction
			&& FunctionExpression->MaterialFunction->IsDependent(EditFunction))
		{
			bIsValidFunctionExpression = false;
		}

		if (bIsValidFunctionExpression && IsAllowedExpressionType(SrcExpression->GetClass(), EditFunction != nullptr))
		{
			UMaterialExpression* NewExpression = Cast<UMaterialExpression>(StaticDuplicateObject( SrcExpression, ExpressionOuter, NAME_None, RF_Transactional ));
			NewExpression->Material = Material;
			// Make sure we remove any references to functions the nodes came from
			NewExpression->Function = nullptr;

			SrcToDestMap.Add( SrcExpression, NewExpression );

			// Add to list of material expressions associated with the copy buffer.
			Material->Expressions.Add( NewExpression );

			// There can be only one default mesh paint texture.
			UMaterialExpressionTextureBase* TextureSample = Cast<UMaterialExpressionTextureBase>( NewExpression );
			if( TextureSample )
			{
				TextureSample->IsDefaultMeshpaintTexture = false;
			}

			NewExpression->UpdateParameterGuid(true, true);
			NewExpression->UpdateMaterialExpressionGuid(true, true);

			UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>( NewExpression );
			if( FunctionInput )
			{
				FunctionInput->ConditionallyGenerateId(true);
				FunctionInput->ValidateName();
			}

			UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>( NewExpression );
			if( FunctionOutput )
			{
				FunctionOutput->ConditionallyGenerateId(true);
				FunctionOutput->ValidateName();
			}

			// Record in output list.
			OutNewExpressions.Add( NewExpression );
		}
	}

	// Fix up internal references.  Iterate over the inputs of the new expressions, and for each input that refers
	// to an expression that was duplicated, point the reference to that new expression.  Otherwise, clear the input.
	for( int32 NewExpressionIndex = 0 ; NewExpressionIndex < OutNewExpressions.Num() ; ++NewExpressionIndex )
	{
		UMaterialExpression* NewExpression = OutNewExpressions[NewExpressionIndex];
		const TArray<FExpressionInput*>& ExpressionInputs = NewExpression->GetInputs();
		for ( int32 ExpressionInputIndex = 0 ; ExpressionInputIndex < ExpressionInputs.Num() ; ++ExpressionInputIndex )
		{
			FExpressionInput* Input = ExpressionInputs[ExpressionInputIndex];
			UMaterialExpression* InputExpression = Input->Expression;
			if ( InputExpression )
			{
				UMaterialExpression** NewInputExpression = SrcToDestMap.Find( InputExpression );
				if ( NewInputExpression )
				{
					check( *NewInputExpression );
					Input->Expression = *NewInputExpression;
				}
				else
				{
					Input->Expression = nullptr;
				}
			}
		}
	}

	// Copy Selected Comments
	for( int32 CommentIndex=0; CommentIndex<SrcExpressionComments.Num(); CommentIndex++)
	{
		UMaterialExpressionComment* ExpressionComment = SrcExpressionComments[CommentIndex];
		UMaterialExpressionComment* NewComment = Cast<UMaterialExpressionComment>(StaticDuplicateObject(ExpressionComment, ExpressionOuter));
		NewComment->Material = Material;

		// Add reference to the material
		Material->EditorComments.Add(NewComment);

		// Add to the output array.
		OutNewComments.Add(NewComment);
	}
}
#endif // WITH_EDITOR


void UMaterialExpression::Serialize(FStructuredArchive::FRecord Record)
{
	SCOPED_LOADTIMER(UMaterialExpression_Serialize);
	Super::Serialize(Record);

	Record.GetUnderlyingArchive().UsingCustomVersion(FRenderingObjectVersion::GUID);

#if WITH_EDITORONLY_DATA
	const TArray<FExpressionInput*> Inputs = GetInputs();
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		FExpressionInput* Input = Inputs[InputIndex];
		DoMaterialAttributeReorder(Input, Record.GetUnderlyingArchive().UE4Ver(), Record.GetUnderlyingArchive().CustomVer(FRenderingObjectVersion::GUID));
	}
#endif // WITH_EDITORONLY_DATA
}

void UMaterialExpression::PostInitProperties()
{
	Super::PostInitProperties();

	UpdateParameterGuid(false, false);
	
	UpdateMaterialExpressionGuid(false, false);
}

void UMaterialExpression::PostLoad()
{
	SCOPED_LOADTIMER(UMaterialExpression_PostLoad);
	Super::PostLoad();

	if (!Material && GetOuter()->IsA(UMaterial::StaticClass()))
	{
		Material = CastChecked<UMaterial>(GetOuter());
	}

	if (!Function && GetOuter()->IsA(UMaterialFunction::StaticClass()))
	{
		Function = CastChecked<UMaterialFunction>(GetOuter());
	}

	UpdateParameterGuid(false, false);
	
	UpdateMaterialExpressionGuid(false, false);
}

void UMaterialExpression::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// We do not force a guid regen here because this function is used when the Material Editor makes a copy of a material to edit.
	// If we forced a GUID regen, it would cause all of the guids for a material to change everytime a material was edited.
	UpdateParameterGuid(false, true);
	UpdateMaterialExpressionGuid(false, true);
}

#if WITH_EDITOR

void UMaterialExpression::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!GIsImportingT3D && !GIsTransacting && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive )
	{
		FPropertyChangedEvent SubPropertyChangedEvent(nullptr, PropertyChangedEvent.ChangeType);

		// Don't recompile the outer material if we are in the middle of a transaction or interactively changing properties
		// as there may be many expressions in the transaction buffer and we would just be recompiling over and over again.
		if (Material && !(Material->bIsPreviewMaterial || Material->bIsFunctionPreviewMaterial))
		{
			Material->PreEditChange(nullptr);
			Material->PostEditChangeProperty(SubPropertyChangedEvent);
		}
		else if (Function)
		{
			Function->PreEditChange(nullptr);
			Function->PostEditChangeProperty(SubPropertyChangedEvent);
		}
	}

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if( PropertyThatChanged != nullptr )
	{
		// Update the preview for this node if we adjusted a property
		bNeedToUpdatePreview = true;

		const FName PropertyName = PropertyThatChanged->GetFName();

		const FName ParameterName = TEXT("ParameterName");
		if (PropertyName == ParameterName)
		{
			ValidateParameterName();
		}

		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpression, Desc) && !IsA(UMaterialExpressionComment::StaticClass()))
		{
			if (GraphNode)
			{
				GraphNode->Modify();
				GraphNode->NodeComment = Desc;
			}
			// Don't need to update preview after changing description
			bNeedToUpdatePreview = false;
		}
	}
}

void UMaterialExpression::PostEditImport()
{
	Super::PostEditImport();

	UpdateParameterGuid(true, true);
}

bool UMaterialExpression::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	if (bIsEditable && InProperty != nullptr)
	{
		// Automatically set property as non-editable if it has OverridingInputProperty metadata
		// pointing to an FExpressionInput property which is hooked up as an input.
		//
		// e.g. in the below snippet, meta=(OverridingInputProperty = "A") indicates that ConstA will
		// be overridden by an FExpressionInput property named 'A' if one is connected, and will thereby
		// be set as non-editable.
		//
		//	UPROPERTY(meta = (RequiredInput = "false", ToolTip = "Defaults to 'ConstA' if not specified"))
		//	FExpressionInput A;
		//
		//	UPROPERTY(EditAnywhere, Category = MaterialExpressionAdd, meta = (OverridingInputProperty = "A"))
		//	float ConstA;
		//

		static FName OverridingInputPropertyMetaData(TEXT("OverridingInputProperty"));

		if (InProperty->HasMetaData(OverridingInputPropertyMetaData))
		{
			const FString& OverridingPropertyName = InProperty->GetMetaData(OverridingInputPropertyMetaData);

			FStructProperty* StructProp = FindFProperty<FStructProperty>(GetClass(), *OverridingPropertyName);
			if (ensure(StructProp != nullptr))
			{
				static FName RequiredInputMetaData(TEXT("RequiredInput"));

				// Must be a single FExpressionInput member, not an array, and must be tagged with metadata RequiredInput="false"
				if (ensure(	StructProp->Struct->GetFName() == NAME_ExpressionInput &&
							StructProp->ArrayDim == 1 &&
							StructProp->HasMetaData(RequiredInputMetaData) &&
							!StructProp->GetBoolMetaData(RequiredInputMetaData)))
				{
					const FExpressionInput* Input = StructProp->ContainerPtrToValuePtr<FExpressionInput>(this);

					if (Input->Expression != nullptr && Input->GetTracedInput().Expression != nullptr)
					{
						bIsEditable = false;
					}
				}
			}
		}
	}

	return bIsEditable;
}

TArray<FExpressionOutput>& UMaterialExpression::GetOutputs() 
{
	return Outputs;
}


const TArray<FExpressionInput*> UMaterialExpression::GetInputs()
{
	TArray<FExpressionInput*> Result;
	for( TFieldIterator<FStructProperty> InputIt(GetClass(), EFieldIteratorFlags::IncludeSuper,  EFieldIteratorFlags::ExcludeDeprecated) ; InputIt ; ++InputIt )
	{
		FStructProperty* StructProp = *InputIt;
		if( StructProp->Struct->GetFName() == NAME_ExpressionInput)
		{
			for (int32 ArrayIndex = 0; ArrayIndex < StructProp->ArrayDim; ArrayIndex++)
			{
				Result.Add(StructProp->ContainerPtrToValuePtr<FExpressionInput>(this, ArrayIndex));
			}
		}
	}
	return Result;
}


FExpressionInput* UMaterialExpression::GetInput(int32 InputIndex)
{
	int32 Index = 0;
	for( TFieldIterator<FStructProperty> InputIt(GetClass(), EFieldIteratorFlags::IncludeSuper,  EFieldIteratorFlags::ExcludeDeprecated) ; InputIt ; ++InputIt )
	{
		FStructProperty* StructProp = *InputIt;
		if( StructProp->Struct->GetFName() == NAME_ExpressionInput)
		{
			for (int32 ArrayIndex = 0; ArrayIndex < StructProp->ArrayDim; ArrayIndex++)
			{
			if( Index == InputIndex )
			{
					return StructProp->ContainerPtrToValuePtr<FExpressionInput>(this, ArrayIndex);
			}
			Index++;
		}
	}
	}

	return nullptr;
}


FName UMaterialExpression::GetInputName(int32 InputIndex) const
{
	int32 Index = 0;
	for( TFieldIterator<FStructProperty> InputIt(GetClass(),EFieldIteratorFlags::IncludeSuper,  EFieldIteratorFlags::ExcludeDeprecated) ; InputIt ; ++InputIt )
	{
		FStructProperty* StructProp = *InputIt;
		if( StructProp->Struct->GetFName() == NAME_ExpressionInput)
		{
			for (int32 ArrayIndex = 0; ArrayIndex < StructProp->ArrayDim; ArrayIndex++)
			{
			if( Index == InputIndex )
			{
					FExpressionInput const* Input = StructProp->ContainerPtrToValuePtr<FExpressionInput>(this, ArrayIndex);

						if (!Input->InputName.IsNone())
						{
							return Input->InputName;
						}
						else
						{
							FName StructName = StructProp->GetFName();

					if (StructProp->ArrayDim > 1)
					{
								StructName = *FString::Printf(TEXT("%s_%d"), *StructName.ToString(), ArrayIndex);
					}

							return StructName;
						}
			}
			Index++;
		}
	}
	}
	return NAME_None;
}

FText UMaterialExpression::GetCreationDescription() const
{
	return FText::GetEmpty();
}

FText UMaterialExpression::GetCreationName() const
{
	return FText::GetEmpty();
}

void UMaterialExpression::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	if (Desc.Len() > 0)
	{
		if (GraphNode)
		{
			GraphNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString().ParseIntoArrayLines(OutToolTip, false);
		}

		TArray<FString> Multiline = TArray<FString>();
		Desc.ParseIntoArrayLines(Multiline, false);

		TArray<FString> CurrentLines = TArray<FString>();
		for (FString Line : Multiline)
		{
			if (Line.IsEmpty())
			{
				OutToolTip.Add(Line);
			}
			else
			{
				ConvertToMultilineToolTip(Line, 40, CurrentLines);
				OutToolTip.Append(CurrentLines);
			}
		}
	}
}

bool UMaterialExpression::IsInputConnectionRequired(int32 InputIndex) const
{
	int32 Index = 0;
	for( TFieldIterator<FStructProperty> InputIt(GetClass(), EFieldIteratorFlags::IncludeSuper,  EFieldIteratorFlags::ExcludeDeprecated) ; InputIt ; ++InputIt )
	{
		FStructProperty* StructProp = *InputIt;
		if( StructProp->Struct->GetFName() == NAME_ExpressionInput)
		{
			for (int32 ArrayIndex = 0; ArrayIndex < StructProp->ArrayDim; ArrayIndex++)
			{
				if( Index == InputIndex )
				{
					FExpressionInput const* Input = StructProp->ContainerPtrToValuePtr<FExpressionInput>(this, ArrayIndex);
					const TCHAR* MetaKey = TEXT("RequiredInput");

					if( StructProp->HasMetaData(MetaKey) )
					{
						return StructProp->GetBoolMetaData(MetaKey);
					}
				}
				Index++;
			}
		}
	}
	return true;
}

uint32 UMaterialExpression::GetInputType(int32 InputIndex)
{
	// different inputs should be defined by sub classed expressions
	return MCT_Float;
}

uint32 UMaterialExpression::GetOutputType(int32 OutputIndex)
{
	// different outputs should be defined by sub classed expressions
	if (IsResultMaterialAttributes(OutputIndex))
	{
		return MCT_MaterialAttributes;
	}
	else
	{
		FExpressionOutput& Output = GetOutputs()[OutputIndex];
		if (Output.Mask)
		{
			int32 MaskChannelCount = (Output.MaskR ? 1 : 0)
									+ (Output.MaskG ? 1 : 0)
									+ (Output.MaskB ? 1 : 0)
									+ (Output.MaskA ? 1 : 0);
			switch (MaskChannelCount)
			{
			case 1:
				return MCT_Float;
			case 2:
				return MCT_Float2;
			case 3:
				return MCT_Float3;
			case 4:
				return MCT_Float4;
			default:
				return MCT_Unknown;
			}
		}
		else
		{
			return MCT_Float;
		}
	}
}

int32 UMaterialExpression::GetWidth() const
{
	return ME_STD_THUMBNAIL_SZ+(ME_STD_BORDER*2);
}



int32 UMaterialExpression::GetHeight() const
{
	return FMath::Max(ME_CAPTION_HEIGHT + (Outputs.Num() * ME_STD_TAB_HEIGHT),ME_CAPTION_HEIGHT+ME_STD_THUMBNAIL_SZ+(ME_STD_BORDER*2));
}


bool UMaterialExpression::UsesLeftGutter() const
{
	return 0;
}



bool UMaterialExpression::UsesRightGutter() const
{
	return 0;
}

void UMaterialExpression::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Expression"));
}

FString UMaterialExpression::GetDescription() const
{
	// Combined captions sufficient for most expressions
	TArray<FString> Captions;
	GetCaption(Captions);

	if (Captions.Num() > 1)
	{
		FString Result = Captions[0];
		for (int32 Index = 1; Index < Captions.Num(); ++Index)
		{
			Result += TEXT(" ");
			Result += Captions[Index];
		}

		return Result;
	}
	else
	{
		return Captions[0];
	}
}

void UMaterialExpression::GetConnectorToolTip(int32 InputIndex, int32 OutputIndex, TArray<FString>& OutToolTip)
{
	if (InputIndex != INDEX_NONE)
	{
		for( TFieldIterator<FStructProperty> InputIt(GetClass()) ; InputIt ; ++InputIt )
		{
			FStructProperty* StructProp = *InputIt;
			if( StructProp->Struct->GetFName() == NAME_ExpressionInput )
			{
				for (int32 ArrayIndex = 0; ArrayIndex < StructProp->ArrayDim; ArrayIndex++)
				{
					if (!InputIndex)
					{
						if (StructProp->HasMetaData(TEXT("tooltip")))
						{
							// Set the tooltip from the .h comments
							ConvertToMultilineToolTip(StructProp->GetToolTipText().ToString(), 40, OutToolTip);
						}
						return;
					}
					InputIndex--;
				}
			}
		}
	}
}

int32 UMaterialExpression::CompilerError(FMaterialCompiler* Compiler, const TCHAR* pcMessage)
{
	TArray<FString> Captions;
	GetCaption(Captions);
	return Compiler->Errorf(TEXT("%s> %s"), Desc.Len() > 0 ? *Desc : *Captions[0], pcMessage);
}

bool UMaterialExpression::Modify( bool bAlwaysMarkDirty/*=true*/ )
{
	bNeedToUpdatePreview = true;
	
	return Super::Modify(bAlwaysMarkDirty);
}

bool UMaterialExpression::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if (FCString::Stristr(SearchQuery, TEXT("NAME=")) != nullptr)
	{
		FString SearchString(SearchQuery);
		SearchString.RightInline(SearchString.Len() - 5, false);
		return (GetName().Contains(SearchString) );
	}
	return Desc.Contains(SearchQuery);
}

void UMaterialExpression::ConnectExpression( FExpressionInput* Input, int32 OutputIndex )
{
	if( Input && OutputIndex >= 0 && OutputIndex < Outputs.Num() )
	{
		FExpressionOutput& Output = Outputs[OutputIndex];
		Input->Expression = this;
		Input->OutputIndex = OutputIndex;
		Input->Mask = Output.Mask;
		Input->MaskR = Output.MaskR;
		Input->MaskG = Output.MaskG;
		Input->MaskB = Output.MaskB;
		Input->MaskA = Output.MaskA;
	}
}
#endif // WITH_EDITOR

void UMaterialExpression::UpdateMaterialExpressionGuid(bool bForceGeneration, bool bAllowMarkingPackageDirty)
{
	// If we are in the editor, and we don't have a valid GUID yet, generate one.
	if (GIsEditor && !FApp::IsGame())
	{
		FGuid& Guid = GetMaterialExpressionId();

		if (bForceGeneration || !Guid.IsValid())
		{
			Guid = FGuid::NewGuid();

			if (bAllowMarkingPackageDirty)
			{
				MarkPackageDirty();
			}
		}
	}
}


void UMaterialExpression::UpdateParameterGuid(bool bForceGeneration, bool bAllowMarkingPackageDirty)
{
	if (bIsParameterExpression)
	{
		// If we are in the editor, and we don't have a valid GUID yet, generate one.
		if(GIsEditor && !FApp::IsGame())
		{
			FGuid& Guid = GetParameterExpressionId();

			if (bForceGeneration || !Guid.IsValid())
			{
				Guid = FGuid::NewGuid();

				if (bAllowMarkingPackageDirty)
				{
					MarkPackageDirty();
				}
			}
		}
	}
}

#if WITH_EDITOR

void UMaterialExpression::ConnectToPreviewMaterial(UMaterial* InMaterial, int32 OutputIndex)
{
	if (InMaterial && OutputIndex >= 0 && OutputIndex < Outputs.Num())
	{
		bool bUseMaterialAttributes = IsResultMaterialAttributes(0);

		if( bUseMaterialAttributes )
		{
			InMaterial->SetShadingModel(MSM_DefaultLit);
			InMaterial->bUseMaterialAttributes = true;
			FExpressionInput* MaterialInput = InMaterial->GetExpressionInputForProperty(MP_MaterialAttributes);
			check(MaterialInput);
			ConnectExpression( MaterialInput, OutputIndex );
		}
		else
		{
			InMaterial->SetShadingModel(MSM_Unlit);
			InMaterial->bUseMaterialAttributes = false;

			// Connect the selected expression to the emissive node of the expression preview material.  The emissive material is not affected by light which is why its a good choice.
			FExpressionInput* MaterialInput = InMaterial->GetExpressionInputForProperty(MP_EmissiveColor);
			check(MaterialInput);
			ConnectExpression( MaterialInput, OutputIndex );
		}
	}
}
#endif // WITH_EDITOR

void UMaterialExpression::ValidateState()
{
	// Disabled for now until issues can be tracked down
	//check(!IsPendingKill());
}

#if WITH_EDITOR
bool UMaterialExpression::GetAllInputExpressions(TArray<UMaterialExpression*>& InputExpressions)
{
	// Make sure we don't end up in a loop
	if (!InputExpressions.Contains(this))
	{
		bool bFoundRepeat = false;
		InputExpressions.Add(this);

		const TArray<FExpressionInput*> Inputs = GetInputs();

		for (int32 Index = 0; Index < Inputs.Num(); Index++)
		{
			if (Inputs[Index]->Expression)
			{
				if (Inputs[Index]->Expression->GetAllInputExpressions(InputExpressions))
				{
					bFoundRepeat = true;
				}
			}
		}

		return bFoundRepeat;
	}
	else
	{
		return true;
	}
}

bool UMaterialExpression::CanRenameNode() const
{
	return false;
}

FString UMaterialExpression::GetEditableName() const
{
	// This function is only safe to call in a class that has implemented CanRenameNode() to return true
	check(false);
	return TEXT("");
}

void UMaterialExpression::SetEditableName(const FString& NewName)
{
	// This function is only safe to call in a class that has implemented CanRenameNode() to return true
	check(false);
}

void UMaterialExpression::ValidateParameterName(const bool bAllowDuplicateName)
{
	// Incrementing the name is now handled in UMaterialExpressionParameter::ValidateParameterName
}

bool UMaterialExpression::HasClassAndNameCollision(UMaterialExpression* OtherExpression) const
{
	return GetClass() == OtherExpression->GetClass();
}


bool UMaterialExpression::HasConnectedOutputs() const
{
	bool bIsConnected = !GraphNode;
	if (GraphNode)
	{
		UMaterialGraphNode* MatGraphNode = Cast<UMaterialGraphNode>(GraphNode);
		if (MatGraphNode)
		{
			TArray<UEdGraphPin*> OutputPins;
			MatGraphNode->GetOutputPins(OutputPins);
			for (UEdGraphPin* Pin : OutputPins)
			{
				if (Pin->LinkedTo.Num() > 0)
				{
					bIsConnected = true;
				}
			}
		}
	}
	return bIsConnected;
}

bool UMaterialExpression::ContainsInputLoop(const bool bStopOnFunctionCall /*= true*/)
{
	TArray<FMaterialExpressionKey> ExpressionStack;
	TSet<FMaterialExpressionKey> VisitedExpressions;
	return ContainsInputLoopInternal(ExpressionStack, VisitedExpressions, bStopOnFunctionCall);
}

bool UMaterialExpression::ContainsInputLoopInternal(TArray<FMaterialExpressionKey>& ExpressionStack, TSet<FMaterialExpressionKey>& VisitedExpressions, const bool bStopOnFunctionCall)
{
	const TArray<FExpressionInput*> Inputs = GetInputs();
	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		FExpressionInput* Input = Inputs[Index];
		if (Input->Expression)
		{
			// ContainsInputLoop primarily used to detect safe traversal path for IsResultMaterialAttributes.
			// In those cases we can bail on a function as the inputs are strongly typed
			UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Input->Expression);
			UMaterialExpressionMaterialAttributeLayers* Layers = Cast<UMaterialExpressionMaterialAttributeLayers>(Input->Expression);
			if (bStopOnFunctionCall && (FunctionCall || Layers))
			{
				continue;
			}

			FMaterialExpressionKey InputExpressionKey(Input->Expression, Input->OutputIndex);
			if (ExpressionStack.Contains(InputExpressionKey))
			{
				return true;
			}
			// prevent recurring visits to expressions we've already checked
			else if (!VisitedExpressions.Contains(InputExpressionKey))
			{
				VisitedExpressions.Add(InputExpressionKey);
				ExpressionStack.Add(InputExpressionKey);
				if (Input->Expression->ContainsInputLoopInternal(ExpressionStack, VisitedExpressions, bStopOnFunctionCall))
				{
					return true;
				}
				ExpressionStack.Pop();
			}
		}
	}

	return false;
}
#endif // WITH_EDITOR

UMaterialExpressionTextureBase::UMaterialExpressionTextureBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, IsDefaultMeshpaintTexture(false)
#endif
{}

#if WITH_EDITOR
void UMaterialExpressionTextureBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty( PropertyChangedEvent );

	if (IsDefaultMeshpaintTexture && PropertyChangedEvent.Property)
	{
		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == FName(TEXT("IsDefaultMeshpaintTexture")))
		{
			const TArray<UMaterialExpression*>& Expressions = this->Material->GetMaterial()->Expressions;

			// Check for other defaulted textures in THIS material (does not search sub levels ie functions etc, as these are ignored in the texture painter). 
			for (auto ItExpressions = Expressions.CreateConstIterator(); ItExpressions; ItExpressions++)
			{
				UMaterialExpressionTextureBase* TextureSample = Cast<UMaterialExpressionTextureBase>(*ItExpressions);
				if (TextureSample != nullptr && TextureSample != this)
				{
					if(TextureSample->IsDefaultMeshpaintTexture)
					{
						FText ErrorMessage = LOCTEXT("MeshPaintDefaultTextureErrorDefault","Only one texture can be set as the Mesh Paint Default Texture, disabling previous default");
						if (TextureSample->Texture != nullptr)
						{
							FFormatNamedArguments Args;
							Args.Add( TEXT("TextureName"), FText::FromString( TextureSample->Texture->GetName() ) );
							ErrorMessage = FText::Format(LOCTEXT("MeshPaintDefaultTextureErrorTextureKnown","Only one texture can be set as the Mesh Paint Default Texture, disabling {TextureName}"), Args );
						}
										
						// Launch notification to inform user of default change
						FNotificationInfo Info( ErrorMessage );
						Info.ExpireDuration = 5.0f;
						Info.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));

						FSlateNotificationManager::Get().AddNotification(Info);

						// Reset the previous default to false;
						TextureSample->IsDefaultMeshpaintTexture = false;
					}
				}
			}
		}
	}
}

FString UMaterialExpressionTextureBase::GetDescription() const
{
	FString Result = Super::GetDescription();
	Result += TEXT(" (");
	Result += Texture ? Texture->GetName() : TEXT("None");
	Result += TEXT(")");

	return Result;
}

FText UMaterialExpressionTextureBase::GetPreviewOverlayText() const
{
	if (IsVirtualSamplerType(SamplerType))
	{
		return LOCTEXT("VT", "VT");
	}
	else
	{
		return FText();
	}
}

void UMaterialExpressionTextureBase::AutoSetSampleType()
{
	if ( Texture )
	{
		SamplerType = GetSamplerTypeForTexture( Texture );
	}
}

EMaterialSamplerType UMaterialExpressionTextureBase::GetSamplerTypeForTexture(const UTexture* Texture, bool ForceNoVT)
{
	if (Texture)
	{
		if (Texture->GetMaterialType() == MCT_TextureExternal)
		{
			return SAMPLERTYPE_External;
		}
		else if (Texture->LODGroup == TEXTUREGROUP_8BitData || Texture->LODGroup == TEXTUREGROUP_16BitData)
		{
			return SAMPLERTYPE_Data;
		}
			
		const bool bVirtual = ForceNoVT ? false : Texture->GetMaterialType() == MCT_TextureVirtual;

		switch (Texture->CompressionSettings)
		{
			case TC_Normalmap:
				return bVirtual ? SAMPLERTYPE_VirtualNormal : SAMPLERTYPE_Normal;
			case TC_Grayscale:
				return Texture->SRGB	? (bVirtual ?  SAMPLERTYPE_VirtualGrayscale : SAMPLERTYPE_Grayscale)
										: (bVirtual ? SAMPLERTYPE_VirtualLinearGrayscale : SAMPLERTYPE_LinearGrayscale);
			case TC_Alpha:
				return bVirtual ?  SAMPLERTYPE_VirtualAlpha : SAMPLERTYPE_Alpha;
			case TC_Masks:
				return bVirtual ?  SAMPLERTYPE_VirtualMasks : SAMPLERTYPE_Masks;
			case TC_DistanceFieldFont:
				return SAMPLERTYPE_DistanceFieldFont;
			default:
				return Texture->SRGB	? (bVirtual ? SAMPLERTYPE_VirtualColor : SAMPLERTYPE_Color) 
										: (bVirtual ? SAMPLERTYPE_VirtualLinearColor : SAMPLERTYPE_LinearColor);
		}
	}
	return SAMPLERTYPE_Color;
}
#endif // WITH_EDITOR

UObject* UMaterialExpressionTextureBase::GetReferencedTexture() const
{
	return Texture;
}

UMaterialExpressionTextureSample::UMaterialExpressionTextureSample(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Texture;
		FConstructorStatics()
			: NAME_Texture(LOCTEXT( "Texture", "Texture" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Texture);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("RGB"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("R"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("G"), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("B"), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("A"), 1, 0, 0, 0, 1));
	Outputs.Add(FExpressionOutput(TEXT("RGBA"), 1, 1, 1, 1, 1));

	bShowOutputNameOnPin = true;
	bShowTextureInputPin = true;
	bCollapsed = false;

	MipValueMode = TMVM_None;
	ConstCoordinate = 0;
	ConstMipValue = INDEX_NONE;
	AutomaticViewMipBias = true;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
bool UMaterialExpressionTextureSample::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	if (bIsEditable && InProperty != nullptr)
	{
		FName PropertyFName = InProperty->GetFName();

		if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, ConstMipValue))
		{
			bIsEditable = MipValueMode == TMVM_MipLevel || MipValueMode == TMVM_MipBias;
		}
		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, ConstCoordinate))
		{
			bIsEditable = !Coordinates.GetTracedInput().Expression;
		}
		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, Texture))
		{
			// The Texture property is overridden by a connection to TextureObject
			bIsEditable = TextureObject.GetTracedInput().Expression == nullptr;
		}
		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, AutomaticViewMipBias))
		{
			bIsEditable = AutomaticViewMipBiasValue.GetTracedInput().Expression == nullptr;
		}
	}

	return bIsEditable;
}

void UMaterialExpressionTextureSample::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if ( PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetName() == TEXT("Texture") )
	{
		if ( Texture )
		{
			AutoSetSampleType();
			FEditorSupportDelegates::ForcePropertyWindowRebuild.Broadcast(this);
		}
	}

	if (PropertyChangedEvent.MemberProperty)
	{
		const FName PropertyName = PropertyChangedEvent.MemberProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionTextureSample, MipValueMode))
		{
			if (GraphNode)
			{
				GraphNode->ReconstructNode();
			}
		}
	}
	
	// Need to update expression properties before super call (which triggers recompile)
	Super::PostEditChangeProperty( PropertyChangedEvent );	
}

void UMaterialExpressionTextureSample::PostLoad()
{
	Super::PostLoad();

	// Clear invalid input reference
	if (!bShowTextureInputPin && TextureObject.Expression)
	{
		TextureObject.Expression = nullptr;
	}
}

const TArray<FExpressionInput*> UMaterialExpressionTextureSample::GetInputs()
{
	TArray<FExpressionInput*> OutInputs;

	// todo: we should remove GetInputs() and make this the common code for all expressions
	uint32 InputIndex = 0;
	while(FExpressionInput* Ptr = GetInput(InputIndex++))
	{
		OutInputs.Add(Ptr);
	}

	return OutInputs;
}

// this define is only used for the following function
#define IF_INPUT_RETURN(Item) if(!InputIndex) return &Item; --InputIndex
FExpressionInput* UMaterialExpressionTextureSample::GetInput(int32 InputIndex)
{
	IF_INPUT_RETURN(Coordinates);

	if (bShowTextureInputPin)
	{
		IF_INPUT_RETURN(TextureObject);
	}

	if(MipValueMode == TMVM_Derivative)
	{
		IF_INPUT_RETURN(CoordinatesDX);
		IF_INPUT_RETURN(CoordinatesDY);
	}
	else if(MipValueMode != TMVM_None)
	{
		IF_INPUT_RETURN(MipValue);
	}

	IF_INPUT_RETURN(AutomaticViewMipBiasValue);

	return nullptr;
}
#undef IF_INPUT_RETURN

// this define is only used for the following function
#define IF_INPUT_RETURN(Name) if(!InputIndex) return Name; --InputIndex
FName UMaterialExpressionTextureSample::GetInputName(int32 InputIndex) const
{
	// Coordinates
	IF_INPUT_RETURN(TEXT("Coordinates"));

	if (bShowTextureInputPin)
	{
		// TextureObject
		IF_INPUT_RETURN(TEXT("TextureObject"));
	}

	if(MipValueMode == TMVM_MipLevel)
	{
		// MipValue
		IF_INPUT_RETURN(TEXT("MipLevel"));
	}
	else if(MipValueMode == TMVM_MipBias)
	{
		// MipValue
		IF_INPUT_RETURN(TEXT("MipBias"));
	}
	else if(MipValueMode == TMVM_Derivative)
	{
		// CoordinatesDX
		IF_INPUT_RETURN(TEXT("DDX(UVs)"));
		// CoordinatesDY
		IF_INPUT_RETURN(TEXT("DDY(UVs)"));
	}

	// AutomaticViewMipBiasValue
	IF_INPUT_RETURN(TEXT("Apply View MipBias"));

	return TEXT("");
}
#undef IF_INPUT_RETURN

/**
 * Verify that the texture and sampler type. Generates a compiler waring if 
 * they do not.
 * @param Compiler - The material compiler to which errors will be reported.
 * @param ExpressionDesc - Description of the expression verifying the sampler type.
 * @param Texture - The texture to verify. A nullptr texture is considered valid!
 * @param SamplerType - The sampler type to verify.
 */
static bool VerifySamplerType(
	FMaterialCompiler* Compiler,
	const TCHAR* ExpressionDesc,
	const UTexture* Texture,
	EMaterialSamplerType SamplerType )
{
	check( Compiler );
	check( ExpressionDesc );

	if ( Texture )
	{
		EMaterialSamplerType CorrectSamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture( Texture );
		bool bIsVirtualTextured = IsVirtualSamplerType(SamplerType);
		if (bIsVirtualTextured && UseVirtualTexturing(Compiler->GetFeatureLevel(), Compiler->GetTargetPlatform()) == false)
		{
			SamplerType = UMaterialExpressionTextureBase::GetSamplerTypeForTexture(Texture, !bIsVirtualTextured);
		}
		if ( SamplerType != CorrectSamplerType )
		{
			UEnum* SamplerTypeEnum = UMaterialInterface::GetSamplerTypeEnum();
			check( SamplerTypeEnum );

			FString SamplerTypeDisplayName = SamplerTypeEnum->GetDisplayNameTextByValue(SamplerType).ToString();
			FString TextureTypeDisplayName = SamplerTypeEnum->GetDisplayNameTextByValue(CorrectSamplerType).ToString();

			Compiler->Errorf( TEXT("%s> Sampler type is %s, should be %s for %s"),
				ExpressionDesc,
				*SamplerTypeDisplayName,
				*TextureTypeDisplayName,
				*Texture->GetPathName() );
			return false;
		}
		if((SamplerType == SAMPLERTYPE_Normal || SamplerType == SAMPLERTYPE_Masks) && Texture->SRGB)
		{
			UEnum* SamplerTypeEnum = UMaterialInterface::GetSamplerTypeEnum();
			check( SamplerTypeEnum );

			FString SamplerTypeDisplayName = SamplerTypeEnum->GetDisplayNameTextByValue(SamplerType).ToString();

			Compiler->Errorf( TEXT("%s> To use '%s' as sampler type, SRGB must be disabled for %s"),
				ExpressionDesc,
				*SamplerTypeDisplayName,
				*Texture->GetPathName() );
			return false;
		}
	}
	return true;
}

int32 UMaterialExpressionTextureSample::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	UMaterialExpression* InputExpression = TextureObject.GetTracedInput().Expression;

	if (Texture || InputExpression) // We deal with reroute textures later on in this function..
	{
		int32 TextureReferenceIndex = INDEX_NONE;
		int32 TextureCodeIndex = INDEX_NONE;

		bool bDoAutomaticViewMipBias = AutomaticViewMipBias;
		if (AutomaticViewMipBiasValue.GetTracedInput().Expression)
		{
			bool bSucceeded;
			bool bValue = Compiler->GetStaticBoolValue(AutomaticViewMipBiasValue.Compile(Compiler), bSucceeded);

			if (bSucceeded)
			{
				bDoAutomaticViewMipBias = bValue;
			}
		}

		if (InputExpression)
		{
			TextureCodeIndex = TextureObject.Compile(Compiler);
		}
		else if (SamplerType == SAMPLERTYPE_External)
		{
			TextureCodeIndex = Compiler->ExternalTexture(Texture, TextureReferenceIndex);
		}
		else
		{
			TextureCodeIndex = Compiler->Texture(Texture, TextureReferenceIndex, SamplerType, SamplerSource, MipValueMode);
		}

		if (TextureCodeIndex == INDEX_NONE)
		{
			// Can't continue without a texture to sample
			return INDEX_NONE;
		}

		UTexture* EffectiveTexture = Texture;
		EMaterialSamplerType EffectiveSamplerType = SamplerType;
		TOptional<FName> EffectiveParameterName;
		if (InputExpression)
		{
			if (!Compiler->GetTextureForExpression(TextureCodeIndex, TextureReferenceIndex, EffectiveSamplerType, EffectiveParameterName))
			{
				return CompilerError(Compiler, TEXT("Tex input requires a texture value"));
			}
			if (TextureReferenceIndex != INDEX_NONE)
			{
				EffectiveTexture = Cast<UTexture>(Compiler->GetReferencedTexture(TextureReferenceIndex));
			}
		}

		if (EffectiveTexture && VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("TextureSample")), EffectiveTexture, EffectiveSamplerType))
		{
			if (TextureCodeIndex != INDEX_NONE)
			{
				const EMaterialValueType TextureType = Compiler->GetParameterType(TextureCodeIndex);
				if (TextureType == MCT_TextureCube && !Coordinates.GetTracedInput().Expression)
				{
					return CompilerError(Compiler, TEXT("UVW input required for cubemap sample"));
				}
				else if (TextureType == MCT_VolumeTexture && !Coordinates.GetTracedInput().Expression)
				{
					return CompilerError(Compiler, TEXT("UVW input required for volume sample"));
				}
				else if (TextureType == MCT_Texture2DArray && !Coordinates.GetTracedInput().Expression)
				{
					return CompilerError(Compiler, TEXT("UVW input required for texturearray sample"));
				}
			}

			int32 CoordinateIndex = Coordinates.GetTracedInput().Expression ? Coordinates.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false);

			// If the sampler type is an external texture, we have might have a scale/bias to apply to the UV coordinates.
			// Generate that code for the TextureReferenceIndex here so we compile it using the correct texture based on possible reroute textures above
			if (EffectiveSamplerType == SAMPLERTYPE_External)
			{
				CoordinateIndex = CompileExternalTextureCoordinates(Compiler, CoordinateIndex, TextureReferenceIndex, EffectiveParameterName);
			}

			return Compiler->TextureSample(
				TextureCodeIndex,
				CoordinateIndex,
				EffectiveSamplerType,
				CompileMipValue0(Compiler),
				CompileMipValue1(Compiler),
				MipValueMode,
				SamplerSource,
				TextureReferenceIndex,
				bDoAutomaticViewMipBias);
		}
		else
		{
			// TextureObject.Expression is responsible for generating the error message, since it had a nullptr texture value
			return INDEX_NONE;
		}
	}
	else
	{
		return CompilerError(Compiler, TEXT("Missing input texture"));
	}
}

int32 UMaterialExpressionTextureSample::GetWidth() const
{
	return ME_STD_THUMBNAIL_SZ+(ME_STD_BORDER*2);
}

#if WITH_EDITOR
#define IF_INPUT_RETURN(Value) if(!InputIndex) return (Value); --InputIndex
int32 GetAbsoluteIndex(int32 InputIndex, const bool bShowTextureInputPin, const TEnumAsByte<enum ETextureMipValueMode>& MipValueMode)
{
	// Coordinates
	IF_INPUT_RETURN(0);
	if (bShowTextureInputPin)
	{
		// TextureObject
		IF_INPUT_RETURN(1);
	}
	if(MipValueMode == TMVM_Derivative)
	{
		// CoordinatesDX
		IF_INPUT_RETURN(3);
		// CoordinatesDY
		IF_INPUT_RETURN(4);
	}
	else if(MipValueMode != TMVM_None)
	{
		// MipValue
		IF_INPUT_RETURN(2);
	}
	// AutomaticViewMipBiasValue
	IF_INPUT_RETURN(5);
	// If not found
	return INDEX_NONE;
}
#undef IF_INPUT_RETURN

void UMaterialExpressionTextureSample::GetConnectorToolTip(int32 InputIndex, int32 OutputIndex, TArray<FString>& OutToolTip)
{
	const int32 AbsoluteIndex = GetAbsoluteIndex(InputIndex, bShowTextureInputPin, MipValueMode);
	Super::GetConnectorToolTip(AbsoluteIndex, OutputIndex, OutToolTip);
}
#endif // WITH_EDITOR

void UMaterialExpressionTextureSample::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Texture Sample"));
}

bool UMaterialExpressionTextureSample::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if( Texture!=nullptr && Texture->GetName().Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

// this define is only used for the following function
#define IF_INPUT_RETURN(Type) if(!InputIndex) return (Type); --InputIndex
uint32 UMaterialExpressionTextureSample::GetInputType(int32 InputIndex)
{
	// Coordinates
	IF_INPUT_RETURN(MCT_Float);

	if (bShowTextureInputPin)
	{
		// TextureObject
		// TODO: Only show the TextureObject input inside a material function, since that's the only place it is useful
		IF_INPUT_RETURN(MCT_Texture);
	}
	
	if(MipValueMode == TMVM_MipLevel || MipValueMode == TMVM_MipBias)
	{
		// MipValue
		IF_INPUT_RETURN(MCT_Float);
	}
	else if(MipValueMode == TMVM_Derivative)
	{
		// CoordinatesDX
		IF_INPUT_RETURN(MCT_Float);
		// CoordinatesDY
		IF_INPUT_RETURN(MCT_Float);
	}

	// AutomaticViewMipBiasValue
	IF_INPUT_RETURN(MCT_StaticBool);

	return MCT_Unknown;
}
#undef IF_INPUT_RETURN

int32 UMaterialExpressionTextureSample::CompileMipValue0(class FMaterialCompiler* Compiler)
{
	if (MipValueMode == TMVM_Derivative)
	{
		if (CoordinatesDX.GetTracedInput().IsConnected())
		{
			return CoordinatesDX.Compile(Compiler);
		}
	}
	else if (MipValue.GetTracedInput().IsConnected())
	{
		return MipValue.Compile(Compiler);
	}
	else
	{
		return Compiler->Constant(ConstMipValue);
	}

	return INDEX_NONE;
}

int32 UMaterialExpressionTextureSample::CompileMipValue1(class FMaterialCompiler* Compiler)
{
	if (MipValueMode == TMVM_Derivative && CoordinatesDY.GetTracedInput().IsConnected())
	{
		return CoordinatesDY.Compile(Compiler);
	}

	return INDEX_NONE;
}
#endif // WITH_EDITOR

UMaterialExpressionRuntimeVirtualTextureOutput::UMaterialExpressionRuntimeVirtualTextureOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VirtualTexture;
		FConstructorStatics()
			: NAME_VirtualTexture(LOCTEXT("VirtualTexture", "VirtualTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VirtualTexture);
#endif

#if WITH_EDITOR
	Outputs.Reset();
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionRuntimeVirtualTextureOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 CodeInput = INDEX_NONE;
	uint8 OutputAttributeMask = 0;

	// Order of outputs generates function names GetVirtualTextureOutput{index}
	// These must match the function names called in VirtualTextureMaterial.usf
	if (OutputIndex == 0)
	{
		CodeInput = BaseColor.IsConnected() ? BaseColor.Compile(Compiler) : Compiler->Constant3(0.f, 0.f, 0.f);
		OutputAttributeMask |= BaseColor.IsConnected() ? (1 << (uint8)ERuntimeVirtualTextureAttributeType::BaseColor) : 0;
	}
	else if (OutputIndex == 1)
	{
		CodeInput = Specular.IsConnected() ? Specular.Compile(Compiler) : Compiler->Constant(0.5f);
		OutputAttributeMask |= Specular.IsConnected() ? (1 << (uint8)ERuntimeVirtualTextureAttributeType::Specular) : 0;
	}
	else if (OutputIndex == 2)
	{
		CodeInput = Roughness.IsConnected() ? Roughness.Compile(Compiler) : Compiler->Constant(0.5f);
		OutputAttributeMask |= Roughness.IsConnected() ? (1 << (uint8)ERuntimeVirtualTextureAttributeType::Roughness) : 0;
	}
	else if (OutputIndex == 3)
	{
		CodeInput = Normal.IsConnected() ? Normal.Compile(Compiler) : Compiler->Constant3(0.f, 0.f, 1.f);
		OutputAttributeMask |= Normal.IsConnected() ? (1 << (uint8)ERuntimeVirtualTextureAttributeType::Normal) : 0;
	}
	else if (OutputIndex == 4)
	{
		CodeInput = WorldHeight.IsConnected() ? WorldHeight.Compile(Compiler) : Compiler->Constant(0.f);
		OutputAttributeMask |= WorldHeight.IsConnected() ? (1 << (uint8)ERuntimeVirtualTextureAttributeType::WorldHeight) : 0;
	}
	else if (OutputIndex == 5)
	{
		CodeInput = Opacity.IsConnected() ? Opacity.Compile(Compiler) : Compiler->Constant(1.f);
	}
	else if (OutputIndex == 6)
	{
		CodeInput = Mask.IsConnected() ? Mask.Compile(Compiler) : Compiler->Constant(1.f);
		OutputAttributeMask |= Mask.IsConnected() ? (1 << (uint8)ERuntimeVirtualTextureAttributeType::Mask) : 0;
	}

	Compiler->VirtualTextureOutput(OutputAttributeMask);
	return Compiler->CustomOutput(this, OutputIndex, CodeInput);
}

void UMaterialExpressionRuntimeVirtualTextureOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Runtime Virtual Texture Output")));
}

#endif // WITH_EDITOR

int32 UMaterialExpressionRuntimeVirtualTextureOutput::GetNumOutputs() const
{
	return 7; 
}

FString UMaterialExpressionRuntimeVirtualTextureOutput::GetFunctionName() const
{
	return TEXT("GetVirtualTextureOutput"); 
}

FString UMaterialExpressionRuntimeVirtualTextureOutput::GetDisplayName() const
{
	return TEXT("Runtime Virtual Texture"); 
}

UMaterialExpressionRuntimeVirtualTextureSample::UMaterialExpressionRuntimeVirtualTextureSample(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VirtualTexture;
		FConstructorStatics()
			: NAME_VirtualTexture(LOCTEXT("VirtualTexture", "VirtualTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VirtualTexture);
#endif

#if WITH_EDITOR
	InitOutputs();
	bShowOutputNameOnPin = true;
	bShowMaskColorsOnPin = false;
#endif
}

bool UMaterialExpressionRuntimeVirtualTextureSample::InitVirtualTextureDependentSettings()
{
	bool bChanged = false;
	if (VirtualTexture != nullptr)
	{
		bChanged |= MaterialType != VirtualTexture->GetMaterialType();
		MaterialType = VirtualTexture->GetMaterialType();
		bChanged |= bSinglePhysicalSpace != VirtualTexture->GetSinglePhysicalSpace();
		bSinglePhysicalSpace = VirtualTexture->GetSinglePhysicalSpace();
		bChanged |= bAdaptive != VirtualTexture->GetAdaptivePageTable();
		bAdaptive = VirtualTexture->GetAdaptivePageTable();
	}
	return bChanged;
}

void UMaterialExpressionRuntimeVirtualTextureSample::InitOutputs()
{
#if WITH_EDITORONLY_DATA
	Outputs.Reset();
	
	Outputs.Add(FExpressionOutput(TEXT("BaseColor"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Specular")));
	Outputs.Add(FExpressionOutput(TEXT("Roughness")));
	Outputs.Add(FExpressionOutput(TEXT("Normal")));
	Outputs.Add(FExpressionOutput(TEXT("WorldHeight")));
	Outputs.Add(FExpressionOutput(TEXT("Mask")));
#endif // WITH_EDITORONLY_DATA
}

UObject* UMaterialExpressionRuntimeVirtualTextureSample::GetReferencedTexture() const
{
	return VirtualTexture; 
}

#if WITH_EDITOR

void UMaterialExpressionRuntimeVirtualTextureSample::PostLoad()
{
	Super::PostLoad();

	// Convert BaseColor_Normal_DEPRECATED
	if (MaterialType == ERuntimeVirtualTextureMaterialType::BaseColor_Normal_DEPRECATED)
	{
		MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;
	}

	InitOutputs();
}

void UMaterialExpressionRuntimeVirtualTextureSample::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Update MaterialType setting to match VirtualTexture
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetName() == TEXT("VirtualTexture"))
	{
		if (VirtualTexture != nullptr)
		{
			InitVirtualTextureDependentSettings();
			FEditorSupportDelegates::ForcePropertyWindowRebuild.Broadcast(this);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

int32 UMaterialExpressionRuntimeVirtualTextureSample::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Is this a valid UMaterialExpressionRuntimeVirtualTextureSampleParameter?
	const bool bIsParameter = HasAParameterName() && GetParameterName().IsValid() && !GetParameterName().IsNone();

	// Check validity of current virtual texture
	bool bIsVirtualTextureValid = VirtualTexture != nullptr;
	if (!bIsVirtualTextureValid)
	{
		if (!bIsParameter)
		{
			Compiler->Error(TEXT("Missing input Virtual Texture"));
		}
	}
	else if (VirtualTexture->GetMaterialType() != MaterialType)
	{
		UEnum const* Enum = StaticEnum<ERuntimeVirtualTextureMaterialType>();
		FString MaterialTypeDisplayName = Enum->GetDisplayNameTextByValue((int64)MaterialType).ToString();
		FString TextureTypeDisplayName = Enum->GetDisplayNameTextByValue((int64)VirtualTexture->GetMaterialType()).ToString();

		Compiler->Errorf(TEXT("%Material type is '%s', should be '%s' to match %s"),
			*MaterialTypeDisplayName,
			*TextureTypeDisplayName,
			*VirtualTexture->GetName());

		bIsVirtualTextureValid = false;
	}
	else if (VirtualTexture->GetSinglePhysicalSpace() != bSinglePhysicalSpace)
	{
		Compiler->Errorf(TEXT("%Page table packing is '%d', should be '%d' to match %s"),
			bSinglePhysicalSpace ? 1 : 0,
			VirtualTexture->GetSinglePhysicalSpace() ? 1 : 0,
			*VirtualTexture->GetName());

		bIsVirtualTextureValid = false;
	}
	else if ((VirtualTexture->GetAdaptivePageTable()) != bAdaptive)
	{
		Compiler->Errorf(TEXT("Adaptive page table is '%d', should be '%d' to match %s"),
			bAdaptive ? 1 : 0,
			VirtualTexture->GetAdaptivePageTable() ? 1 : 0,
			*VirtualTexture->GetName());

		bIsVirtualTextureValid = false;
	}

	// Calculate the virtual texture layer and sampling/unpacking functions for this output
	// Fallback to a sensible default value if the output isn't valid for the bound virtual texture
	uint32 UnpackTarget = 0;
	uint32 UnpackMask = 0;
	EVirtualTextureUnpackType UnpackType = EVirtualTextureUnpackType::None;

	bool bIsBaseColorValid = false;
	bool bIsSpecularValid = false;
	bool bIsNormalValid = false;
	bool bIsWorldHeightValid = false;
	bool bIsMaskValid = false;

	switch (MaterialType)
	{
	case ERuntimeVirtualTextureMaterialType::BaseColor: bIsBaseColorValid = true; break;
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular: bIsBaseColorValid = bIsNormalValid = bIsSpecularValid = true; break;
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg: bIsBaseColorValid = bIsNormalValid = bIsSpecularValid = true; break;
	case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg: bIsBaseColorValid = bIsNormalValid = bIsSpecularValid = bIsMaskValid = true; break;
	case ERuntimeVirtualTextureMaterialType::WorldHeight: bIsWorldHeightValid = true; break;
	}

	switch (OutputIndex)
	{
	case 0: 
		if ((bIsParameter || bIsVirtualTextureValid) && bIsBaseColorValid)
		{
			switch (MaterialType)
			{
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg: UnpackType = EVirtualTextureUnpackType::BaseColorYCoCg; break;
			default: UnpackTarget = 0; UnpackMask = 0x7; break;
			}
		}
		else
		{
			return Compiler->Constant3(0.f, 0.f, 0.f);
		}
		break;
	case 1:
		if ((bIsParameter || bIsVirtualTextureValid) && bIsSpecularValid)
		{
			switch (MaterialType)
			{
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular:  UnpackTarget = 1; UnpackMask = 0x1; break;
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg: UnpackTarget = 2; UnpackMask = 0x1; break;
			}
		}
		else
		{
			return Compiler->Constant(0.5f);
		}
		break;
	case 2:
		if ((bIsParameter || bIsVirtualTextureValid) && bIsSpecularValid)
		{
			switch (MaterialType)
			{
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular: UnpackTarget = 1; UnpackMask = 0x2; break;
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg: UnpackTarget = 2; UnpackMask = 0x2; break;
			}
		}
		else
		{
			return Compiler->Constant(0.5f);
		}
		break;
	case 3:
		if ((bIsParameter || bIsVirtualTextureValid) && bIsNormalValid)
		{
			switch (MaterialType)
			{
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular: UnpackType = EVirtualTextureUnpackType::NormalBC3BC3; break;
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_YCoCg:
			case ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg: UnpackType = EVirtualTextureUnpackType::NormalBC5BC1; break;
			}
		}
		else
		{
			return Compiler->Constant3(0.f, 0.f, 1.f);
		}
		break;
	case 4:
		if ((bIsParameter || bIsVirtualTextureValid) && bIsWorldHeightValid)
		{
			UnpackType = EVirtualTextureUnpackType::HeightR16;
		}
		else
		{
			return Compiler->Constant(0.f);
		}
		break;
	case 5:
		if ((bIsParameter || bIsVirtualTextureValid) && bIsMaskValid)
		{
			UnpackTarget = 2; UnpackMask = 0x8; break;
		}
		else
		{
			return Compiler->Constant(1.f);
		}
		break;
	default:
		return INDEX_NONE;
	}
	
	// Compile the texture object references
	const int32 TextureLayerCount = URuntimeVirtualTexture::GetLayerCount(MaterialType);
	check(TextureLayerCount <= RuntimeVirtualTexture::MaxTextureLayers);

	int32 TextureCodeIndex[RuntimeVirtualTexture::MaxTextureLayers] = { INDEX_NONE };
	int32 TextureReferenceIndex[RuntimeVirtualTexture::MaxTextureLayers] = { INDEX_NONE };
	for (int32 TexureLayerIndex = 0; TexureLayerIndex < TextureLayerCount; TexureLayerIndex++)
	{
		const int32 PageTableLayerIndex = bSinglePhysicalSpace ? 0 : TexureLayerIndex;

		if (bIsParameter)
		{
			TextureCodeIndex[TexureLayerIndex] = Compiler->VirtualTextureParameter(GetParameterName(), VirtualTexture, TexureLayerIndex, PageTableLayerIndex, TextureReferenceIndex[TexureLayerIndex], SAMPLERTYPE_VirtualMasks);
		}
		else
		{
			TextureCodeIndex[TexureLayerIndex] = Compiler->VirtualTexture(VirtualTexture, TexureLayerIndex, PageTableLayerIndex, TextureReferenceIndex[TexureLayerIndex], SAMPLERTYPE_VirtualMasks);
		}
	}

	// Compile the runtime virtual texture uniforms
	int32 Uniforms[ERuntimeVirtualTextureShaderUniform_Count];
	for (int32 UniformIndex = 0; UniformIndex < ERuntimeVirtualTextureShaderUniform_Count; ++UniformIndex)
	{
		if (bIsParameter)
		{
			Uniforms[UniformIndex] = Compiler->VirtualTextureUniform(GetParameterName(), TextureReferenceIndex[0], UniformIndex);
		}
		else
		{
			Uniforms[UniformIndex] = Compiler->VirtualTextureUniform(TextureReferenceIndex[0], UniformIndex);
		}
	}

	// Compile the coordinates
	// We use the virtual texture world space transform by default
	int32 CoordinateIndex = INDEX_NONE;

	if (Coordinates.GetTracedInput().Expression != nullptr && WorldPosition.GetTracedInput().Expression != nullptr)
	{
		Compiler->Errorf(TEXT("Only one of 'Coordinates' and 'WorldPosition' can be used"));
	}

	if (Coordinates.GetTracedInput().Expression != nullptr)
	{
		CoordinateIndex = Coordinates.Compile(Compiler);
	}
	else
	{
		int32 WorldPositionIndex = INDEX_NONE;
		if (WorldPosition.GetTracedInput().Expression != nullptr)
		{
			WorldPositionIndex = WorldPosition.Compile(Compiler);
		}
		else
		{
			WorldPositionIndex = Compiler->WorldPosition(WPT_Default);
		}
		
		const int32 P0 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform0];
		const int32 P1 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1];
		const int32 P2 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2];
		CoordinateIndex = Compiler->VirtualTextureWorldToUV(WorldPositionIndex, P0, P1, P2);
	}
	
	// Compile the mip level for the current mip value mode
	ETextureMipValueMode TextureMipLevelMode = TMVM_None;
	int32 MipValueIndex = INDEX_NONE;
	if (MipValue.GetTracedInput().Expression != nullptr)
	{
		switch (MipValueMode)
		{
		case RVTMVM_MipLevel: TextureMipLevelMode = TMVM_MipLevel; break;
		case RVTMVM_MipBias: TextureMipLevelMode = TMVM_MipBias; break;
		}
		if (TextureMipLevelMode != TMVM_None)
		{
			MipValueIndex = MipValue.Compile(Compiler);
		}
	}

	// Convert texture address mode to matching sampler source mode.
	// Would be better if ESamplerSourceMode had a Mirror enum that we could also use...
	ESamplerSourceMode SamplerSourceMode = SSM_Clamp_WorldGroupSettings;
	switch (TextureAddressMode)
	{
	case RVTTA_Clamp:
		SamplerSourceMode = SSM_Clamp_WorldGroupSettings;
		break;
	case RVTTA_Wrap:
		SamplerSourceMode = SSM_Wrap_WorldGroupSettings;
		break;
	}

	// Compile the texture sample code
	const bool bAutomaticMipViewBias = true;
	int32 SampleCodeIndex[RuntimeVirtualTexture::MaxTextureLayers] = { INDEX_NONE };
	for (int32 TexureLayerIndex = 0; TexureLayerIndex < TextureLayerCount; TexureLayerIndex++)
	{
		SampleCodeIndex[TexureLayerIndex] = Compiler->TextureSample(
			TextureCodeIndex[TexureLayerIndex],
			CoordinateIndex, 
			SAMPLERTYPE_VirtualMasks,
			MipValueIndex, INDEX_NONE, TextureMipLevelMode, SamplerSourceMode,
			TextureReferenceIndex[TexureLayerIndex],
			bAutomaticMipViewBias, bAdaptive);
	}

	// Compile any unpacking code
	int32 UnpackCodeIndex = INDEX_NONE;
	if (UnpackType != EVirtualTextureUnpackType::None)
	{
		int32 P0 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldHeightUnpack];
		UnpackCodeIndex = Compiler->VirtualTextureUnpack(SampleCodeIndex[0], SampleCodeIndex[1], SampleCodeIndex[2], P0, UnpackType);
	}
	else
	{
		UnpackCodeIndex = SampleCodeIndex[UnpackTarget] == INDEX_NONE ? INDEX_NONE : Compiler->ComponentMask(SampleCodeIndex[UnpackTarget], UnpackMask & 1, (UnpackMask >> 1) & 1, (UnpackMask >> 2) & 1, (UnpackMask >> 3) & 1);
	}
	return UnpackCodeIndex;
}

void UMaterialExpressionRuntimeVirtualTextureSample::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Runtime Virtual Texture Sample")));
}

#endif // WITH_EDITOR

UMaterialExpressionRuntimeVirtualTextureSampleParameter::UMaterialExpressionRuntimeVirtualTextureSampleParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_Parameters(LOCTEXT("Parameters", "Parameters"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bIsParameterExpression = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

bool UMaterialExpressionRuntimeVirtualTextureSampleParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutValue = VirtualTexture;
		return true;
	}

	return false;
}

void UMaterialExpressionRuntimeVirtualTextureSampleParameter::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);
#if WITH_EDITOR
	if (HasConnectedOutputs())
#endif
	{
		OutParameterInfo.AddUnique(NewParameter);
		if (CurrentSize != OutParameterInfo.Num())
		{
			OutParameterIds.Add(ExpressionGUID);
		}
	}
}

#if WITH_EDITOR

bool UMaterialExpressionRuntimeVirtualTextureSampleParameter::SetParameterValue(FName InParameterName, URuntimeVirtualTexture* InValue)
{
	if (InParameterName == ParameterName)
	{
		VirtualTexture = InValue;
		return true;
	}

	return false;
}

void UMaterialExpressionRuntimeVirtualTextureSampleParameter::SetEditableName(const FString& NewName)
{
	ParameterName = *NewName;
}

FString UMaterialExpressionRuntimeVirtualTextureSampleParameter::GetEditableName() const
{
	return ParameterName.ToString();
}

void UMaterialExpressionRuntimeVirtualTextureSampleParameter::ValidateParameterName(const bool bAllowDuplicateName)
{
	ValidateParameterNameInternal(this, Material, bAllowDuplicateName);
}

void UMaterialExpressionRuntimeVirtualTextureSampleParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Runtime Virtual Texture Sample Param ")));
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionRuntimeVirtualTextureSampleParameter::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	if (ParameterName.ToString().Contains(SearchQuery))
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

void UMaterialExpressionRuntimeVirtualTextureSampleParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	URuntimeVirtualTexture* Value = nullptr;
	if (Material->GetRuntimeVirtualTextureParameterValue(FMaterialParameterInfo(OtherExpression->GetParameterName()), Value))
	{
		VirtualTexture = Value;
		FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionRuntimeVirtualTextureSampleParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionRuntimeVirtualTextureSampleParameter, VirtualTexture));
		FPropertyChangedEvent PropertyChangedEvent(ParamProperty);
		PostEditChangeProperty(PropertyChangedEvent);
	}
}

#endif

UMaterialExpressionRuntimeVirtualTextureReplace::UMaterialExpressionRuntimeVirtualTextureReplace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VirtualTexture;
		FConstructorStatics()
			: NAME_VirtualTexture(LOCTEXT("VirtualTexture", "VirtualTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VirtualTexture);
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionRuntimeVirtualTextureReplace::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Default.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RuntimeVirtualTextureReplace input 'Default'"));
	}

	if (!VirtualTextureOutput.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RuntimeVirtualTextureReplace input 'VirtualTextureOutput'"));
	}

	int32 Arg1 = Default.Compile(Compiler);
	int32 Arg2 = VirtualTextureOutput.Compile(Compiler);
	return Compiler->VirtualTextureOutputReplace(Arg1, Arg2);
}

bool UMaterialExpressionRuntimeVirtualTextureReplace::IsResultMaterialAttributes(int32 OutputIndex)
{
	for (FExpressionInput* ExpressionInput : GetInputs())
	{
		if (ExpressionInput->GetTracedInput().Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}
	return false;
}

void UMaterialExpressionRuntimeVirtualTextureReplace::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("RuntimeVirtualTextureReplace"));
}

#endif // WITH_EDITOR

UMaterialExpressionVirtualTextureFeatureSwitch::UMaterialExpressionVirtualTextureFeatureSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VirtualTexture;
		FConstructorStatics()
			: NAME_VirtualTexture(LOCTEXT("VirtualTexture", "VirtualTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VirtualTexture);
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionVirtualTextureFeatureSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Yes.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing VirtualTextureFeatureSwitch input 'Yes'"));
	}

	if (!No.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing VirtualTextureFeatureSwitch input 'No'"));
	}

	if (UseVirtualTexturing(Compiler->GetFeatureLevel(), Compiler->GetTargetPlatform()))
	{
		return Yes.Compile(Compiler);
	}
	
	return No.Compile(Compiler);
}

bool UMaterialExpressionVirtualTextureFeatureSwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	for (FExpressionInput* ExpressionInput : GetInputs())
	{
		if (ExpressionInput->GetTracedInput().Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}
	return false;
}

void UMaterialExpressionVirtualTextureFeatureSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("VirtualTextureFeatureSwitch"));
}

#endif // WITH_EDITOR

UMaterialExpressionAdd::UMaterialExpressionAdd(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 0.0f;
	ConstB = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

//
//  UMaterialExpressionTextureSampleParameter
//
UMaterialExpressionTextureSampleParameter::UMaterialExpressionTextureSampleParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Obsolete;
		FConstructorStatics()
			: NAME_Obsolete(LOCTEXT( "Obsolete", "Obsolete" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	bIsParameterExpression = true;
	bShowTextureInputPin = false;

	MenuCategories.Empty();
	MenuCategories.Add( ConstructorStatics.NAME_Obsolete);
	ApplyChannelNames();
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureSampleParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	FString ErrorMessage;
	if (!TextureIsValid(Texture, ErrorMessage))
	{
		return CompilerError(Compiler, *ErrorMessage);
	}

	if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("TextureSampleParameter")), Texture, SamplerType))
	{
		return INDEX_NONE;
	}

	if (!ParameterName.IsValid() || ParameterName.IsNone())
	{
		return UMaterialExpressionTextureSample::Compile(Compiler, OutputIndex);
	}

	return CompileTextureSample(
		Compiler,
		Texture,
		Coordinates.GetTracedInput().Expression ? Coordinates.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false),
		SamplerType,
		ParameterName,
		CompileMipValue0(Compiler),
		CompileMipValue1(Compiler),
		MipValueMode,
		SamplerSource,
		AutomaticViewMipBias);
}

void UMaterialExpressionTextureSampleParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Texture Param")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

void UMaterialExpressionTextureSampleParameter::ValidateParameterName(const bool bAllowDuplicateName)
{
	ValidateParameterNameInternal(this, Material, bAllowDuplicateName);
}

void UMaterialExpressionTextureSampleParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	UTexture* ExistingValue;
	if (Material->GetTextureParameterValue(OtherExpression->GetParameterName(), ExistingValue))
	{
		Texture = ExistingValue;
		FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionTextureSampleParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionTextureSampleParameter, Texture));
		FPropertyChangedEvent PropertyChangedEvent(ParamProperty);
		PostEditChangeProperty(PropertyChangedEvent);
	}
}
#endif // WITH_EDITOR

bool UMaterialExpressionTextureSampleParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutValue = Texture;
		return true;
	}

	return false;
}

#if WITH_EDITOR
bool UMaterialExpressionTextureSampleParameter::SetParameterValue(FName InParameterName, UTexture* InValue)
{
	if (InParameterName == ParameterName)
	{
		Texture = InValue;
		return true;
	}
	return false;
}

void UMaterialExpressionTextureSampleParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionTextureSampleParameter, ChannelNames))
	{
		ApplyChannelNames();

		if (GraphNode)
		{
			GraphNode->ReconstructNode();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialExpressionTextureSampleParameter::ApplyChannelNames()
{
	static const FName Red("R");
	static const FName Green("G");
	static const FName Blue("B");
	static const FName Alpha("A");
	if (GetOutputType(0) != MCT_Texture)
	{
		Outputs[1].OutputName = !ChannelNames.R.IsEmpty() ? FName(*ChannelNames.R.ToString()) : Red;
		Outputs[2].OutputName = !ChannelNames.G.IsEmpty() ? FName(*ChannelNames.G.ToString()) : Green;
		Outputs[3].OutputName = !ChannelNames.B.IsEmpty() ? FName(*ChannelNames.B.ToString()) : Blue;
		Outputs[4].OutputName = !ChannelNames.A.IsEmpty() ? FName(*ChannelNames.A.ToString()) : Alpha;
	}
}

bool UMaterialExpressionTextureSampleParameter::TextureIsValid(UTexture* /*InTexture*/, FString& OutMessage)
{
	OutMessage = TEXT("Invalid texture type");
	return false;
}

void UMaterialExpressionTextureSampleParameter::SetDefaultTexture()
{
	// Does nothing in the base case...
}
#endif // WITH_EDITOR

void UMaterialExpressionTextureSampleParameter::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);

#if WITH_EDITOR
	if (HasConnectedOutputs())
#endif
	{
		OutParameterInfo.AddUnique(NewParameter);

		if (CurrentSize != OutParameterInfo.Num())
		{
			OutParameterIds.Add(ExpressionGUID);
		}
	}
}

//
//  UMaterialExpressionTextureObjectParameter
//
UMaterialExpressionTextureObjectParameter::UMaterialExpressionTextureObjectParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultTexture2D;
		FText NAME_Texture;
		FText NAME_Parameters;
		FConstructorStatics()
			: DefaultTexture2D(TEXT("/Engine/EngineResources/DefaultTexture"))
			, NAME_Texture(LOCTEXT( "Texture", "Texture" ))
			, NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Texture = ConstructorStatics.DefaultTexture2D.Object;

#if WITH_EDITORONLY_DATA
	MenuCategories.Empty();
	MenuCategories.Add(ConstructorStatics.NAME_Texture);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);

	Outputs.Empty();
	Outputs.Add(FExpressionOutput(TEXT("")));
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
bool UMaterialExpressionTextureObjectParameter::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	if (!InTexture)
	{
		OutMessage = TEXT("Requires valid texture");
		return false;
	}

	return true;
}

void UMaterialExpressionTextureObjectParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Param Tex Object")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

const TArray<FExpressionInput*> UMaterialExpressionTextureObjectParameter::GetInputs()
{
	// Hide the texture coordinate input
	return TArray<FExpressionInput*>();
}

int32 UMaterialExpressionTextureObjectParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	FString ErrorMessage;
	if (!TextureIsValid(Texture, ErrorMessage))
	{
		return CompilerError(Compiler, *ErrorMessage);
	}

	// It seems like this error should be checked here, but this can break existing materials, see https://jira.it.epicgames.net/browse/UE-68862
	/*if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("TextureObjectParameter")), Texture, SamplerType))
	{
		return INDEX_NONE;
	}*/

	return SamplerType == SAMPLERTYPE_External ? Compiler->ExternalTextureParameter(ParameterName, Texture) : Compiler->TextureParameter(ParameterName, Texture, SamplerType);
}

int32 UMaterialExpressionTextureObjectParameter::CompilePreview(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	FString ErrorMessage;
	if (!TextureIsValid(Texture, ErrorMessage))
	{
		return CompilerError(Compiler, *ErrorMessage);
	}

	// Preview the texture object by actually sampling it
	return CompileTextureSample(Compiler, Texture, Compiler->TextureCoordinate(0, false, false), SamplerType, ParameterName);
}
#endif // WITH_EDITOR

//
//  UMaterialExpressionTextureObject
//
UMaterialExpressionTextureObject::UMaterialExpressionTextureObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTexture2D> Object0;
		FText NAME_Texture;
		FText NAME_Functions;
		FConstructorStatics()
			: Object0(TEXT("/Engine/EngineResources/DefaultTexture"))
			, NAME_Texture(LOCTEXT( "Texture", "Texture" ))
			, NAME_Functions(LOCTEXT( "Functions", "Functions" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Texture = ConstructorStatics.Object0.Object;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Texture);
	MenuCategories.Add(ConstructorStatics.NAME_Functions);

	Outputs.Empty();
	Outputs.Add(FExpressionOutput(TEXT("")));

	bCollapsed = false;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UMaterialExpressionTextureObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty( PropertyChangedEvent );

	if ( PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetName() == TEXT("Texture") )
	{
		if ( Texture )
		{
			AutoSetSampleType();
			FEditorSupportDelegates::ForcePropertyWindowRebuild.Broadcast(this);
		}
	}
}

void UMaterialExpressionTextureObject::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Texture Object")); 
}


int32 UMaterialExpressionTextureObject::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Texture)
	{
		return CompilerError(Compiler, TEXT("Requires valid texture"));
	}

	// It seems like this error should be checked here, but this can break existing materials, see https://jira.it.epicgames.net/browse/UE-68862
	/*if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("TextureObject")), Texture, SamplerType))
	{
		return INDEX_NONE;
	}*/

	return SamplerType == SAMPLERTYPE_External ? Compiler->ExternalTexture(Texture) : Compiler->Texture(Texture, SamplerType);
}

int32 UMaterialExpressionTextureObject::CompilePreview(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Texture)
	{
		return CompilerError(Compiler, TEXT("Requires valid texture"));
	}

	return CompileTextureSample(Compiler, Texture, Compiler->TextureCoordinate(0, false, false), UMaterialExpressionTextureBase::GetSamplerTypeForTexture( Texture ));
}

uint32 UMaterialExpressionTextureObject::GetOutputType(int32 OutputIndex)
{
	if (Cast<UTextureCube>(Texture) != nullptr)
	{
		return MCT_TextureCube;
	}
	else if (Cast<UTexture2DArray>(Texture) != nullptr)
	{
		return MCT_Texture2DArray;
	}
	else if (Cast<UVolumeTexture>(Texture) != nullptr)
	{
		return MCT_VolumeTexture;
	}
	else
	{
		return MCT_Texture2D;
	}
}
#endif //WITH_EDITOR

//
//  UMaterialExpressionTextureProperty
//
UMaterialExpressionTextureProperty::UMaterialExpressionTextureProperty(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Texture", "Texture" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Property = TMTM_TextureSize;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
	bShowOutputNameOnPin = false;
	
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("")));
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureProperty::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{	
	if (!TextureObject.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("TextureSample> Missing input texture"));
	}

	const int32 TextureCodeIndex = TextureObject.Compile(Compiler);
	if (TextureCodeIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return Compiler->TextureProperty(TextureCodeIndex, Property);
}

void UMaterialExpressionTextureProperty::GetTexturesForceMaterialRecompile(TArray<UTexture *> &Textures) const
{
	UMaterialExpression *TextureObjectExpression = TextureObject.GetTracedInput().Expression;

	if (TextureObjectExpression && TextureObjectExpression->IsA(UMaterialExpressionTextureBase::StaticClass()))
	{
		UMaterialExpressionTextureBase *TextureExpressionBase = Cast<UMaterialExpressionTextureBase>(TextureObjectExpression);
		if (TextureExpressionBase->Texture)
		{
			Textures.Add(TextureExpressionBase->Texture);
		}
	}
}

void UMaterialExpressionTextureProperty::GetCaption(TArray<FString>& OutCaptions) const
{
#if WITH_EDITOR
	const UEnum* TexturePropertyEnum = StaticEnum<EMaterialExposedTextureProperty>();
	check(TexturePropertyEnum);

	const FString PropertyDisplayName = TexturePropertyEnum->GetDisplayNameTextByValue(Property).ToString();
#else
	const FString PropertyDisplayName = TEXT("");
#endif

	OutCaptions.Add(PropertyDisplayName);
}

// this define is only used for the following function
#define IF_INPUT_RETURN(Type) if(!InputIndex) return Type; --InputIndex
uint32 UMaterialExpressionTextureProperty::GetInputType(int32 InputIndex)
{
	// TextureObject
	IF_INPUT_RETURN(MCT_Texture);
	return MCT_Unknown;
}
#undef IF_INPUT_RETURN


bool UMaterialExpressionTextureProperty::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	TArray<FString> Captions;
	GetCaption(Captions);
	for (const FString& Caption : Captions)
	{
		if (Caption.Contains(SearchQuery))
		{
			return true;
		}
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

#endif

//
//  UMaterialExpressionTextureSampleParameter2D
//
UMaterialExpressionTextureSampleParameter2D::UMaterialExpressionTextureSampleParameter2D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultTexture;
		FText NAME_Texture;
		FText NAME_Parameters;
		FConstructorStatics()
			: DefaultTexture(TEXT("/Engine/EngineResources/DefaultTexture"))
			, NAME_Texture(LOCTEXT( "Texture", "Texture" ))
			, NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Texture = ConstructorStatics.DefaultTexture.Object;

#if WITH_EDITORONLY_DATA
	MenuCategories.Empty();
	MenuCategories.Add(ConstructorStatics.NAME_Texture);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

#if WITH_EDITOR
void UMaterialExpressionTextureSampleParameter2D::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Param2D")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionTextureSampleParameter2D::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	const bool bRequiresVirtualTexture = IsVirtualSamplerType(SamplerType);
	if (!InTexture)
	{
		OutMessage = TEXT("Found NULL, requires Texture2D");
		return false;
	}
	else if (!(InTexture->GetMaterialType() & (MCT_Texture2D | MCT_TextureExternal | MCT_TextureVirtual)))
	{
		OutMessage = FString::Printf(TEXT("Found %s, requires Texture2D"), *InTexture->GetClass()->GetName());
		return false;
	}
	else if (bRequiresVirtualTexture && !InTexture->VirtualTextureStreaming)
	{
		OutMessage = TEXT("Sampler requires VirtualTexture");
		return false;
	}
	else if (!bRequiresVirtualTexture && InTexture->VirtualTextureStreaming)
	{
		OutMessage = TEXT("Sampler requires non-VirtualTexture");
		return false;
	}

	return true;
}

void UMaterialExpressionTextureSampleParameter2D::SetDefaultTexture()
{
	Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"), nullptr, LOAD_None, nullptr);
}

bool UMaterialExpressionTextureSampleParameter::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if( ParameterName.ToString().Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

FString UMaterialExpressionTextureSampleParameter::GetEditableName() const
{
	return ParameterName.ToString();
}

void UMaterialExpressionTextureSampleParameter::SetEditableName(const FString& NewName)
{
	ParameterName = *NewName;
}
#endif


//
//  UMaterialExpressionTextureSampleParameterCube
//
UMaterialExpressionTextureSampleParameterCube::UMaterialExpressionTextureSampleParameterCube(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTextureCube> DefaultTextureCube;
		FText NAME_Texture;
		FText NAME_Parameters;
		FConstructorStatics()
			: DefaultTextureCube(TEXT("/Engine/EngineResources/DefaultTextureCube"))
			, NAME_Texture(LOCTEXT( "Texture", "Texture" ))
			, NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Texture = ConstructorStatics.DefaultTextureCube.Object;

#if WITH_EDITORONLY_DATA
	MenuCategories.Empty();
	MenuCategories.Add(ConstructorStatics.NAME_Texture);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureSampleParameterCube::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Coordinates.GetTracedInput().Expression)
	{
		return CompilerError(Compiler, TEXT("Cube sample needs UV input"));
	}

	return UMaterialExpressionTextureSampleParameter::Compile(Compiler, OutputIndex);
}

void UMaterialExpressionTextureSampleParameterCube::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ParamCube")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionTextureSampleParameterCube::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	if (!InTexture)
	{
		OutMessage = TEXT("Found NULL, requires TextureCube");
		return false;
	}
	else if (!(InTexture->GetMaterialType() & MCT_TextureCube))
	{
		OutMessage = FString::Printf(TEXT("Found %s, requires TextureCube"), *InTexture->GetClass()->GetName());
		return false;
	}

	return true;
}

void UMaterialExpressionTextureSampleParameterCube::SetDefaultTexture()
{
	Texture = LoadObject<UTextureCube>(nullptr, TEXT("/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"), nullptr, LOAD_None, nullptr);
}
#endif // WITH_EDITOR

//
//  UMaterialExpressionTextureSampleParameter2DArray
//
UMaterialExpressionTextureSampleParameter2DArray::UMaterialExpressionTextureSampleParameter2DArray(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization. No default texture array added.
	struct FConstructorStatics
	{
		FText NAME_Texture;
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_Texture(LOCTEXT("Texture", "Texture"))
			, NAME_Parameters(LOCTEXT("Parameters", "Parameters"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Empty();
	MenuCategories.Add(ConstructorStatics.NAME_Texture);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureSampleParameter2DArray::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
#if PLATFORM_ANDROID
	return CompilerError(Compiler, TEXT("Texture2DArrays not supported on selected platform."));
#endif

	if (!Coordinates.GetTracedInput().Expression)
	{
		return CompilerError(Compiler, TEXT("2D array sample needs UVW input"));
	}

	return UMaterialExpressionTextureSampleParameter::Compile(Compiler, OutputIndex);
}

void UMaterialExpressionTextureSampleParameter2DArray::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Param2DArray"));
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionTextureSampleParameter2DArray::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	if (!InTexture)
	{
		OutMessage = TEXT("Found NULL, requires Texture2DArray");
		return false;
	}
	else if (!(InTexture->GetMaterialType() & MCT_Texture2DArray))
	{
		OutMessage = FString::Printf(TEXT("Found %s, requires Texture2DArray"), *InTexture->GetClass()->GetName());
		return false;
	}

	return true;
}
#endif // WITH_EDITOR

const TCHAR* UMaterialExpressionTextureSampleParameter2DArray::GetRequirements()
{
	return TEXT("Requires Texture2DArray");
}

//
//  UMaterialExpressionTextureSampleParameterVolume
//
UMaterialExpressionTextureSampleParameterVolume::UMaterialExpressionTextureSampleParameterVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UVolumeTexture> DefaultVolumeTexture;
		FText NAME_Texture;
		FText NAME_Parameters;
		FConstructorStatics()
			: DefaultVolumeTexture(TEXT("/Engine/EngineResources/DefaultVolumeTexture"))
			, NAME_Texture(LOCTEXT( "Texture", "Texture" ))
			, NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Texture = ConstructorStatics.DefaultVolumeTexture.Object;

#if WITH_EDITORONLY_DATA
	MenuCategories.Empty();
	MenuCategories.Add(ConstructorStatics.NAME_Texture);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureSampleParameterVolume::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Coordinates.GetTracedInput().Expression)
	{
		return CompilerError(Compiler, TEXT("Volume sample needs UVW input"));
	}

	return UMaterialExpressionTextureSampleParameter::Compile(Compiler, OutputIndex);
}

void UMaterialExpressionTextureSampleParameterVolume::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ParamVolume")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionTextureSampleParameterVolume::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	if (!InTexture)
	{
		OutMessage = TEXT("Found NULL, requires VolumeTexture");
		return false;
	}
	else if (!(InTexture->GetMaterialType() & MCT_VolumeTexture))
	{
		OutMessage = FString::Printf(TEXT("Found %s, requires VolumeTexture"), *InTexture->GetClass()->GetName());
		return false;
	}

	return true;
}

void UMaterialExpressionTextureSampleParameterVolume::SetDefaultTexture()
{
	Texture = LoadObject<UVolumeTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultVolumeTexture.DefaultVolumeTexture"), nullptr, LOAD_None, nullptr);
}
#endif // WITH_EDITOR

/** 
 * Performs a SubUV operation, which is doing a texture lookup into a sub rectangle of a texture, and optionally blending with another rectangle.  
 * This supports both sprites and mesh emitters.
 */
static int32 ParticleSubUV(FMaterialCompiler* Compiler, int32 TextureIndex, UTexture* DefaultTexture, EMaterialSamplerType SamplerType, FExpressionInput& Coordinates, bool bBlend)
{
	return Compiler->ParticleSubUV(TextureIndex, SamplerType, bBlend);
}

/** 
 *	UMaterialExpressionTextureSampleParameterSubUV
 */
UMaterialExpressionTextureSampleParameterSubUV::UMaterialExpressionTextureSampleParameterSubUV(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bBlend = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Particles);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureSampleParameterSubUV::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	FString ErrorMessage;
	if (!TextureIsValid(Texture, ErrorMessage))
	{
		return CompilerError(Compiler, *ErrorMessage);
	}

	if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("TextureSampleParameterSubUV")), Texture, SamplerType))
	{
		return INDEX_NONE;
	}

	int32 TextureCodeIndex = Compiler->TextureParameter(ParameterName, Texture, SamplerType);
	return ParticleSubUV(Compiler, TextureCodeIndex, Texture, SamplerType, Coordinates, bBlend);
}

void UMaterialExpressionTextureSampleParameterSubUV::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Parameter SubUV"));
}

bool UMaterialExpressionTextureSampleParameterSubUV::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	return UMaterialExpressionTextureSampleParameter2D::TextureIsValid(InTexture, OutMessage);
}

int32 UMaterialExpressionAdd::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	return Compiler->Add(Arg1, Arg2);
}

void UMaterialExpressionAdd::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Add");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();
	if(!ATraced.Expression || !BTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstB);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionMultiply
//
UMaterialExpressionMultiply::UMaterialExpressionMultiply(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 0.0f;
	ConstB = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMultiply::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	return Compiler->Mul(Arg1, Arg2);
}

void UMaterialExpressionMultiply::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Multiply");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();

	if(!ATraced.Expression || !BTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstB);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

UMaterialExpressionDivide::UMaterialExpressionDivide(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif

	ConstA = 1.0f;
	ConstB = 2.0f;
}

#if WITH_EDITOR
int32 UMaterialExpressionDivide::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	return Compiler->Div(Arg1, Arg2);
}

void UMaterialExpressionDivide::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Divide");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();

	if(!ATraced.Expression || !BTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstB);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionSubtract
//
UMaterialExpressionSubtract::UMaterialExpressionSubtract(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 1.0f;
	ConstB = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSubtract::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	return Compiler->Sub(Arg1, Arg2);
}

void UMaterialExpressionSubtract::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Subtract");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();
	if(!ATraced.Expression || !BTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstB);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionSmoothStep
//

UMaterialExpressionSmoothStep::UMaterialExpressionSmoothStep(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Math(LOCTEXT("Math", "Math"))
			, NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstMin = 0.0f;
	ConstMax = 1.0f;
	ConstValue = 0.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSmoothStep::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = Min.GetTracedInput().Expression ? Min.Compile(Compiler) : Compiler->Constant(ConstMin);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = Max.GetTracedInput().Expression ? Max.Compile(Compiler) : Compiler->Constant(ConstMax);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg3 = Value.GetTracedInput().Expression ? Value.Compile(Compiler) : Compiler->Constant(ConstValue);

	return Compiler->SmoothStep(Arg1, Arg2, Arg3);
}

void UMaterialExpressionSmoothStep::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("SmoothStep");

	FExpressionInput MinTraced = Min.GetTracedInput();
	FExpressionInput MaxTraced = Max.GetTracedInput();
	FExpressionInput ValueTraced = Value.GetTracedInput();

	if (!MinTraced.Expression || !MaxTraced.Expression || !ValueTraced.Expression)
	{
		ret += TEXT("(");
		ret += MinTraced.Expression ? TEXT(",") : FString::Printf(TEXT("%.4g,"), ConstMin);
		ret += MaxTraced.Expression ? TEXT(",") : FString::Printf(TEXT("%.4g,"), ConstMax);
		ret += ValueTraced.Expression ? TEXT(")") : FString::Printf(TEXT("%.4g)"), ConstValue);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionStep
//

UMaterialExpressionStep::UMaterialExpressionStep(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Math(LOCTEXT("Math", "Math"))
			, NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstY = 0.0f;
	ConstX = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionStep::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = Y.GetTracedInput().Expression ? Y.Compile(Compiler) : Compiler->Constant(ConstY);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = X.GetTracedInput().Expression ? X.Compile(Compiler) : Compiler->Constant(ConstX);

	return Compiler->Step(Arg1, Arg2);
}

void UMaterialExpressionStep::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Step");

	FExpressionInput YTraced = Y.GetTracedInput();
	FExpressionInput XTraced = X.GetTracedInput();

	if (!YTraced.Expression || !XTraced.Expression)
	{
		ret += TEXT("(");
		ret += YTraced.Expression ? TEXT(",") : FString::Printf(TEXT("%.4g,"), ConstY);
		ret += XTraced.Expression ? TEXT(")") : FString::Printf(TEXT("%.4g)"), ConstX);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionInverseLerp
//

UMaterialExpressionInverseLinearInterpolate::UMaterialExpressionInverseLinearInterpolate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Math(LOCTEXT("Math", "Math"))
			, NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 0.0f;
	ConstB = 1.0f;
	ConstValue = 0.0f;
	bClampResult = false;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionInverseLinearInterpolate::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg3 = Value.GetTracedInput().Expression ? Value.Compile(Compiler) : Compiler->Constant(ConstValue);

	int32 Result = Compiler->InvLerp(Arg1, Arg2, Arg3);
	return bClampResult ? Compiler->Clamp(Result, Compiler->Constant(0.0f), Compiler->Constant(1.0f)) : Result;
}

void UMaterialExpressionInverseLinearInterpolate::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("InvLerp");

	FExpressionInput MinTraced = A.GetTracedInput();
	FExpressionInput MaxTraced = B.GetTracedInput();
	FExpressionInput ValueTraced = Value.GetTracedInput();

	if (!MinTraced.Expression || !MaxTraced.Expression || !ValueTraced.Expression)
	{
		ret += TEXT("(");
		ret += MinTraced.Expression ? TEXT(",") : FString::Printf(TEXT("%.4g,"), ConstA);
		ret += MaxTraced.Expression ? TEXT(",") : FString::Printf(TEXT("%.4g,"), ConstB);
		ret += ValueTraced.Expression ? TEXT(")") : FString::Printf(TEXT("%.4g)"), ConstValue);
	}

	if (bClampResult)
	{
		ret += TEXT(" Clamped");
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionLinearInterpolate
//

UMaterialExpressionLinearInterpolate::UMaterialExpressionLinearInterpolate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
			, NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 0;
	ConstB = 1;
	ConstAlpha = 0.5f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLinearInterpolate::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg3 = Alpha.GetTracedInput().Expression ? Alpha.Compile(Compiler) : Compiler->Constant(ConstAlpha);

	return Compiler->Lerp(Arg1, Arg2, Arg3);
}

void UMaterialExpressionLinearInterpolate::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Lerp");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();
	FExpressionInput AlphaTraced = Alpha.GetTracedInput();

	if(!ATraced.Expression || !BTraced.Expression || !AlphaTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstB);
		ret += AlphaTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstAlpha);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

UMaterialExpressionConstant::UMaterialExpressionConstant(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionConstant::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->Constant(R);
}

void UMaterialExpressionConstant::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf( TEXT("%.4g"), R ));
}

FString UMaterialExpressionConstant::GetDescription() const
{
	FString Result = FString(*GetClass()->GetName()).Mid(FCString::Strlen(TEXT("MaterialExpression")));
	Result += TEXT(" (");
	Result += Super::GetDescription();
	Result += TEXT(")");
	return Result;
}
#endif // WITH_EDITOR

UMaterialExpressionConstant2Vector::UMaterialExpressionConstant2Vector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
			, NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Constants);
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionConstant2Vector::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->Constant2(R,G);
}

void UMaterialExpressionConstant2Vector::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf( TEXT("%.3g,%.3g"), R, G ));
}

FString UMaterialExpressionConstant2Vector::GetDescription() const
{
	FString Result = FString(*GetClass()->GetName()).Mid(FCString::Strlen(TEXT("MaterialExpression")));
	Result += TEXT(" (");
	Result += Super::GetDescription();
	Result += TEXT(")");
	return Result;
}
#endif // WITH_EDITOR

UMaterialExpressionConstant3Vector::UMaterialExpressionConstant3Vector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
			, NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Constants);
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionConstant3Vector::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->Constant3(Constant.R,Constant.G,Constant.B);
}

void UMaterialExpressionConstant3Vector::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf( TEXT("%.3g,%.3g,%.3g"), Constant.R, Constant.G, Constant.B ));
}

FString UMaterialExpressionConstant3Vector::GetDescription() const
{
	FString Result = FString(*GetClass()->GetName()).Mid(FCString::Strlen(TEXT("MaterialExpression")));
	Result += TEXT(" (");
	Result += Super::GetDescription();
	Result += TEXT(")");
	return Result;
}
#endif // WITH_EDITOR

UMaterialExpressionConstant4Vector::UMaterialExpressionConstant4Vector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
			, NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Constants);
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionConstant4Vector::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->Constant4(Constant.R,Constant.G,Constant.B,Constant.A);
}


void UMaterialExpressionConstant4Vector::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf( TEXT("%.2g,%.2g,%.2g,%.2g"), Constant.R, Constant.G, Constant.B, Constant.A ));
}

FString UMaterialExpressionConstant4Vector::GetDescription() const
{
	FString Result = FString(*GetClass()->GetName()).Mid(FCString::Strlen(TEXT("MaterialExpression")));
	Result += TEXT(" (");
	Result += Super::GetDescription();
	Result += TEXT(")");
	return Result;
}
#endif // WITH_EDITOR

UMaterialExpressionClamp::UMaterialExpressionClamp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ClampMode = CMODE_Clamp;
	MinDefault = 0.0f;
	MaxDefault = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

void UMaterialExpressionClamp::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();

	if (UnderlyingArchive.IsLoading() && UnderlyingArchive.UE4Ver() < VER_UE4_RETROFIT_CLAMP_EXPRESSIONS_SWAP)
	{
		if (ClampMode == CMODE_ClampMin)
		{
			ClampMode = CMODE_ClampMax;
		}
		else if (ClampMode == CMODE_ClampMax)
		{
			ClampMode = CMODE_ClampMin;
		}
	}
}

#if WITH_EDITOR
int32 UMaterialExpressionClamp::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Clamp input"));
	}
	else
	{
		const int32 MinIndex = Min.GetTracedInput().Expression ? Min.Compile(Compiler) : Compiler->Constant(MinDefault);
		const int32 MaxIndex = Max.GetTracedInput().Expression ? Max.Compile(Compiler) : Compiler->Constant(MaxDefault);

		if (ClampMode == CMODE_Clamp)
		{
			return Compiler->Clamp(Input.Compile(Compiler), MinIndex, MaxIndex);
		}
		else if (ClampMode == CMODE_ClampMin)
		{
			return Compiler->Max(Input.Compile(Compiler), MinIndex);
		}
		else if (ClampMode == CMODE_ClampMax)
		{
			return Compiler->Min(Input.Compile(Compiler), MaxIndex);
		}
		return INDEX_NONE;
	}
}

void UMaterialExpressionClamp::GetCaption(TArray<FString>& OutCaptions) const
{
	FString	NewCaption = TEXT( "Clamp" );

	if (ClampMode == CMODE_ClampMin || ClampMode == CMODE_Clamp)
	{
		NewCaption += Min.GetTracedInput().Expression ? TEXT(" (Min)") : FString::Printf(TEXT(" (Min=%.4g)"), MinDefault);
	}
	if (ClampMode == CMODE_ClampMax || ClampMode == CMODE_Clamp)
	{
		NewCaption += Max.GetTracedInput().Expression ? TEXT(" (Max)") : FString::Printf(TEXT(" (Max=%.4g)"), MaxDefault);
	}
	OutCaptions.Add(NewCaption);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionSaturate
//
UMaterialExpressionSaturate::UMaterialExpressionSaturate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSaturate::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Saturate input"));
	}
	
	return Compiler->Saturate(Input.Compile(Compiler));
}

void UMaterialExpressionSaturate::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Saturate"));
}

void UMaterialExpressionSaturate::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Clamps the value between 0 and 1. Saturate is free on most modern graphics hardware."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionMin
//

UMaterialExpressionMin::UMaterialExpressionMin(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 0.0f;
	ConstB = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMin::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	return Compiler->Min(Arg1, Arg2);
}

void UMaterialExpressionMin::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Min");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();

	if(!ATraced.Expression || !BTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstB);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionMax
//

UMaterialExpressionMax::UMaterialExpressionMax(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstA = 0.0f;
	ConstB = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMax::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg1 = A.GetTracedInput().Expression ? A.Compile(Compiler) : Compiler->Constant(ConstA);
	// if the input is hooked up, use it, otherwise use the internal constant
	int32 Arg2 = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	return Compiler->Max(Arg1, Arg2);
}

void UMaterialExpressionMax::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Max");

	FExpressionInput ATraced = A.GetTracedInput();
	FExpressionInput BTraced = B.GetTracedInput();

	if(!ATraced.Expression || !BTraced.Expression)
	{
		ret += TEXT("(");
		ret += ATraced.Expression ? TEXT(",") : FString::Printf( TEXT("%.4g,"), ConstA);
		ret += BTraced.Expression ? TEXT(")") : FString::Printf( TEXT("%.4g)"), ConstB);
	}

	OutCaptions.Add(ret);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionTextureCoordinate
//

UMaterialExpressionTextureCoordinate::UMaterialExpressionTextureCoordinate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	UTiling = 1.0f;
	VTiling = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTextureCoordinate::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Depending on whether we have U and V scale values that differ, we can perform a multiply by either
	// a scalar or a float2.  These tiling values are baked right into the shader node, so they're always
	// known at compile time.
	if( FMath::Abs( UTiling - VTiling ) > SMALL_NUMBER )
	{
		return Compiler->Mul(Compiler->TextureCoordinate(CoordinateIndex, UnMirrorU, UnMirrorV),Compiler->Constant2(UTiling, VTiling));
	}
	else if(FMath::Abs(1.0f - UTiling) > SMALL_NUMBER)
	{
		return Compiler->Mul(Compiler->TextureCoordinate(CoordinateIndex, UnMirrorU, UnMirrorV),Compiler->Constant(UTiling));
	}
	else
	{
		// Avoid emitting the multiply by 1.0f if possible
		// This should make generated HLSL a bit cleaner, but more importantly will help avoid generating redundant virtual texture stacks
		return Compiler->TextureCoordinate(CoordinateIndex, UnMirrorU, UnMirrorV);
	}
}

void UMaterialExpressionTextureCoordinate::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("TexCoord[%i]"), CoordinateIndex));
}


bool UMaterialExpressionTextureCoordinate::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	TArray<FString> Captions;
	GetCaption(Captions);
	for (const FString& Caption : Captions)
	{
		if (Caption.Contains(SearchQuery))
		{
			return true;
		}
	}

	return Super::MatchesSearchQuery(SearchQuery);
}
#endif // WITH_EDITOR

UMaterialExpressionDotProduct::UMaterialExpressionDotProduct(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
			, NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDotProduct::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing DotProduct input A"));
	}
	else if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing DotProduct input B"));
	}
	else
	{
		int32 Arg1 = A.Compile(Compiler);
		int32 Arg2 = B.Compile(Compiler);
		return Compiler->Dot(
			Arg1,
			Arg2
			);
	}
}

void UMaterialExpressionDotProduct::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Dot"));
}
#endif // WITH_EDITOR

UMaterialExpressionCrossProduct::UMaterialExpressionCrossProduct(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
			, NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCrossProduct::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing CrossProduct input A"));
	}
	else if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing CrossProduct input B"));
	}
	else
	{
		int32 Arg1 = A.Compile(Compiler);
		int32 Arg2 = B.Compile(Compiler);
		return Compiler->Cross(
			Arg1,
			Arg2
			);
	}
}

void UMaterialExpressionCrossProduct::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Cross"));
}
#endif // WITH_EDITOR

UMaterialExpressionComponentMask::UMaterialExpressionComponentMask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
			, NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionComponentMask::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing ComponentMask input"));
	}

	return Compiler->ComponentMask(
		Input.Compile(Compiler),
		R,
		G,
		B,
		A
		);
}

void UMaterialExpressionComponentMask::GetCaption(TArray<FString>& OutCaptions) const
{
	FString Str(TEXT("Mask ("));
	if ( R ) Str += TEXT(" R");
	if ( G ) Str += TEXT(" G");
	if ( B ) Str += TEXT(" B");
	if ( A ) Str += TEXT(" A");
	Str += TEXT(" )");
	OutCaptions.Add(Str);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionStaticComponentMaskParameter
//
UMaterialExpressionStaticComponentMaskParameter::UMaterialExpressionStaticComponentMaskParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionStaticComponentMaskParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing ComponentMaskParameter input"));
	}
	else
	{
		return Compiler->StaticComponentMask(
			Input.Compile(Compiler),
			ParameterName,
			DefaultR,
			DefaultG,
			DefaultB,
			DefaultA
			);
	}
}

void UMaterialExpressionStaticComponentMaskParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Mask Param")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

void UMaterialExpressionStaticComponentMaskParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	bool R = false;
	bool G = false;
	bool B = false;
	bool A = false;
	FGuid Guid;
	Material->GetStaticComponentMaskParameterValue(OtherExpression->GetParameterName(), R, G, B, A, Guid);
	DefaultR = R;
	DefaultG = G;
	DefaultB = B;
	DefaultA = A;
	FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionStaticComponentMaskParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionStaticComponentMaskParameter, DefaultR));
	FPropertyChangedEvent RChangedEvent(ParamProperty);
	PostEditChangeProperty(RChangedEvent);
	ParamProperty = FindFProperty<FProperty>(UMaterialExpressionStaticComponentMaskParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionStaticComponentMaskParameter, DefaultG));
	FPropertyChangedEvent GChangedEvent(ParamProperty);
	PostEditChangeProperty(GChangedEvent);
	ParamProperty = FindFProperty<FProperty>(UMaterialExpressionStaticComponentMaskParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionStaticComponentMaskParameter, DefaultB));
	FPropertyChangedEvent BChangedEvent(ParamProperty);
	PostEditChangeProperty(BChangedEvent);
	ParamProperty = FindFProperty<FProperty>(UMaterialExpressionStaticComponentMaskParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionStaticComponentMaskParameter, DefaultA));
	FPropertyChangedEvent AChangedEvent(ParamProperty);
	PostEditChangeProperty(AChangedEvent);
}
#endif // WITH_EDITOR

bool UMaterialExpressionStaticComponentMaskParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid&OutExpressionGuid) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutR = DefaultR;
		OutG = DefaultG;
		OutB = DefaultB;
		OutA = DefaultA;
		OutExpressionGuid = ExpressionGUID;
		return true;
	}

	return false;
}

#if WITH_EDITOR
bool  UMaterialExpressionStaticComponentMaskParameter::SetParameterValue(FName InParameterName, bool InR, bool InG, bool InB, bool InA, FGuid InExpressionGuid)
{
	if (InParameterName == ParameterName)
	{
		DefaultR = InR;
		DefaultG = InG;
		DefaultB = InB;
		DefaultA = InA;
		ExpressionGUID = InExpressionGuid;
		return true;
	}

	return false;
}
#endif

//
//	UMaterialExpressionTime
//

UMaterialExpressionTime::UMaterialExpressionTime(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif

	Period = 0.0f;
	bOverride_Period = false;
}

#if WITH_EDITOR
int32 UMaterialExpressionTime::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return bIgnorePause ? Compiler->RealTime(bOverride_Period, Period) : Compiler->GameTime(bOverride_Period, Period);
}

void UMaterialExpressionTime::GetCaption(TArray<FString>& OutCaptions) const
{
	if (bOverride_Period)
	{
		if (Period == 0.0f)
		{
			OutCaptions.Add(TEXT("Time (Stopped)"));
		}
		else
		{
			OutCaptions.Add(FString::Printf(TEXT("Time (Period of %.2f)"), Period));
		}
	}
	else
	{
		OutCaptions.Add(TEXT("Time"));
	}
}
#endif // WITH_EDITOR

UMaterialExpressionCameraVectorWS::UMaterialExpressionCameraVectorWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCameraVectorWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->CameraVector();
}

void UMaterialExpressionCameraVectorWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Camera Vector"));
}
#endif // WITH_EDITOR

UMaterialExpressionCameraPositionWS::UMaterialExpressionCameraPositionWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCameraPositionWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ViewProperty(MEVP_WorldSpaceCameraPosition);
}

void UMaterialExpressionCameraPositionWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Camera Position"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionReflectionVectorWS
//

UMaterialExpressionReflectionVectorWS::UMaterialExpressionReflectionVectorWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionReflectionVectorWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result = CustomWorldNormal.Compile(Compiler);
	if (CustomWorldNormal.Expression) 
	{
		// Don't do anything special here in regards to if the Expression is a Reroute node, the compiler will handle properly internally and return INDEX_NONE if rerouted to nowhere.
		return Compiler->ReflectionAboutCustomWorldNormal(Result, bNormalizeCustomWorldNormal); 
	}
	else
	{
		return Compiler->ReflectionVector();
	}
}

void UMaterialExpressionReflectionVectorWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Reflection Vector"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionPanner
//
UMaterialExpressionPanner::UMaterialExpressionPanner(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bCollapsed = true;
#endif
	ConstCoordinate = 0;
}

#if WITH_EDITOR
int32 UMaterialExpressionPanner::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 TimeArg = Time.GetTracedInput().Expression ? Time.Compile(Compiler) : Compiler->GameTime(false, 0.0f);
	bool bIsSpeedExpressionValid = Speed.GetTracedInput().Expression != nullptr;
	int32 SpeedVectorArg = bIsSpeedExpressionValid ? Speed.Compile(Compiler) : INDEX_NONE;
	int32 SpeedXArg = bIsSpeedExpressionValid ? Compiler->ComponentMask(SpeedVectorArg, true, false, false, false) : Compiler->Constant(SpeedX);
	int32 SpeedYArg = bIsSpeedExpressionValid ? Compiler->ComponentMask(SpeedVectorArg, false, true, false, false) : Compiler->Constant(SpeedY);
	int32 Arg1;
	int32 Arg2;
	if (bFractionalPart)
	{
		// Note: this is to avoid (delay) divergent accuracy issues as GameTime increases.
		// TODO: C++ to calculate its phase via per frame time delta.
		Arg1 = Compiler->PeriodicHint(Compiler->Frac(Compiler->Mul(TimeArg, SpeedXArg)));
		Arg2 = Compiler->PeriodicHint(Compiler->Frac(Compiler->Mul(TimeArg, SpeedYArg)));
	}
	else
	{
		Arg1 = Compiler->PeriodicHint(Compiler->Mul(TimeArg, SpeedXArg));
		Arg2 = Compiler->PeriodicHint(Compiler->Mul(TimeArg, SpeedYArg));
	}

	int32 Arg3 = Coordinate.GetTracedInput().Expression ? Coordinate.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false);
	return Compiler->Add(
			Compiler->AppendVector(
				Arg1,
				Arg2
				),
			Arg3
			);
}

void UMaterialExpressionPanner::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Panner"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionRotator
//
UMaterialExpressionRotator::UMaterialExpressionRotator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	CenterX = 0.5f;
	CenterY = 0.5f;
	Speed = 0.25f;
	ConstCoordinate = 0;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionRotator::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32	Cosine = Compiler->Cosine(Compiler->Mul(Time.GetTracedInput().Expression ? Time.Compile(Compiler) : Compiler->GameTime(false, 0.0f),Compiler->Constant(Speed))),
		Sine = Compiler->Sine(Compiler->Mul(Time.GetTracedInput().Expression ? Time.Compile(Compiler) : Compiler->GameTime(false, 0.0f),Compiler->Constant(Speed))),
		RowX = Compiler->AppendVector(Cosine,Compiler->Mul(Compiler->Constant(-1.0f),Sine)),
		RowY = Compiler->AppendVector(Sine,Cosine),
		Origin = Compiler->Constant2(CenterX,CenterY),
		BaseCoordinate = Coordinate.GetTracedInput().Expression ? Coordinate.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false);

	const int32 Arg1 = Compiler->Dot(RowX,Compiler->Sub(Compiler->ComponentMask(BaseCoordinate,1,1,0,0),Origin));
	const int32 Arg2 = Compiler->Dot(RowY,Compiler->Sub(Compiler->ComponentMask(BaseCoordinate,1,1,0,0),Origin));

	if(Compiler->GetType(BaseCoordinate) == MCT_Float3)
		return Compiler->AppendVector(
				Compiler->Add(
					Compiler->AppendVector(
						Arg1,
						Arg2
						),
					Origin
					),
				Compiler->ComponentMask(BaseCoordinate,0,0,1,0)
				);
	else
	{
		const int32 ArgOne = Compiler->Dot(RowX,Compiler->Sub(BaseCoordinate,Origin));
		const int32 ArgTwo = Compiler->Dot(RowY,Compiler->Sub(BaseCoordinate,Origin));

		return Compiler->Add(
				Compiler->AppendVector(
					ArgOne,
					ArgTwo
					),
				Origin
				);
	}
}

void UMaterialExpressionRotator::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Rotator"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionSine
//
UMaterialExpressionSine::UMaterialExpressionSine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Period = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSine::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Sine input"));
	}

	return Compiler->Sine(Period > 0.0f ? Compiler->Mul(Input.Compile(Compiler),Compiler->Constant(2.0f * (float)PI / Period)) : Input.Compile(Compiler));
}

void UMaterialExpressionSine::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Sine"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionCosine
//
UMaterialExpressionCosine::UMaterialExpressionCosine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Period = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCosine::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Cosine input"));
	}

	return Compiler->Cosine(Compiler->Mul(Input.Compile(Compiler),Period > 0.0f ? Compiler->Constant(2.0f * (float)PI / Period) : 0));
}

void UMaterialExpressionCosine::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Cosine"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionTangent
//
UMaterialExpressionTangent::UMaterialExpressionTangent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Period = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTangent::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Tangent input"));
	}

	return Compiler->Tangent(Compiler->Mul(Input.Compile(Compiler),Period > 0.0f ? Compiler->Constant(2.0f * (float)PI / Period) : 0));
}

void UMaterialExpressionTangent::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Tangent"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArcsine
//
UMaterialExpressionArcsine::UMaterialExpressionArcsine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArcsine::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Arcsine input"));
	}

	return Compiler->Arcsine(Input.Compile(Compiler));
}

void UMaterialExpressionArcsine::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Arcsine"));
}

void UMaterialExpressionArcsine::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Inverse sine function. This is an expensive operation not reflected by instruction count."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArcsineFast
//
UMaterialExpressionArcsineFast::UMaterialExpressionArcsineFast(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArcsineFast::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing ArcsineFast input"));
	}

	return Compiler->ArcsineFast(Input.Compile(Compiler));
}

void UMaterialExpressionArcsineFast::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ArcsineFast"));
}

void UMaterialExpressionArcsineFast::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Approximate inverse sine function. Input must be between -1 and 1."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArccosine
//
UMaterialExpressionArccosine::UMaterialExpressionArccosine(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArccosine::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Arccosine input"));
	}

	return Compiler->Arccosine(Input.Compile(Compiler));
}

void UMaterialExpressionArccosine::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Arccosine"));
}

void UMaterialExpressionArccosine::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Inverse cosine function. This is an expensive operation not reflected by instruction count."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArccosineFast
//
UMaterialExpressionArccosineFast::UMaterialExpressionArccosineFast(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArccosineFast::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing ArccosineFast input"));
	}

	return Compiler->ArccosineFast(Input.Compile(Compiler));
}

void UMaterialExpressionArccosineFast::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ArccosineFast"));
}

void UMaterialExpressionArccosineFast::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Approximate inverse cosine function. Input must be between -1 and 1."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArctangent
//
UMaterialExpressionArctangent::UMaterialExpressionArctangent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArctangent::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Arctangent input"));
	}

	return Compiler->Arctangent(Input.Compile(Compiler));
}

void UMaterialExpressionArctangent::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Arctangent"));
}

void UMaterialExpressionArctangent::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Inverse tangent function. This is an expensive operation not reflected by instruction count."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArctangentFast
//
UMaterialExpressionArctangentFast::UMaterialExpressionArctangentFast(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArctangentFast::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing ArctangentFast input"));
	}

	return Compiler->ArctangentFast(Input.Compile(Compiler));
}

void UMaterialExpressionArctangentFast::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ArctangentFast"));
}

void UMaterialExpressionArctangentFast::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Approximate inverse tangent function."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArctangent2
//
UMaterialExpressionArctangent2::UMaterialExpressionArctangent2(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArctangent2::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Y.GetTracedInput().Expression || !X.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Arctangent2 input"));
	}

	int32 YResult = Y.Compile(Compiler);
	int32 XResult = X.Compile(Compiler);
	return Compiler->Arctangent2(YResult, XResult);
}

void UMaterialExpressionArctangent2::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Arctangent2"));
}

void UMaterialExpressionArctangent2::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Inverse tangent of X / Y where input signs are used to determine quadrant. This is an expensive operation not reflected by instruction count."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionArctangent2Fast
//
UMaterialExpressionArctangent2Fast::UMaterialExpressionArctangent2Fast(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionArctangent2Fast::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Y.GetTracedInput().Expression || !X.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Arctangent2Fast input"));
	}

	int32 YResult = Y.Compile(Compiler);
	int32 XResult = X.Compile(Compiler);
	return Compiler->Arctangent2Fast(YResult, XResult);
}

void UMaterialExpressionArctangent2Fast::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Arctangent2Fast"));
}

void UMaterialExpressionArctangent2Fast::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Approximate inverse tangent of X / Y where input signs are used to determine quadrant."), 40, OutToolTip);
}
#endif // WITH_EDITOR

UMaterialExpressionBumpOffset::UMaterialExpressionBumpOffset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	HeightRatio = 0.05f;
	ReferencePlane = 0.5f;
	ConstCoordinate = 0;
#if WITH_EDITORONLY_DATA
	bCollapsed = false;

	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionBumpOffset::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Height.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Height input"));
	}

	return Compiler->Add(
			Compiler->Mul(
				Compiler->ComponentMask(Compiler->TransformVector(MCB_World, MCB_Tangent, Compiler->CameraVector()),1,1,0,0),
				Compiler->Add(
					Compiler->Mul(
						HeightRatioInput.GetTracedInput().Expression ? Compiler->ForceCast(HeightRatioInput.Compile(Compiler),MCT_Float1) : Compiler->Constant(HeightRatio),
						Compiler->ForceCast(Height.Compile(Compiler),MCT_Float1)
						),
					HeightRatioInput.GetTracedInput().Expression ? Compiler->Mul(Compiler->Constant(-ReferencePlane), Compiler->ForceCast(HeightRatioInput.Compile(Compiler),MCT_Float1)) : Compiler->Constant(-ReferencePlane * HeightRatio)
					)
				),
			Coordinate.GetTracedInput().Expression ? Coordinate.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false)
			);
}

void UMaterialExpressionBumpOffset::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("BumpOffset"));
}
#endif // WITH_EDITOR

UMaterialExpressionAppendVector::UMaterialExpressionAppendVector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
			, NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionAppendVector::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing AppendVector input A"));
	}
	else if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing AppendVector input B"));
	}
	else
	{
		int32 Arg1 = A.Compile(Compiler);
		int32 Arg2 = B.Compile(Compiler);
		return Compiler->AppendVector(
			Arg1,
			Arg2
			);
	}
}

void UMaterialExpressionAppendVector::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Append"));
}
#endif // WITH_EDITOR

// -----

UMaterialExpressionMakeMaterialAttributes::UMaterialExpressionMakeMaterialAttributes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialAttributes;
		FConstructorStatics()
			: NAME_MaterialAttributes(LOCTEXT( "MaterialAttributes", "Material Attributes" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialAttributes);
#endif
}

void UMaterialExpressionMakeMaterialAttributes::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();

	UnderlyingArchive.UsingCustomVersion(FRenderingObjectVersion::GUID);
	
	if (UnderlyingArchive.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::FixedLegacyMaterialAttributeNodeTypes)
	{
		// Update the legacy masks else fail on vec3 to vec2 conversion
		Refraction.SetMask(1, 1, 1, 0, 0);
	}
}

#if WITH_EDITOR
int32 UMaterialExpressionMakeMaterialAttributes::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) 
{
	int32 Ret = INDEX_NONE;
	UMaterialExpression* Expression = nullptr;

 	static_assert(MP_MAX == 32, 
		"New material properties should be added to the end of the inputs for this expression. \
		The order of properties here should match the material results pins, the make material attriubtes node inputs and the mapping of IO indices to properties in GetMaterialPropertyFromInputOutputIndex().\
		Insertions into the middle of the properties or a change in the order of properties will also require that existing data is fixed up in DoMaterialAttributeReorder().\
		");

	EMaterialProperty Property = FMaterialAttributeDefinitionMap::GetProperty(Compiler->GetMaterialAttribute());
	// We don't worry about reroute nodes in the switch, as we have a test for their validity afterwards.
	switch (Property)
	{
	case MP_BaseColor: Ret = BaseColor.Compile(Compiler); Expression = BaseColor.Expression; break;
	case MP_Metallic: Ret = Metallic.Compile(Compiler); Expression = Metallic.Expression; break;
	case MP_Specular: Ret = Specular.Compile(Compiler); Expression = Specular.Expression; break;
	case MP_Roughness: Ret = Roughness.Compile(Compiler); Expression = Roughness.Expression; break;
	case MP_Anisotropy: Ret = Anisotropy.Compile(Compiler); Expression = Anisotropy.Expression; break;
	case MP_EmissiveColor: Ret = EmissiveColor.Compile(Compiler); Expression = EmissiveColor.Expression; break;
	case MP_Opacity: Ret = Opacity.Compile(Compiler); Expression = Opacity.Expression; break;
	case MP_OpacityMask: Ret = OpacityMask.Compile(Compiler); Expression = OpacityMask.Expression; break;
	case MP_Normal: Ret = Normal.Compile(Compiler); Expression = Normal.Expression; break;
	case MP_Tangent: Ret = Tangent.Compile(Compiler); Expression = Tangent.Expression; break;
	case MP_WorldPositionOffset: Ret = WorldPositionOffset.Compile(Compiler); Expression = WorldPositionOffset.Expression; break;
	case MP_WorldDisplacement: Ret = WorldDisplacement.Compile(Compiler); Expression = WorldDisplacement.Expression; break;
	case MP_TessellationMultiplier: Ret = TessellationMultiplier.Compile(Compiler); Expression = TessellationMultiplier.Expression; break;
	case MP_SubsurfaceColor: Ret = SubsurfaceColor.Compile(Compiler); Expression = SubsurfaceColor.Expression; break;
	case MP_CustomData0: Ret = ClearCoat.Compile(Compiler); Expression = ClearCoat.Expression; break;
	case MP_CustomData1: Ret = ClearCoatRoughness.Compile(Compiler); Expression = ClearCoatRoughness.Expression; break;
	case MP_AmbientOcclusion: Ret = AmbientOcclusion.Compile(Compiler); Expression = AmbientOcclusion.Expression; break;
	case MP_Refraction: Ret = Refraction.Compile(Compiler); Expression = Refraction.Expression; break;
	case MP_PixelDepthOffset: Ret = PixelDepthOffset.Compile(Compiler); Expression = PixelDepthOffset.Expression; break;
	case MP_ShadingModel: Ret = ShadingModel.Compile(Compiler); Expression = ShadingModel.Expression; break;
	};

	if (Property >= MP_CustomizedUVs0 && Property <= MP_CustomizedUVs7)
	{
		Ret = CustomizedUVs[Property - MP_CustomizedUVs0].Compile(Compiler); Expression = CustomizedUVs[Property - MP_CustomizedUVs0].Expression;
	}

	//If we've connected an expression but its still returned INDEX_NONE, flag the error. This also catches reroute nodes to nowhere.
	if (Expression && INDEX_NONE == Ret)
	{
		Compiler->Errorf(TEXT("Error on property %s"), *FMaterialAttributeDefinitionMap::GetAttributeName(Property));
	}

	return Ret;
}

void UMaterialExpressionMakeMaterialAttributes::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("MakeMaterialAttributes"));
}

uint32 UMaterialExpressionMakeMaterialAttributes::GetInputType(int32 InputIndex)
{
	if (GetInputName(InputIndex).IsEqual("ShadingModel"))
	{
		return MCT_ShadingModel;
	}
	else
	{
		return UMaterialExpression::GetInputType(InputIndex);
	}
}
#endif // WITH_EDITOR

// -----

UMaterialExpressionBreakMaterialAttributes::UMaterialExpressionBreakMaterialAttributes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialAttributes;
		FConstructorStatics()
			: NAME_MaterialAttributes(LOCTEXT( "MaterialAttributes", "Material Attributes" ))
		{
		}
	};

	static FConstructorStatics ConstructorStatics;

	bShowOutputNameOnPin = true;
	bShowMaskColorsOnPin = false;

	MenuCategories.Add(ConstructorStatics.NAME_MaterialAttributes);
	
 	static_assert(MP_MAX == 32, 
		"New material properties should be added to the end of the outputs for this expression. \
		The order of properties here should match the material results pins, the make material attriubtes node inputs and the mapping of IO indices to properties in GetMaterialPropertyFromInputOutputIndex().\
		Insertions into the middle of the properties or a change in the order of properties will also require that existing data is fixed up in DoMaterialAttriubtesReorder().\
		");

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("BaseColor"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Metallic"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Specular"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Roughness"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Anisotropy"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("EmissiveColor"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Opacity"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("OpacityMask"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Normal"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Tangent"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("WorldPositionOffset"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("WorldDisplacement"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("TessellationMultiplier"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("SubsurfaceColor"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("ClearCoat"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("ClearCoatRoughness"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("AmbientOcclusion"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Refraction"), 1, 1, 1, 0, 0));

	for (int32 UVIndex = 0; UVIndex <= MP_CustomizedUVs7 - MP_CustomizedUVs0; UVIndex++)
	{
		Outputs.Add(FExpressionOutput(*FString::Printf(TEXT("CustomizedUV%u"), UVIndex), 1, 1, 1, 0, 0));
	}

	Outputs.Add(FExpressionOutput(TEXT("PixelDepthOffset"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("ShadingModel"), 0, 0, 0, 0, 0));
#endif
}

void UMaterialExpressionBreakMaterialAttributes::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();

	UnderlyingArchive.UsingCustomVersion(FRenderingObjectVersion::GUID);

#if WITH_EDITOR
	if (UnderlyingArchive.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::FixedLegacyMaterialAttributeNodeTypes)
	{
		// Update the masks for legacy content
		int32 OutputIndex = 0;

		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // BaseColor
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // Metallic
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // Specular
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // Roughness
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // Anisotropy
		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // EmissiveColor
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // Opacity
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // OpacityMask
		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // Normal
		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // Tangent
		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // WorldPositionOffset
		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // WorldDisplacement
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // TessellationMultiplier
		Outputs[OutputIndex].SetMask(1, 1, 1, 1, 0); ++OutputIndex; // SubsurfaceColor
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // ClearCoat
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // ClearCoatRoughness 
		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex; // AmbientOcclusion
		Outputs[OutputIndex].SetMask(1, 1, 1, 0, 0); ++OutputIndex; // Refraction
		
		for (int32 i = 0; i <= MP_CustomizedUVs7 - MP_CustomizedUVs0; ++i, ++OutputIndex)
		{
			Outputs[OutputIndex].SetMask(1, 1, 1, 0, 0);
		}

		Outputs[OutputIndex].SetMask(1, 1, 0, 0, 0); ++OutputIndex;// PixelDepthOffset
		Outputs[OutputIndex].SetMask(0, 0, 0, 0, 0); // ShadingModelFromMaterialExpression
	}
#endif // WITH_EDITOR
}

#if WITH_EDITOR
static TMap<EMaterialProperty, int32> PropertyToIOIndexMap;

static void BuildPropertyToIOIndexMap()
{
	if (PropertyToIOIndexMap.Num() == 0)
	{
		PropertyToIOIndexMap.Add(MP_BaseColor,				0);
		PropertyToIOIndexMap.Add(MP_Metallic,				1);
		PropertyToIOIndexMap.Add(MP_Specular,				2);
		PropertyToIOIndexMap.Add(MP_Roughness,				3);
		PropertyToIOIndexMap.Add(MP_Anisotropy,				4);
		PropertyToIOIndexMap.Add(MP_EmissiveColor,			5);
		PropertyToIOIndexMap.Add(MP_Opacity,				6);
		PropertyToIOIndexMap.Add(MP_OpacityMask,			7);
		PropertyToIOIndexMap.Add(MP_Normal,					8);
		PropertyToIOIndexMap.Add(MP_Tangent,				9);
		PropertyToIOIndexMap.Add(MP_WorldPositionOffset,	10);
		PropertyToIOIndexMap.Add(MP_WorldDisplacement,		11);
		PropertyToIOIndexMap.Add(MP_TessellationMultiplier, 12);
		PropertyToIOIndexMap.Add(MP_SubsurfaceColor,		13);
		PropertyToIOIndexMap.Add(MP_CustomData0,			14);
		PropertyToIOIndexMap.Add(MP_CustomData1,			15);
		PropertyToIOIndexMap.Add(MP_AmbientOcclusion,		16);
		PropertyToIOIndexMap.Add(MP_Refraction,				17);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs0,			18);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs1,			19);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs2,			20);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs3,			21);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs4,			22);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs5,			23);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs6,			24);
		PropertyToIOIndexMap.Add(MP_CustomizedUVs7,			25);
		PropertyToIOIndexMap.Add(MP_PixelDepthOffset,		26);
		PropertyToIOIndexMap.Add(MP_ShadingModel,			27);
	}
}

int32 UMaterialExpressionBreakMaterialAttributes::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	BuildPropertyToIOIndexMap();

	// Here we don't care about any multiplex index coming in.
	// We pass through our output index as the multiplex index so the MakeMaterialAttriubtes node at the other end can send us the right data.
	const EMaterialProperty* Property = PropertyToIOIndexMap.FindKey(OutputIndex);

	if (!Property)
	{
		return Compiler->Errorf(TEXT("Tried to compile material attributes?"));
	}
	else
	{
		return MaterialAttributes.CompileWithDefault(Compiler, FMaterialAttributeDefinitionMap::GetID(*Property));
	}
}

void UMaterialExpressionBreakMaterialAttributes::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("BreakMaterialAttributes"));
}

const TArray<FExpressionInput*> UMaterialExpressionBreakMaterialAttributes::GetInputs()
{
	TArray<FExpressionInput*> Result;
	Result.Add(&MaterialAttributes);
	return Result;
}


FExpressionInput* UMaterialExpressionBreakMaterialAttributes::GetInput(int32 InputIndex)
{
	if( 0 == InputIndex )
	{
		return &MaterialAttributes;
	}

	return nullptr;
}

FName UMaterialExpressionBreakMaterialAttributes::GetInputName(int32 InputIndex) const
{
	if( 0 == InputIndex )
	{
		return *NSLOCTEXT("BreakMaterialAttributes", "InputName", "Attr").ToString();
	}
	return NAME_None;
}

bool UMaterialExpressionBreakMaterialAttributes::IsInputConnectionRequired(int32 InputIndex) const
{
	return true;
}

uint32 UMaterialExpressionBreakMaterialAttributes::GetOutputType(int32 OutputIndex)
{
	BuildPropertyToIOIndexMap();

	const EMaterialProperty* Property = PropertyToIOIndexMap.FindKey(OutputIndex);

	if (Property && *Property == EMaterialProperty::MP_ShadingModel)
	{
		return MCT_ShadingModel;
	}
	else
	{
		return UMaterialExpression::GetOutputType(OutputIndex);
	}
}
#endif // WITH_EDITOR

// -----

UMaterialExpressionGetMaterialAttributes::UMaterialExpressionGetMaterialAttributes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialAttributes;
		FConstructorStatics()
			: NAME_MaterialAttributes(LOCTEXT( "MaterialAttributes", "Material Attributes" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialAttributes);

	bShowOutputNameOnPin = true;
#endif

#if WITH_EDITOR
	// Add default output pins
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("MaterialAttributes"), 0, 0, 0, 0, 0));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionGetMaterialAttributes::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Verify setup
	const int32 NumOutputPins = AttributeGetTypes.Num();
	for (int32 i = 0; i < NumOutputPins; ++i)
	{
		for (int j = i + 1; j < NumOutputPins; ++j)
		{
			if (AttributeGetTypes[i] == AttributeGetTypes[j])
			{
				return Compiler->Errorf(TEXT("Duplicate attribute types."));
			}
		}

		if (FMaterialAttributeDefinitionMap::GetProperty(AttributeGetTypes[i]) == MP_MAX)
		{
			return Compiler->Errorf(TEXT("Property type doesn't exist, needs re-mapping?"));
		}
	}

	// Compile attribute
	int32 Result = INDEX_NONE;

	if (OutputIndex == 0)
	{
		const FGuid AttributeID = Compiler->GetMaterialAttribute();
		Result = MaterialAttributes.CompileWithDefault(Compiler, AttributeID);
	}
	else if (OutputIndex > 0)
	{
		checkf(OutputIndex <= AttributeGetTypes.Num(), TEXT("Requested non-existent pin."));
		Result = MaterialAttributes.CompileWithDefault(Compiler, AttributeGetTypes[OutputIndex-1]);
	}

	return Result;
}

void UMaterialExpressionGetMaterialAttributes::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("GetMaterialAttributes"));
}

const TArray<FExpressionInput*> UMaterialExpressionGetMaterialAttributes::GetInputs()
{
	TArray<FExpressionInput*> Result;
	Result.Add(&MaterialAttributes);
	return Result;
}

FExpressionInput* UMaterialExpressionGetMaterialAttributes::GetInput(int32 InputIndex)
{
	if (InputIndex == 0)
	{
		return &MaterialAttributes;
	}

	return nullptr;
}

FName UMaterialExpressionGetMaterialAttributes::GetInputName(int32 InputIndex) const
{
	return NAME_None;
}

uint32 UMaterialExpressionGetMaterialAttributes::GetOutputType(int32 OutputIndex)
{
	// Call base class impl to get the type
	uint32 OutputType = Super::GetOutputType(OutputIndex);

	// Override the type if it's a ShadingModel type
	if (OutputIndex > 0) // "0th" place is the mandatory MaterialAttribute itself, skip it
	{
		ensure(OutputIndex < AttributeGetTypes.Num() + 1);
		EMaterialValueType PinType = FMaterialAttributeDefinitionMap::GetValueType(AttributeGetTypes[OutputIndex - 1]);
		if (PinType == MCT_ShadingModel)
		{
			OutputType = PinType;
		}
	}

	return OutputType;
}

void UMaterialExpressionGetMaterialAttributes::PreEditChange(FProperty* PropertyAboutToChange)
{
	// Backup attribute array so we can re-connect pins
	PreEditAttributeGetTypes.Empty();
	for (const FGuid& AttributeID : AttributeGetTypes)
	{
		PreEditAttributeGetTypes.Add(AttributeID);
	};

	Super::PreEditChange(PropertyAboutToChange);
}

void UMaterialExpressionGetMaterialAttributes::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty && GraphNode)
	{
		if (PreEditAttributeGetTypes.Num() < AttributeGetTypes.Num())
		{
			// Attribute type added so default out type
			AttributeGetTypes.Last() = FMaterialAttributeDefinitionMap::GetDefaultID();

			// Attempt to find a valid attribute that's not already listed
			const TArray<FGuid>& OrderedVisibleAttributes = FMaterialAttributeDefinitionMap::GetOrderedVisibleAttributeList();
			for (const FGuid& AttributeID : OrderedVisibleAttributes)
			{
				if (PreEditAttributeGetTypes.Find(AttributeID) == INDEX_NONE)
				{
					 AttributeGetTypes.Last() = AttributeID;
					 break;
				}
			}
		
			// Copy final defaults to new output
			FString AttributeName = FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(AttributeGetTypes.Last(), Material).ToString();
			Outputs.Add(FExpressionOutput(*AttributeName, 0, 0, 0, 0, 0));

			GraphNode->ReconstructNode();
		}	 
		else if (PreEditAttributeGetTypes.Num() > AttributeGetTypes.Num())
		{
			if (AttributeGetTypes.Num() == 0)
			{
				// All attribute types removed
				while (Outputs.Num() > 1)
				{
					Outputs.Pop();
					GraphNode->RemovePinAt(Outputs.Num(), EGPD_Output);
				}
			}
			else
			{
				// Attribute type removed
				int32 RemovedInputIndex = INDEX_NONE;

				for (int32 Attribute = 0; Attribute < AttributeGetTypes.Num(); ++Attribute)
				{
					// A mismatched attribute type means a middle pin was removed
					if (AttributeGetTypes[Attribute] != PreEditAttributeGetTypes[Attribute])
					{
						RemovedInputIndex = Attribute + 1;
						Outputs.RemoveAt(RemovedInputIndex);
						break;
					}
				};

				if (RemovedInputIndex == INDEX_NONE)
				{
					Outputs.Pop();
					RemovedInputIndex = Outputs.Num();
				}

				GraphNode->RemovePinAt(RemovedInputIndex, EGPD_Output);
			}
		}
		else
		{
			// Type changed, update pin names
			for (int i = 1; i < Outputs.Num(); ++i)
			{
				Outputs[i].OutputName = *FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(AttributeGetTypes[i-1], Material).ToString();
			}

			GraphNode->ReconstructNode();
		}
	}

	Super::PostEditChangeProperty( PropertyChangedEvent );
}

void UMaterialExpressionGetMaterialAttributes::PostLoad()
{
	Super::PostLoad();

	// Verify serialized attributes
	check(Outputs.Num() == AttributeGetTypes.Num() + 1);

	// Make sure all outputs have up to date display names
	for (int i = 1; i < Outputs.Num(); ++i)
	{
		const FString DisplayName = FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(AttributeGetTypes[i - 1], Material).ToString();
		Outputs[i].OutputName = *DisplayName;
	}
}
#endif // WITH_EDITOR

// -----

UMaterialExpressionSetMaterialAttributes::UMaterialExpressionSetMaterialAttributes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialAttributes;
		FConstructorStatics()
			: NAME_MaterialAttributes(LOCTEXT( "MaterialAttributes", "Material Attributes" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialAttributes);
#endif

#if WITH_EDITOR
	// Add default input pins
	Inputs.Reset();
	Inputs.Add(FMaterialAttributesInput());
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSetMaterialAttributes::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) 
{
	// Verify setup
	const int32 NumInputPins = AttributeSetTypes.Num();
	for (int32 i = 0; i < NumInputPins; ++i)
	{
		for (int j = i + 1; j < NumInputPins; ++j)
		{
			if (AttributeSetTypes[i] == AttributeSetTypes[j])
			{
				return Compiler->Errorf(TEXT("Duplicate attribute types."));
			}
		}

		if (FMaterialAttributeDefinitionMap::GetProperty(AttributeSetTypes[i]) == MP_MAX)
		{
			return Compiler->Errorf(TEXT("Property type doesn't exist, needs re-mapping?"));
		}
	}

	// Compile attribute
	const FGuid AttributeID = Compiler->GetMaterialAttribute();
	FExpressionInput* AttributeInput = nullptr;

	int32 PinIndex;
	if (AttributeSetTypes.Find(AttributeID, PinIndex))
	{
		checkf(PinIndex + 1 < Inputs.Num(), TEXT("Requested non-existent pin."));
		AttributeInput = &Inputs[PinIndex + 1];
	}

	if (AttributeInput && AttributeInput->GetTracedInput().Expression)
	{
		EMaterialValueType ValueType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
		return Compiler->ValidCast(AttributeInput->GetTracedInput().Compile(Compiler), ValueType);
	}
	else if (Inputs[0].GetTracedInput().Expression)
	{
		return Inputs[0].GetTracedInput().Compile(Compiler);
	}

	return FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, AttributeID);
}

void UMaterialExpressionSetMaterialAttributes::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SetMaterialAttributes"));
}

const TArray<FExpressionInput*> UMaterialExpressionSetMaterialAttributes::GetInputs()
{
	TArray<FExpressionInput*> Result;
	for (FExpressionInput& Input : Inputs)
	{
		Result.Add(&Input);
	}
	return Result;
}

FExpressionInput* UMaterialExpressionSetMaterialAttributes::GetInput(int32 InputIndex)
{
	return &Inputs[InputIndex];
}

FName UMaterialExpressionSetMaterialAttributes::GetInputName(int32 InputIndex) const
{
	FName Name;

	if (InputIndex == 0)
	{
		Name = *NSLOCTEXT("SetMaterialAttributes", "InputName", "MaterialAttributes").ToString();
	}
	else if (InputIndex > 0)
	{
		Name = *FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(AttributeSetTypes[InputIndex-1], Material).ToString();
	}

	return Name;
}

uint32 UMaterialExpressionSetMaterialAttributes::GetInputType(int32 InputIndex)
{
	uint32 InputType = MCT_Unknown;

	if (InputIndex == 0)
	{
		InputType = MCT_MaterialAttributes;
	}
	else
	{
		ensure(InputIndex > 0 && InputIndex < AttributeSetTypes.Num() + 1);
		InputType = FMaterialAttributeDefinitionMap::GetValueType(AttributeSetTypes[InputIndex - 1]);
		if (InputType == MCT_ShadingModel)
		{
			InputType = MCT_ShadingModel;
		}
		else
		{
			InputType = MCT_Float3;
		}
	}

	return InputType;
}

void UMaterialExpressionSetMaterialAttributes::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Allows assigning values to specific inputs on a material attributes pin. Any unconnected inputs will be unchanged."), 40, OutToolTip);
}

void UMaterialExpressionSetMaterialAttributes::PreEditChange(FProperty* PropertyAboutToChange)
{
	// Backup attribute array so we can re-connect pins
	PreEditAttributeSetTypes.Empty();
	for (const FGuid& AttributeID : AttributeSetTypes)
	{
		PreEditAttributeSetTypes.Add(AttributeID);
	};

	Super::PreEditChange(PropertyAboutToChange);
}

void UMaterialExpressionSetMaterialAttributes::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.MemberProperty && GraphNode)
	{
		if (PreEditAttributeSetTypes.Num() < AttributeSetTypes.Num())
		{
			// Attribute type added so default out type
			AttributeSetTypes.Last() = FMaterialAttributeDefinitionMap::GetDefaultID();

			// Attempt to find a valid attribute that's not already listed
			const TArray<FGuid>& OrderedVisibleAttributes = FMaterialAttributeDefinitionMap::GetOrderedVisibleAttributeList();
			for (const FGuid& AttributeID : OrderedVisibleAttributes)
			{
				if (PreEditAttributeSetTypes.Find(AttributeID) == INDEX_NONE)
				{
					 AttributeSetTypes.Last() = AttributeID;
					 break;
				}
			}
		
			// Copy final defaults to new input
			Inputs.Add(FExpressionInput());
			Inputs.Last().InputName = FName(*FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(AttributeSetTypes.Last(), Material).ToString());
			GraphNode->ReconstructNode();
		}	 
		else if (PreEditAttributeSetTypes.Num() > AttributeSetTypes.Num())
		{
			if (AttributeSetTypes.Num() == 0)
			{
				// All attribute types removed
				while (Inputs.Num() > 1)
				{
					Inputs.Pop();
					GraphNode->RemovePinAt(Inputs.Num(), EGPD_Input);
				}
			}
			else
			{
				// Attribute type removed
				int32 RemovedInputIndex = INDEX_NONE;

				for (int32 Attribute = 0; Attribute < AttributeSetTypes.Num(); ++Attribute)
				{
					// A mismatched attribute type means a middle pin was removed
					if (AttributeSetTypes[Attribute] != PreEditAttributeSetTypes[Attribute])
					{
						RemovedInputIndex = Attribute + 1;
						Inputs.RemoveAt(RemovedInputIndex);
						break;
					}
				};

				if (RemovedInputIndex == INDEX_NONE)
				{
					Inputs.Pop();
					RemovedInputIndex = Inputs.Num();
				}

				GraphNode->RemovePinAt(RemovedInputIndex, EGPD_Input);
			}
		}
		else
		{
			// Type changed, update pin names
			for (int i = 1; i < Inputs.Num(); ++i)
			{
				Inputs[i].InputName = FName(*FMaterialAttributeDefinitionMap::GetDisplayNameForMaterial(AttributeSetTypes[i - 1], Material).ToString());
			}
			GraphNode->ReconstructNode();
		}
	}

	Super::PostEditChangeProperty( PropertyChangedEvent );
}
#endif // WITH_EDITOR

// -----

UMaterialExpressionBlendMaterialAttributes::UMaterialExpressionBlendMaterialAttributes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PixelAttributeBlendType(EMaterialAttributeBlend::Blend)
	, VertexAttributeBlendType(EMaterialAttributeBlend::Blend)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialAttributes;
		FConstructorStatics()
			: NAME_MaterialAttributes(LOCTEXT( "MaterialAttributes", "Material Attributes" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialAttributes);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 0, 0, 0, 0, 0));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionBlendMaterialAttributes::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const FGuid AttributeID = Compiler->GetMaterialAttribute();

	// Blending is optional, can skip on a per-node basis
	EMaterialAttributeBlend::Type BlendType;
	EShaderFrequency AttributeFrequency = FMaterialAttributeDefinitionMap::GetShaderFrequency(AttributeID);

	switch (AttributeFrequency)
	{
	case SF_Vertex:	BlendType = VertexAttributeBlendType;	break;
	case SF_Hull:	BlendType = VertexAttributeBlendType;	break;
	case SF_Domain:	BlendType = VertexAttributeBlendType;	break;
	case SF_Pixel:	BlendType = PixelAttributeBlendType;	break;
	default:
		return Compiler->Errorf(TEXT("Attribute blending for shader frequency %i not implemented."), AttributeFrequency);
	}

	switch (BlendType)
	{
	case EMaterialAttributeBlend::UseA: return A.CompileWithDefault(Compiler, AttributeID);
	case EMaterialAttributeBlend::UseB: return B.CompileWithDefault(Compiler, AttributeID);
	default:
		check(BlendType == EMaterialAttributeBlend::Blend);
	}

	// Allow custom blends or fallback to standard interpolation
	int32 ResultA = A.CompileWithDefault(Compiler, AttributeID);
	int32 ResultB = B.CompileWithDefault(Compiler, AttributeID);
	int32 ResultAlpha = Alpha.Compile(Compiler);

	MaterialAttributeBlendFunction BlendFunction = FMaterialAttributeDefinitionMap::GetBlendFunction(AttributeID);
	if (BlendFunction)
	{
		return BlendFunction(Compiler, ResultA, ResultB, ResultAlpha);
	}
	else
	{
		return Compiler->Lerp(ResultA, ResultB, ResultAlpha);
	}
}

void UMaterialExpressionBlendMaterialAttributes::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("BlendMaterialAttributes"));
}

const TArray<FExpressionInput*> UMaterialExpressionBlendMaterialAttributes::GetInputs()
{
	TArray<FExpressionInput*> Result;
	Result.Add(&A);
	Result.Add(&B);
	Result.Add(&Alpha);
	return Result;
}

FExpressionInput* UMaterialExpressionBlendMaterialAttributes::GetInput(int32 InputIndex)
{
	if (InputIndex == 0)
	{
		return &A;
	}
	else if (InputIndex == 1)
	{
		return &B;
	}
	else if (InputIndex == 2)
	{
		return &Alpha;
	}

	return nullptr;
}

FName UMaterialExpressionBlendMaterialAttributes::GetInputName(int32 InputIndex) const
{
	FName Name;

	switch (InputIndex)
	{
	case 0: Name = TEXT("A"); break;
	case 1: Name = TEXT("B"); break;
	case 2: Name = TEXT("Alpha"); break;
	};

	return Name;
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionMaterialAttributeLayers
//
UMaterialExpressionMaterialAttributeLayers::UMaterialExpressionMaterialAttributeLayers(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)	
	, NumActiveLayerCallers(0)
	, NumActiveBlendCallers(0)
	, bIsLayerGraphBuilt(false)
	, ParamLayers(nullptr)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialAttributes;
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_MaterialAttributes(LOCTEXT( "MaterialAttributes", "Material Attributes" ))
			, NAME_Parameters(LOCTEXT("Parameters", "Parameters"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bIsParameterExpression = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialAttributes);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

void UMaterialExpressionMaterialAttributeLayers::PostLoad()
{
	Super::PostLoad();

	for (UMaterialFunctionInterface* Layer : DefaultLayers.Layers)
	{
		if (Layer)
		{
			Layer->ConditionalPostLoad();
		}
	}

	for (UMaterialFunctionInterface* Blend : DefaultLayers.Blends)
	{
		if (Blend)
		{
			Blend->ConditionalPostLoad();
		}
	}

#if WITH_EDITORONLY_DATA
	RebuildLayerGraph(false);
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UMaterialExpressionMaterialAttributeLayers::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildLayerGraph(false);
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void UMaterialExpressionMaterialAttributeLayers::RebuildLayerGraph(bool bReportErrors)
{
	const TArray<UMaterialFunctionInterface*>& Layers = GetLayers();
	const TArray<UMaterialFunctionInterface*>& Blends = GetBlends();
	const TArray<bool>& LayerStates = GetLayerStates();
	
	// Pre-populate callers, we maintain these transient objects to avoid
	// heavy UObject recreation as the graphs are frequently rebuilt
	while (LayerCallers.Num() < Layers.Num())
	{
		LayerCallers.Add(NewObject<UMaterialExpressionMaterialFunctionCall>(GetTransientPackage()));
	}
	while (BlendCallers.Num() < Blends.Num())
	{
		BlendCallers.Add(NewObject<UMaterialExpressionMaterialFunctionCall>(GetTransientPackage()));
	}

	// Reset graph connectivity
	bIsLayerGraphBuilt = false;
	NumActiveLayerCallers = 0;
	NumActiveBlendCallers = 0;

	if (ValidateLayerConfiguration(nullptr, bReportErrors))
	{
		// Initialize layer function callers
		for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
		{
			if (Layers[LayerIndex] && LayerStates[LayerIndex])
			{
				int32 CallerIndex = NumActiveLayerCallers;
				LayerCallers[CallerIndex]->MaterialFunction = Layers[LayerIndex];
				LayerCallers[CallerIndex]->FunctionParameterInfo.Association = EMaterialParameterAssociation::LayerParameter;
				LayerCallers[CallerIndex]->FunctionParameterInfo.Index = LayerIndex;
				++NumActiveLayerCallers;

#if WITH_EDITOR
				Layers[LayerIndex]->GetInputsAndOutputs(LayerCallers[CallerIndex]->FunctionInputs, LayerCallers[CallerIndex]->FunctionOutputs);
				for (FFunctionExpressionOutput& FunctionOutput : LayerCallers[CallerIndex]->FunctionOutputs)
				{
					LayerCallers[CallerIndex]->Outputs.Add(FunctionOutput.Output);
				}

				// Optional: Single material attributes input, the base input to the stack
				if (LayerCallers[CallerIndex]->FunctionInputs.Num() > 0)
				{
					if (Input.GetTracedInput().Expression)
					{
						LayerCallers[CallerIndex]->FunctionInputs[0].Input = Input;
					}
				}

				// Recursively run through internal functions to allow connection of inputs/outputs
				LayerCallers[CallerIndex]->UpdateFromFunctionResource();
#endif
			}
		}

		for (int32 BlendIndex = 0; BlendIndex < Blends.Num(); ++BlendIndex)
		{
			const int32 LayerIndex = BlendIndex + 1;
			if (Layers[LayerIndex] && LayerStates[LayerIndex])
			{
				int32 CallerIndex = NumActiveBlendCallers;
				++NumActiveBlendCallers;

				if (Blends[BlendIndex])
				{
					BlendCallers[CallerIndex]->MaterialFunction = Blends[BlendIndex];
					BlendCallers[CallerIndex]->FunctionParameterInfo.Association = EMaterialParameterAssociation::BlendParameter;
					BlendCallers[CallerIndex]->FunctionParameterInfo.Index = BlendIndex;

#if WITH_EDITOR
					Blends[BlendIndex]->GetInputsAndOutputs(BlendCallers[CallerIndex]->FunctionInputs, BlendCallers[CallerIndex]->FunctionOutputs);
					for (FFunctionExpressionOutput& FunctionOutput : BlendCallers[CallerIndex]->FunctionOutputs)
					{
						BlendCallers[CallerIndex]->Outputs.Add(FunctionOutput.Output);
					}

					// Recursively run through internal functions to allow connection of inputs/ouputs
					BlendCallers[CallerIndex]->UpdateFromFunctionResource();
#endif
				}
				else
				{
					// Empty entries for opaque layers
					BlendCallers[CallerIndex]->MaterialFunction = nullptr;
				}
			}
		}

		// Empty out unused callers
		for (int32 CallerIndex = NumActiveLayerCallers; CallerIndex < LayerCallers.Num(); ++CallerIndex)
		{
			LayerCallers[CallerIndex]->MaterialFunction = nullptr;
		}

		for (int32 CallerIndex = NumActiveBlendCallers; CallerIndex < BlendCallers.Num(); ++CallerIndex)
		{
			BlendCallers[CallerIndex]->MaterialFunction = nullptr;
		}

#if WITH_EDITOR
		// Assemble function chain so each layer blends with the previous
		if (NumActiveLayerCallers >= 2 && NumActiveBlendCallers >= 1)
		{
			if (BlendCallers[0]->MaterialFunction)
			{
				BlendCallers[0]->FunctionInputs[0].Input.Connect(0, LayerCallers[0]);
				BlendCallers[0]->FunctionInputs[1].Input.Connect(0, LayerCallers[1]);
			}

			for (int32 LayerIndex = 2; LayerIndex < NumActiveLayerCallers; ++LayerIndex)
			{
				if (BlendCallers[LayerIndex - 1]->MaterialFunction)
				{
					// Active blend input is previous blend or direct layer if previous is opaque
					UMaterialExpressionMaterialFunctionCall* BlendInput = BlendCallers[LayerIndex - 2];
					BlendInput = BlendInput->MaterialFunction ? BlendInput : LayerCallers[LayerIndex - 1];

					BlendCallers[LayerIndex - 1]->FunctionInputs[0].Input.Connect(0, BlendInput);
					BlendCallers[LayerIndex - 1]->FunctionInputs[1].Input.Connect(0, LayerCallers[LayerIndex]);
				}
			}
		}
#endif

		bIsLayerGraphBuilt = true;
	}
	else if (bReportErrors)
	{
		UE_LOG(LogMaterial, Warning, TEXT("Failed to build layer graph for %s."), Material ? *(Material->GetName()) : TEXT("Unknown"));
	}
}

void UMaterialExpressionMaterialAttributeLayers::OverrideLayerGraph(const FMaterialLayersFunctions* OverrideLayers)
{
	if (ParamLayers != OverrideLayers)
	{
		ParamLayers = OverrideLayers;
		RebuildLayerGraph(false);
	}
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
bool UMaterialExpressionMaterialAttributeLayers::ValidateLayerConfiguration(FMaterialCompiler* Compiler, bool bReportErrors)
{
#define COMPILER_OR_LOG_ERROR(Format, ...)								\
	if (bReportErrors)													\
	{																	\
		if (Compiler) { Compiler->Errorf(Format, ##__VA_ARGS__); }		\
		else { UE_LOG(LogMaterial, Warning, Format, ##__VA_ARGS__); }	\
	}

	const TArray<UMaterialFunctionInterface*>& Layers = GetLayers();
	const TArray<UMaterialFunctionInterface*>& Blends = GetBlends();
	const TArray<bool>& LayerStates = GetLayerStates();

	bool bIsValid = true;
	const int32 NumLayers = Layers.Num();
	const int32 NumBlends = Blends.Num();

	int32 NumActiveLayers = 0;
	int32 NumActiveBlends = 0;

	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		UMaterialFunctionInterface* Layer = Layers[LayerIndex];
		
		if (Layer)
		{
			if (Layer->GetMaterialFunctionUsage() != EMaterialFunctionUsage::MaterialLayer)
			{
				COMPILER_OR_LOG_ERROR(TEXT("Layer %i, %s, not set for layer usage."), LayerIndex, *Layer->GetName());
				bIsValid = false;
			}
			else if (UMaterialFunctionInstance* InstanceLayer = Cast<UMaterialFunctionInstance>(Layer))
			{
				if (!InstanceLayer->Parent)
				{
					COMPILER_OR_LOG_ERROR(TEXT("Layer %i, %s, layer instance has no parent set."), LayerIndex, *Layer->GetName());
					bIsValid = false;
				}
			}
			else
			{
				TArray<UMaterialExpressionFunctionInput*> InputExpressions;
				Layer->GetAllExpressionsOfType<UMaterialExpressionFunctionInput>(InputExpressions, false);
				if (InputExpressions.Num() > 1)
				{
					COMPILER_OR_LOG_ERROR(TEXT("Layer %i, %s, must have one MaterialAttributes input only."), LayerIndex, *Layer->GetName());
					bIsValid = false;
				}
			}

			if (LayerStates[LayerIndex])
			{
				++NumActiveLayers;
			}
		}
	}

	for (int32 BlendIndex = 0; BlendIndex < NumBlends; ++BlendIndex)
	{
		UMaterialFunctionInterface* Blend = Blends[BlendIndex];
		
		if (Blend)
		{
			if (Blend->GetMaterialFunctionUsage() != EMaterialFunctionUsage::MaterialLayerBlend)
			{
				COMPILER_OR_LOG_ERROR(TEXT("Blend %i, %s, not set for layer blend usage."), BlendIndex, *Blend->GetName());
				bIsValid = false;
			}
			else if (UMaterialFunctionInstance* InstanceBlend = Cast<UMaterialFunctionInstance>(Blend))
			{
				if (!InstanceBlend->Parent)
				{
					COMPILER_OR_LOG_ERROR(TEXT("Blend %i, %s, layer instance has no parent set."), BlendIndex, *Blend->GetName());
					bIsValid = false;
				}
			}
			else
			{
				TArray<UMaterialExpressionFunctionInput*> InputExpressions;
				Blend->GetAllExpressionsOfType<UMaterialExpressionFunctionInput>(InputExpressions, false);
				if (InputExpressions.Num() != 2)
				{
					COMPILER_OR_LOG_ERROR(TEXT("Blend %i, %s, must have two MaterialAttributes inputs only."), BlendIndex, *Blend->GetName());
					bIsValid = false;
				}
			}
		}
		
		// Null blends signify an opaque layer so count as valid for the sake of graph validation
		if (Layers[BlendIndex+1] && LayerStates[BlendIndex+1])
		{
			++NumActiveBlends;
		}
	}

	bool bValidGraphLayout = (NumActiveLayers == 0 && NumActiveBlends == 0)		// Pass-through
		|| (NumActiveLayers == 1 && NumActiveBlends == 0)						// Single layer
		|| (NumActiveLayers >= 2 && NumActiveBlends == NumActiveLayers - 1);	// Blend graph

	if (!bValidGraphLayout)
	{
		COMPILER_OR_LOG_ERROR(TEXT("Invalid number of layers (%i) or blends (%i) assigned."), NumActiveLayers, NumActiveBlends);
		bIsValid = false;
	}

	if (Compiler && Compiler->GetCurrentFunctionStackDepth() > 1)
	{
		COMPILER_OR_LOG_ERROR(TEXT("Layer expressions cannot be used within a material function."));
		bIsValid = false;
	}

	return bIsValid;

#undef COMPILER_OR_LOG_ERROR
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
bool UMaterialExpressionMaterialAttributeLayers::IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
{
	const TArray<UMaterialFunctionInterface*>& Layers = GetLayers();
	const TArray<UMaterialFunctionInterface*>& Blends = GetBlends();

	for (auto* Layer : Layers)
	{
		if (Layer)
		{		
			if (!Layer->IterateDependentFunctions(Predicate))
			{
				return false;
			}
			if (!Predicate(Layer))
			{
				return false;
			}
		}
	}

	for (auto* Blend : Blends)
	{
		if (Blend)
		{
			if (!Blend->IterateDependentFunctions(Predicate))
			{
				return false;
			}
			if (!Predicate(Blend))
			{
				return false;
			}
		}
	}
	return true;
}

void UMaterialExpressionMaterialAttributeLayers::GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const
{
	IterateDependentFunctions([&DependentFunctions](UMaterialFunctionInterface* MaterialFunction) -> bool
	{
		DependentFunctions.AddUnique(MaterialFunction);
		return true;
	});
}
#endif // WITH_EDITORONLY_DATA

UMaterialFunctionInterface* UMaterialExpressionMaterialAttributeLayers::GetParameterAssociatedFunction(const FHashedMaterialParameterInfo& ParameterInfo) const
{
	check(ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter);

	// Grab the associated layer or blend
	UMaterialFunctionInterface* LayersFunction = nullptr;

	if (ParameterInfo.Association == EMaterialParameterAssociation::LayerParameter)
	{
		const TArray<UMaterialFunctionInterface*>& Layers = GetLayers();
		if (Layers.IsValidIndex(ParameterInfo.Index))
		{
			LayersFunction = Layers[ParameterInfo.Index];
		}
	}
	else if (ParameterInfo.Association == EMaterialParameterAssociation::BlendParameter)
	{
		const TArray<UMaterialFunctionInterface*>& Blends = GetBlends();
		if (Blends.IsValidIndex(ParameterInfo.Index))
		{
			LayersFunction = Blends[ParameterInfo.Index];
		}
	}

	return LayersFunction;
}

#if WITH_EDITOR
int32 UMaterialExpressionMaterialAttributeLayers::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result = INDEX_NONE;

	// The layer stack is a parameter so can be overridden
	const FMaterialLayersFunctions* OverrideLayers = Compiler->StaticMaterialLayersParameter(ParameterName);
	OverrideLayerGraph(OverrideLayers);

	if (ValidateLayerConfiguration(Compiler, true) && bIsLayerGraphBuilt)
	{
		if (NumActiveBlendCallers > 0 && BlendCallers[NumActiveBlendCallers-1]->MaterialFunction)
		{
			// Multiple blended layers
			Result = BlendCallers[NumActiveBlendCallers-1]->Compile(Compiler, 0);
		}
		else if (NumActiveLayerCallers > 0 && LayerCallers[NumActiveLayerCallers-1]->MaterialFunction)
		{
			// Single layer
			Result = LayerCallers[NumActiveLayerCallers-1]->Compile(Compiler, 0);
		}
		else if (NumActiveLayerCallers == 0)
		{
			// Pass-through
			const FGuid AttributeID = Compiler->GetMaterialAttribute();
			if (Input.GetTracedInput().Expression)
			{
				Result = Input.CompileWithDefault(Compiler, AttributeID);
			}
			else
			{
				Result = FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, AttributeID);
			}
		}
		else
		{
			// Error on unknown mismatch
			Result = Compiler->Errorf(TEXT("Unknown error occured on validated layers."));
		}
	}
	else
	{
		// Error on unknown mismatch
		Result = Compiler->Errorf(TEXT("Failed to validate layer configuration."));
	}

	OverrideLayerGraph(nullptr);

	return Result;
}

void UMaterialExpressionMaterialAttributeLayers::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Material Attribute Layers"));
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

void UMaterialExpressionMaterialAttributeLayers::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Evaluates the active material layer stack and outputs the merged attributes."), 40, OutToolTip);
}

const TArray<FExpressionInput*> UMaterialExpressionMaterialAttributeLayers::GetInputs()
{
	TArray<FExpressionInput*> Result;
	Result.Add(&Input);
	return Result;
}

FExpressionInput* UMaterialExpressionMaterialAttributeLayers::GetInput(int32 InputIndex)
{
	if (InputIndex == 0)
	{
		return &Input;
	}

	return nullptr;
}

FName UMaterialExpressionMaterialAttributeLayers::GetInputName(int32 InputIndex) const
{
	return NAME_None;
}

uint32 UMaterialExpressionMaterialAttributeLayers::GetInputType(int32 InputIndex)
{
	return MCT_MaterialAttributes;
}
#endif

bool UMaterialExpressionMaterialAttributeLayers::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, FMaterialLayersFunctions& OutLayers, FGuid& OutExpressionGuid) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutLayers = (ParamLayers != nullptr) ? *ParamLayers : DefaultLayers;
		OutExpressionGuid = ExpressionGUID;
		return true;
	}

	return false;
}

#if WITH_EDITOR
bool UMaterialExpressionMaterialAttributeLayers::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	if (ParameterName.ToString().Contains(SearchQuery))
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

FString UMaterialExpressionMaterialAttributeLayers::GetEditableName() const
{
	return ParameterName.ToString();
}

void UMaterialExpressionMaterialAttributeLayers::SetEditableName(const FString& NewName)
{
	ParameterName = *NewName;
}
#endif

void UMaterialExpressionMaterialAttributeLayers::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName);
	OutParameterInfo.AddUnique(NewParameter);
	if (CurrentSize != OutParameterInfo.Num())
	{
		OutParameterIds.Add(ExpressionGUID);
	}
	
	checkf(InBaseParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter && InBaseParameterInfo.Index == INDEX_NONE,
		TEXT("Tried to gather parameters from a material layer not in the top-level material, shouldn't be possible"));
}

// -----

UMaterialExpressionFloor::UMaterialExpressionFloor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFloor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Floor input"));
	}

	return Compiler->Floor(Input.Compile(Compiler));
}

void UMaterialExpressionFloor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Floor"));
}
#endif // WITH_EDITOR

UMaterialExpressionCeil::UMaterialExpressionCeil(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCeil::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Ceil input"));
	}
	return Compiler->Ceil(Input.Compile(Compiler));
}


void UMaterialExpressionCeil::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Ceil"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionRound
//
UMaterialExpressionRound::UMaterialExpressionRound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionRound::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Round input"));
	}
	return Compiler->Round(Input.Compile(Compiler));
}

void UMaterialExpressionRound::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Round"));
}

void UMaterialExpressionRound::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Rounds the value up to the next whole number if the fractional part is greater than or equal to half, else rounds down."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionTruncate
//
UMaterialExpressionTruncate::UMaterialExpressionTruncate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTruncate::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Truncate input"));
	}
	return Compiler->Truncate(Input.Compile(Compiler));
}

void UMaterialExpressionTruncate::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Truncate"));
}

void UMaterialExpressionTruncate::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Truncates a value by discarding the fractional part."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionSign
//
UMaterialExpressionSign::UMaterialExpressionSign(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSign::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Sign input"));
	}
	return Compiler->Sign(Input.Compile(Compiler));
}

void UMaterialExpressionSign::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Sign"));
}

void UMaterialExpressionSign::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns -1 if the input is less than 0, 1 if greater, or 0 if equal."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionFmod
//

UMaterialExpressionFmod::UMaterialExpressionFmod(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFmod::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Fmod input A"));
	}
	if (!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Fmod input B"));
	}
	return Compiler->Fmod(A.Compile(Compiler), B.Compile(Compiler));
}

void UMaterialExpressionFmod::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Fmod"));
}
#endif // WITH_EDITOR

UMaterialExpressionFrac::UMaterialExpressionFrac(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFrac::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Frac input"));
	}

	return Compiler->Frac(Input.Compile(Compiler));
}

void UMaterialExpressionFrac::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Frac"));
}
#endif // WITH_EDITOR

UMaterialExpressionDesaturation::UMaterialExpressionDesaturation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Color;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Color(LOCTEXT( "Color", "Color" ))
			, NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	LuminanceFactors = FLinearColor(0.3f, 0.59f, 0.11f, 0.0f);

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Color);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDesaturation::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
		return Compiler->Errorf(TEXT("Missing Desaturation input"));

	int32 Color = Compiler->ForceCast(Input.Compile(Compiler), MCT_Float3, MFCF_ExactMatch|MFCF_ReplicateValue),
		Grey = Compiler->Dot(Color,Compiler->Constant3(LuminanceFactors.R,LuminanceFactors.G,LuminanceFactors.B));

	if(Fraction.GetTracedInput().Expression)
		return Compiler->Lerp(Color,Grey,Fraction.Compile(Compiler));
	else
		return Grey;
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionParameter
//
FName UMaterialExpressionParameter::ParameterDefaultName = TEXT("Param");

UMaterialExpressionParameter::UMaterialExpressionParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Parameters;
		FName ParameterName;
		FConstructorStatics()
			: NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
			, ParameterName(UMaterialExpressionParameter::ParameterDefaultName)
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bIsParameterExpression = true;
	ParameterName = ConstructorStatics.ParameterName;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR

bool UMaterialExpressionParameter::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if( ParameterName.ToString().Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}



FString UMaterialExpressionParameter::GetEditableName() const
{
	return ParameterName.ToString();
}

void UMaterialExpressionParameter::SetEditableName(const FString& NewName)
{
	ParameterName = *NewName;
}

#endif

void UMaterialExpressionParameter::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);

#if WITH_EDITOR
	if (HasConnectedOutputs())
#endif
	{
		OutParameterInfo.AddUnique(MoveTemp(NewParameter));
		if (CurrentSize != OutParameterInfo.Num())
		{
			OutParameterIds.Add(ExpressionGUID);
		}
	}
}

#if WITH_EDITOR
void UMaterialExpressionParameter::ValidateParameterName(const bool bAllowDuplicateName)
{
	ValidateParameterNameInternal(this, Material, bAllowDuplicateName);
}
#endif

//
//	UMaterialExpressionVectorParameter
//
UMaterialExpressionVectorParameter::UMaterialExpressionVectorParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 0, 1));
	ApplyChannelNames();
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionVectorParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (bUseCustomPrimitiveData)
	{
		return Compiler->CustomPrimitiveData(PrimitiveDataIndex, MCT_Float4);
	}
	else
	{
		return Compiler->VectorParameter(ParameterName,DefaultValue);
	}
}

void UMaterialExpressionVectorParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	if (bUseCustomPrimitiveData)
	{
		FString IndexString = FString::Printf(TEXT("Index %d"), PrimitiveDataIndex);

		// Add info about remaining 3 components
		for (int i = 1; i < 4; i++)
		{
			// Append index if it's valid, otherwise append N/A
			if(PrimitiveDataIndex+i < FCustomPrimitiveData::NumCustomPrimitiveDataFloats)
			{
				IndexString.Append(FString::Printf(TEXT(", %d"), PrimitiveDataIndex+i));
			}
			else
			{
				IndexString.Append(FString::Printf(TEXT(", N/A")));
			}
		}

		OutCaptions.Add(IndexString); 
		OutCaptions.Add(FString::Printf(TEXT("Custom Primitive Data"))); 
	}
	else
	{
		OutCaptions.Add(FString::Printf(
			 TEXT("Param (%.3g,%.3g,%.3g,%.3g)"),
			 DefaultValue.R,
			 DefaultValue.G,
			 DefaultValue.B,
			 DefaultValue.A ));
	}

	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString())); 
}
#endif // WITH_EDITOR

bool UMaterialExpressionVectorParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutValue = DefaultValue;
		return true;
	}

	return false;
}

void UMaterialExpressionVectorParameter::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	if (!bUseCustomPrimitiveData)
	{
		Super::GetAllParameterInfo(OutParameterInfo, OutParameterIds, InBaseParameterInfo);
	}
}

#if WITH_EDITOR
bool UMaterialExpressionVectorParameter::SetParameterValue(FName InParameterName, FLinearColor InValue)
{
	if (InParameterName == ParameterName)
	{
		DefaultValue = InValue;
		return true;
	}
	return false;
}

void UMaterialExpressionVectorParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionVectorParameter, DefaultValue))
	{
		// Callback into the editor
		FEditorSupportDelegates::VectorParameterDefaultChanged.Broadcast(this, ParameterName, DefaultValue);
	}
	else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionVectorParameter, PrimitiveDataIndex))
	{
		// Clamp value
		const int32 PrimDataIndex = PrimitiveDataIndex;
		PrimitiveDataIndex = (uint8)FMath::Clamp(PrimDataIndex, 0, FCustomPrimitiveData::NumCustomPrimitiveDataFloats-1);
	}
	else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionVectorParameter, ChannelNames)
		&& !IsUsedAsChannelMask())
	{
		ApplyChannelNames();

		if (GraphNode)
		{
			GraphNode->ReconstructNode();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialExpressionVectorParameter::ApplyChannelNames()
{
	Outputs[1].OutputName = FName(*ChannelNames.R.ToString());
	Outputs[2].OutputName = FName(*ChannelNames.G.ToString());
	Outputs[3].OutputName = FName(*ChannelNames.B.ToString());
	Outputs[4].OutputName = FName(*ChannelNames.A.ToString());
	bShowOutputNameOnPin = !ChannelNames.R.IsEmpty() || !ChannelNames.G.IsEmpty() || !ChannelNames.B.IsEmpty() || !ChannelNames.A.IsEmpty();
}


void UMaterialExpressionVectorParameter::ValidateParameterName(const bool bAllowDuplicateName)
{
	bool bOverrideDuplicateBehavior = false;
	TArray<UMaterialExpression*> Expressions;
	if (Material)
	{
		Expressions = Material->Expressions;
	}
	else if (Function)
	{
		Expressions = Function->FunctionExpressions;
	}

	for (UMaterialExpression* Expression : Expressions)
	{
		if (Expression != nullptr && Expression->HasAParameterName())
		{
			UMaterialExpressionVectorParameter* VectorExpression = Cast<UMaterialExpressionVectorParameter>(Expression);
			if (VectorExpression
				&& GetParameterName() == VectorExpression->GetParameterName()
				&& IsUsedAsChannelMask() != VectorExpression->IsUsedAsChannelMask())
			{
				bOverrideDuplicateBehavior = true;
				break;
			}
		}
	}
	Super::ValidateParameterName(bOverrideDuplicateBehavior ? false : bAllowDuplicateName);
}

bool UMaterialExpressionVectorParameter::HasClassAndNameCollision(UMaterialExpression* OtherExpression) const
{
	UMaterialExpressionVectorParameter* VectorExpression = Cast<UMaterialExpressionVectorParameter>(OtherExpression);
	if (VectorExpression
		&& GetParameterName() == VectorExpression->GetParameterName()
		&& IsUsedAsChannelMask() != VectorExpression->IsUsedAsChannelMask())
	{
		return true;
	}
	return Super::HasClassAndNameCollision(OtherExpression);
}

void UMaterialExpressionVectorParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	FLinearColor ExistingValue = FLinearColor::Transparent;
	Material->GetVectorParameterValue(FMaterialParameterInfo(OtherExpression->GetParameterName()), ExistingValue);
	DefaultValue = ExistingValue;
	FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionVectorParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionVectorParameter, DefaultValue));
	FPropertyChangedEvent PropertyChangedEvent(ParamProperty);
	PostEditChangeProperty(PropertyChangedEvent);
}
#endif

//
//	UMaterialExpressionChannelMaskParameter
//
UMaterialExpressionChannelMaskParameter::UMaterialExpressionChannelMaskParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	bShowMaskColorsOnPin = false;
#endif

	// Default mask to red channel
	DefaultValue = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f);
	MaskChannel = EChannelMaskParameterColor::Red;
}

#if WITH_EDITOR
bool UMaterialExpressionChannelMaskParameter::SetParameterValue(FName InParameterName, FLinearColor InValue)
{
	if (InParameterName == ParameterName)
	{
		// Update value
		DefaultValue = InValue;

		// Update enum
		if (DefaultValue.R > 0.0f)
		{
			MaskChannel = EChannelMaskParameterColor::Red;
		}
		else if (DefaultValue.G > 0.0f)
		{
			MaskChannel = EChannelMaskParameterColor::Green;
		}
		else if (DefaultValue.B > 0.0f)
		{
			MaskChannel = EChannelMaskParameterColor::Blue;
		}
		else
		{
			MaskChannel = EChannelMaskParameterColor::Alpha;
		}

		// Update caption and preview
		if (GraphNode)
		{
			CastChecked<UMaterialGraphNode>(GraphNode)->RecreateAndLinkNode();
		}

		return true;
	}

	return false;
}

void UMaterialExpressionChannelMaskParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionChannelMaskParameter, MaskChannel))
	{
		// Update internal value
		switch (MaskChannel)
		{
		case EChannelMaskParameterColor::Red:
			DefaultValue = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f); break;
		case EChannelMaskParameterColor::Green:
			DefaultValue = FLinearColor(0.0f, 1.0f, 0.0f, 0.0f); break;
		case EChannelMaskParameterColor::Blue:
			DefaultValue = FLinearColor(0.0f, 0.0f, 1.0f, 0.0f); break;
		default:
			DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f); break;
		}

		FEditorSupportDelegates::VectorParameterDefaultChanged.Broadcast(this, ParameterName, DefaultValue);
	}
	else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionVectorParameter, DefaultValue))
	{
		// If the vector parameter was updated, the enum needs to match and we assert the values are valid
		if (DefaultValue.R > 0.0f)
		{
			MaskChannel = EChannelMaskParameterColor::Red;
			DefaultValue = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f);
		}
		else if (DefaultValue.G > 0.0f)
		{
			MaskChannel = EChannelMaskParameterColor::Green;
			DefaultValue = FLinearColor(0.0f, 1.0f, 0.0f, 0.0f);
		}
		else if (DefaultValue.B > 0.0f)
		{
			MaskChannel = EChannelMaskParameterColor::Blue;
			DefaultValue = FLinearColor(0.0f, 0.0f, 1.0f, 0.0f);
		}
		else
		{
			MaskChannel = EChannelMaskParameterColor::Alpha;
			DefaultValue = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f);
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

int32 UMaterialExpressionChannelMaskParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing mask input"));
	}

	int32 Ret = Input.Compile(Compiler);
	Ret = Compiler->ForceCast(Ret, MCT_Float4, MFCF_ForceCast);

	if (Ret != INDEX_NONE)
	{
		// Internally this mask is a simple dot product, the mask is stored as a vector parameter
		int32 Param = Compiler->VectorParameter(ParameterName, DefaultValue);
		Ret = Compiler->Dot(Ret, Param);
	}
	else
	{
		Ret = Compiler->Errorf(TEXT("Failed to compile mask input"));
	}
	
	return Ret;
}

void UMaterialExpressionChannelMaskParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	switch (MaskChannel)
	{
	case EChannelMaskParameterColor::Red:
		OutCaptions.Add(TEXT("Red")); break;
	case EChannelMaskParameterColor::Green:
		OutCaptions.Add(TEXT("Green")); break;
	case EChannelMaskParameterColor::Blue:
		OutCaptions.Add(TEXT("Blue")); break;
	default:
		OutCaptions.Add(TEXT("Alpha")); break;
	}

	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString())); 
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionScalarParameter
//
UMaterialExpressionScalarParameter::UMaterialExpressionScalarParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bCollapsed = true;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionScalarParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (bUseCustomPrimitiveData)
	{
		return Compiler->CustomPrimitiveData(PrimitiveDataIndex, MCT_Float);
	}
	else
	{
		return Compiler->ScalarParameter(ParameterName,DefaultValue);
	}
}

void UMaterialExpressionScalarParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	if (bUseCustomPrimitiveData)
	{
		OutCaptions.Add(FString::Printf(TEXT("Index %d"), PrimitiveDataIndex)); 
		OutCaptions.Add(FString::Printf(TEXT("Custom Primitive Data"))); 
	}
	else
	{
		OutCaptions.Add(FString::Printf(
			 TEXT("Param (%.4g)"),
			DefaultValue )); 
	}
	 OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString())); 
}
#endif // WITH_EDITOR

bool UMaterialExpressionScalarParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutValue = DefaultValue;
		return true;
	}

	return false;
}

void UMaterialExpressionScalarParameter::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	if (!bUseCustomPrimitiveData)
	{
		Super::GetAllParameterInfo(OutParameterInfo, OutParameterIds, InBaseParameterInfo);
	}
}

#if WITH_EDITOR
bool UMaterialExpressionScalarParameter::SetParameterValue(FName InParameterName, float InValue)
{
	if (InParameterName == ParameterName)
	{
		DefaultValue = InValue;
		return true;
	}
	return false;
}

void UMaterialExpressionScalarParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionScalarParameter, DefaultValue))
	{
		// Callback into the editor
		FEditorSupportDelegates::ScalarParameterDefaultChanged.Broadcast(this, ParameterName, DefaultValue);
	}
	else if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionScalarParameter, PrimitiveDataIndex))
	{
		// Clamp value
		const int32 PrimDataIndex = PrimitiveDataIndex;
		PrimitiveDataIndex = (uint8)FMath::Clamp(PrimDataIndex, 0, FCustomPrimitiveData::NumCustomPrimitiveDataFloats-1);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialExpressionScalarParameter::ValidateParameterName(const bool bAllowDuplicateName)
{
	bool bOverrideDuplicateBehavior = false;
	TArray<UMaterialExpression*> Expressions;
	if (Material)
	{
		Expressions = Material->Expressions;
	}
	else if (Function)
	{
		Expressions = Function->FunctionExpressions;
	}

	for (UMaterialExpression* Expression : Expressions)
	{
		if (Expression != nullptr && Expression->HasAParameterName())
		{
			UMaterialExpressionScalarParameter* ScalarExpression = Cast<UMaterialExpressionScalarParameter>(Expression);
			if (ScalarExpression 
				&& GetParameterName() == ScalarExpression->GetParameterName() 
				&& IsUsedAsAtlasPosition() != ScalarExpression->IsUsedAsAtlasPosition())
			{
				bOverrideDuplicateBehavior = true;
				break;
			}
		}
	}
	Super::ValidateParameterName(bOverrideDuplicateBehavior ? false : bAllowDuplicateName);
}

bool UMaterialExpressionScalarParameter::HasClassAndNameCollision(UMaterialExpression* OtherExpression) const
{
	UMaterialExpressionScalarParameter* ScalarExpression = Cast<UMaterialExpressionScalarParameter>(OtherExpression);
	if (ScalarExpression
		&& GetParameterName() == ScalarExpression->GetParameterName()
		&& IsUsedAsAtlasPosition() != ScalarExpression->IsUsedAsAtlasPosition())
	{
		return true;
	}
	return Super::HasClassAndNameCollision(OtherExpression);
}

void UMaterialExpressionScalarParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	float ExistingValue = 0.0f;
	Material->GetScalarParameterDefaultValue(FMaterialParameterInfo(OtherExpression->GetParameterName()), ExistingValue);
	DefaultValue = ExistingValue;
	FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionScalarParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionScalarParameter, DefaultValue));
	FPropertyChangedEvent PropertyChangedEvent(ParamProperty);
	PostEditChangeProperty(PropertyChangedEvent);
}

#endif

//
//	UMaterialExpressionStaticSwitchParameter
//
UMaterialExpressionStaticSwitchParameter::UMaterialExpressionStaticSwitchParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
bool UMaterialExpressionStaticSwitchParameter::IsResultMaterialAttributes(int32 OutputIndex)
{
	check(OutputIndex == 0);
	// This one is a little tricky. Since we are treating a dangling reroute as an empty expression, this
	// should early out, whereas IsResultMaterialAttributes on a reroute node will return false as the 
	// reroute node's input is dangling and therefore its type is unknown.
	if ((A.GetTracedInput().Expression && !A.Expression->ContainsInputLoop() && A.Expression->IsResultMaterialAttributes(A.OutputIndex)) ||
		(B.GetTracedInput().Expression && !B.Expression->ContainsInputLoop() && B.Expression->IsResultMaterialAttributes(B.OutputIndex)))
	{
		return true;
	}
	else
	{
		return false;
	}
}

int32 UMaterialExpressionStaticSwitchParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	bool bSucceeded;
	const bool bValue = Compiler->GetStaticBoolValue(Compiler->StaticBoolParameter(ParameterName,DefaultValue), bSucceeded);
	
	//Both A and B must be connected in a parameter. 
	if( !A.GetTracedInput().IsConnected() )
	{
		Compiler->Errorf(TEXT("Missing A input"));
		bSucceeded = false;
	}
	if( !B.GetTracedInput().IsConnected() )
	{
		Compiler->Errorf(TEXT("Missing B input"));
		bSucceeded = false;
	}

	if (!bSucceeded)
	{
		return INDEX_NONE;
	}

	if (bValue)
	{
		return A.Compile(Compiler);
	}
	else
	{
		return B.Compile(Compiler);
	}
}

void UMaterialExpressionStaticSwitchParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("Switch Param (%s)"), (DefaultValue ? TEXT("True") : TEXT("False")))); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString())); 
}

FName UMaterialExpressionStaticSwitchParameter::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("True");
	}
	else
	{
		return TEXT("False");
	}
}
#endif // WITH_EDITOR


//
//	UMaterialExpressionStaticBoolParameter
//
UMaterialExpressionStaticBoolParameter::UMaterialExpressionStaticBoolParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bHidePreviewWindow = true;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionStaticBoolParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->StaticBoolParameter(ParameterName,DefaultValue);
}

int32 UMaterialExpressionStaticBoolParameter::CompilePreview(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return INDEX_NONE;
}

void UMaterialExpressionStaticBoolParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("Static Bool Param (%s)"), (DefaultValue ? TEXT("True") : TEXT("False")))); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString())); 
}

void UMaterialExpressionStaticBoolParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	bool ExistingValue = false;
	FGuid Guid;
	Material->GetStaticSwitchParameterValue(OtherExpression->GetParameterName(), ExistingValue, Guid);
	DefaultValue = ExistingValue;
	FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionStaticBoolParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionStaticBoolParameter, DefaultValue));
	FPropertyChangedEvent PropertyChangedEvent(ParamProperty);
	PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

bool UMaterialExpressionStaticBoolParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutValue = DefaultValue;
		OutExpressionGuid = ExpressionGUID;
		return true;
	}

	return false;
}

#if WITH_EDITOR
bool UMaterialExpressionStaticBoolParameter::SetParameterValue(FName InParameterName, bool InValue, FGuid InExpressionGuid)
{
	if (InParameterName == ParameterName)
	{
		DefaultValue = InValue;
		ExpressionGUID = InExpressionGuid;
		return true;
	}

	return false;
}
#endif

//
//	UMaterialExpressionStaticBool
//
UMaterialExpressionStaticBool::UMaterialExpressionStaticBool(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Functions;
		FConstructorStatics()
			: NAME_Functions(LOCTEXT( "Functions", "Functions" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bHidePreviewWindow = true;

	MenuCategories.Add(ConstructorStatics.NAME_Functions);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionStaticBool::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->StaticBool(Value);
}

int32 UMaterialExpressionStaticBool::CompilePreview(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return INDEX_NONE;
}

void UMaterialExpressionStaticBool::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Static Bool ")) + (Value ? TEXT("(True)") : TEXT("(False)")));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionStaticSwitch
//
UMaterialExpressionStaticSwitch::UMaterialExpressionStaticSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Functions;
		FConstructorStatics()
			: NAME_Functions(LOCTEXT( "Functions", "Functions" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Functions);
#endif
}

#if WITH_EDITOR
bool UMaterialExpressionStaticSwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	// If there is a loop anywhere in this expression's inputs then we can't risk checking them. 
	// This one is a little tricky with respect to Reroute nodes. Since we are treating a dangling reroute as an empty expression, this
	// should early out, whereas IsResultMaterialAttributes on a reroute node will return false as the 
	// reroute node's input is dangling and therefore its type is unknown.
	check(OutputIndex == 0);
	if ((A.GetTracedInput().Expression && !A.Expression->ContainsInputLoop() && A.Expression->IsResultMaterialAttributes(A.OutputIndex)) ||
		(B.GetTracedInput().Expression && !B.Expression->ContainsInputLoop() && B.Expression->IsResultMaterialAttributes(B.OutputIndex)))
	{
		return true;
	}
	else
	{
		return false;
	}
}

int32 UMaterialExpressionStaticSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	bool bValue = DefaultValue;

	if (Value.GetTracedInput().Expression)
	{
		bool bSucceeded;
		bValue = Compiler->GetStaticBoolValue(Value.Compile(Compiler), bSucceeded);

		if (!bSucceeded)
		{
			return INDEX_NONE;
		}
	}
	
	// We only call Compile on the branch that is taken to avoid compile errors in the disabled branch.
	if (bValue)
	{
		return A.Compile(Compiler);
	}
	else
	{
		return B.Compile(Compiler);
	}
}

void UMaterialExpressionStaticSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Switch")));
}

FName UMaterialExpressionStaticSwitch::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("True");
	}
	else if (InputIndex == 1)
	{
		return TEXT("False");
	}
	else
	{
		return TEXT("Value");
	}
}

uint32 UMaterialExpressionStaticSwitch::GetInputType(int32 InputIndex)
{
	if (InputIndex == 0 || InputIndex == 1)
	{
		return MCT_Unknown;
	}
	else
	{
		return MCT_StaticBool;
	}
}
#endif

//
//	UMaterialExpressionPreviousFrameSwitch
//
UMaterialExpressionPreviousFrameSwitch::UMaterialExpressionPreviousFrameSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Functions;
		FConstructorStatics()
			: NAME_Functions(LOCTEXT("Functions", "Functions"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Functions);
#endif
}

#if WITH_EDITOR
bool UMaterialExpressionPreviousFrameSwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	// If there is a loop anywhere in this expression's inputs then we can't risk checking them
	check(OutputIndex == 0);
	if ((CurrentFrame.Expression && !CurrentFrame.Expression->ContainsInputLoop() && CurrentFrame.Expression->IsResultMaterialAttributes(CurrentFrame.OutputIndex)) ||
		(PreviousFrame.Expression && !PreviousFrame.Expression->ContainsInputLoop() && PreviousFrame.Expression->IsResultMaterialAttributes(PreviousFrame.OutputIndex)))
	{
		return true;
	}
	else
	{
		return false;
	}
}

int32 UMaterialExpressionPreviousFrameSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Compiler->IsCurrentlyCompilingForPreviousFrame())
	{
		return PreviousFrame.Compile(Compiler);
	}
	return CurrentFrame.Compile(Compiler);
}

void UMaterialExpressionPreviousFrameSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("PreviousFrameSwitch")));
}

void UMaterialExpressionPreviousFrameSwitch::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Used to manually provide expressions for motion vector generation caused by changes in world position offset between frames."), 40, OutToolTip);
}

FName UMaterialExpressionPreviousFrameSwitch::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("Current Frame");
	}
	else
	{
		return TEXT("Previous Frame");
	}
}

uint32 UMaterialExpressionPreviousFrameSwitch::GetInputType(int32 InputIndex)
{
	return MCT_Unknown;
}
#endif

//
//	UMaterialExpressionQualitySwitch
//

UMaterialExpressionQualitySwitch::UMaterialExpressionQualitySwitch(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionQualitySwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const EMaterialQualityLevel::Type QualityLevelToCompile = Compiler->GetQualityLevel();
	FExpressionInput DefaultTraced = Default.GetTracedInput();

	if (!DefaultTraced.Expression)
	{
		return Compiler->Errorf(TEXT("Quality switch missing default input"));
	}

	if (QualityLevelToCompile != EMaterialQualityLevel::Num)
	{
		check(QualityLevelToCompile < UE_ARRAY_COUNT(Inputs));
		FExpressionInput QualityInput = Inputs[QualityLevelToCompile].GetTracedInput();
		if (QualityInput.Expression)
		{
			return QualityInput.Compile(Compiler);
		}
	}

	return DefaultTraced.Compile(Compiler);
}

void UMaterialExpressionQualitySwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Quality Switch")));
}

const TArray<FExpressionInput*> UMaterialExpressionQualitySwitch::GetInputs()
{
	TArray<FExpressionInput*> OutInputs;

	OutInputs.Add(&Default);

	for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(Inputs); InputIndex++)
	{
		OutInputs.Add(&Inputs[InputIndex]);
	}

	return OutInputs;
}

FExpressionInput* UMaterialExpressionQualitySwitch::GetInput(int32 InputIndex)
{
	if (InputIndex == 0)
	{
		return &Default;
	}

	return &Inputs[InputIndex - 1];
}

FName UMaterialExpressionQualitySwitch::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("Default");
	}

	return GetMaterialQualityLevelFName((EMaterialQualityLevel::Type)(InputIndex - 1));
}

bool UMaterialExpressionQualitySwitch::IsInputConnectionRequired(int32 InputIndex) const
{
	return InputIndex == 0;
}

bool UMaterialExpressionQualitySwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	check(OutputIndex == 0);
	TArray<FExpressionInput*> ExpressionInputs = GetInputs();

	for (FExpressionInput* ExpressionInput : ExpressionInputs)
	{
		// If there is a loop anywhere in this expression's inputs then we can't risk checking them
		if (ExpressionInput->Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}

	return false;
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionFeatureLevelSwitch
//

UMaterialExpressionFeatureLevelSwitch::UMaterialExpressionFeatureLevelSwitch(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFeatureLevelSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const ERHIFeatureLevel::Type FeatureLevelToCompile = Compiler->GetFeatureLevel();
	check(FeatureLevelToCompile < UE_ARRAY_COUNT(Inputs));
	FExpressionInput& FeatureInput = Inputs[FeatureLevelToCompile];

	if (!Default.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Feature Level switch missing default input"));
	}

	if (FeatureInput.GetTracedInput().Expression)
	{
		return FeatureInput.Compile(Compiler);
	}

	return Default.Compile(Compiler);
}

void UMaterialExpressionFeatureLevelSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Feature Level Switch")));
}

const TArray<FExpressionInput*> UMaterialExpressionFeatureLevelSwitch::GetInputs()
{
	TArray<FExpressionInput*> OutInputs;

	OutInputs.Add(&Default);

	for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(Inputs); InputIndex++)
	{
		OutInputs.Add(&Inputs[InputIndex]);
	}

	return OutInputs;
}

FExpressionInput* UMaterialExpressionFeatureLevelSwitch::GetInput(int32 InputIndex)
{
	if (InputIndex == 0)
	{
		return &Default;
	}

	return &Inputs[InputIndex - 1];
}

FName UMaterialExpressionFeatureLevelSwitch::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("Default");
	}

	FName FeatureLevelName;
	GetFeatureLevelName((ERHIFeatureLevel::Type)(InputIndex - 1), FeatureLevelName);
	return FeatureLevelName;
}

bool UMaterialExpressionFeatureLevelSwitch::IsInputConnectionRequired(int32 InputIndex) const
{
	return InputIndex == 0;
}


bool UMaterialExpressionFeatureLevelSwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	check(OutputIndex == 0);
	TArray<FExpressionInput*> ExpressionInputs = GetInputs();

	for (FExpressionInput* ExpressionInput : ExpressionInputs)
	{
		// If there is a loop anywhere in this expression's inputs then we can't risk checking them
		if (ExpressionInput->GetTracedInput().Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}

	return false;
}
#endif // WITH_EDITOR

void UMaterialExpressionFeatureLevelSwitch::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();
	UnderlyingArchive.UsingCustomVersion(FRenderingObjectVersion::GUID);

#if !WITH_EDITORONLY_DATA
	FExpressionInput Inputs[ERHIFeatureLevel::Num];
#endif

	if (UnderlyingArchive.IsLoading() && UnderlyingArchive.UE4Ver() < VER_UE4_RENAME_SM3_TO_ES3_1)
	{
		// Copy the ES2 input to SM3 (since SM3 will now become ES3_1 and we don't want broken content)
		Inputs[ERHIFeatureLevel::ES3_1] = Inputs[ERHIFeatureLevel::ES2_REMOVED];
	}

	if (UnderlyingArchive.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::RemovedSM4)
	{
		Inputs[ERHIFeatureLevel::SM4_REMOVED] = UMaterialExpressionFeatureLevelSwitch::Default;
	}
}

//
//	UMaterialExpressionShadingPathSwitch
//

UMaterialExpressionShadingPathSwitch::UMaterialExpressionShadingPathSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionShadingPathSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const EShaderPlatform ShaderPlatform = Compiler->GetShaderPlatform();
	ERHIShadingPath::Type ShadingPathToCompile = ERHIShadingPath::Deferred;

	if (IsForwardShadingEnabled(ShaderPlatform))
	{
		ShadingPathToCompile = ERHIShadingPath::Forward;
	}
	else if (Compiler->GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		ShadingPathToCompile = ERHIShadingPath::Mobile;
	}

	check(ShadingPathToCompile < UE_ARRAY_COUNT(Inputs));
	FExpressionInput ShadingPathInput = Inputs[ShadingPathToCompile].GetTracedInput();
	FExpressionInput DefaultTraced = Default.GetTracedInput();

	if (!DefaultTraced.Expression)
	{
		return Compiler->Errorf(TEXT("Shading path switch missing default input"));
	}

	if (ShadingPathInput.Expression)
	{
		return ShadingPathInput.Compile(Compiler);
	}

	return DefaultTraced.Compile(Compiler);
}

void UMaterialExpressionShadingPathSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Shading Path Switch")));
}

const TArray<FExpressionInput*> UMaterialExpressionShadingPathSwitch::GetInputs()
{
	TArray<FExpressionInput*> OutInputs;

	OutInputs.Add(&Default);

	for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(Inputs); InputIndex++)
	{
		OutInputs.Add(&Inputs[InputIndex]);
	}

	return OutInputs;
}

FExpressionInput* UMaterialExpressionShadingPathSwitch::GetInput(int32 InputIndex)
{
	if (InputIndex == 0)
	{
		return &Default;
	}

	return &Inputs[InputIndex - 1];
}

FName UMaterialExpressionShadingPathSwitch::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		return TEXT("Default");
	}

	FName ShadingPathName;
	GetShadingPathName((ERHIShadingPath::Type)(InputIndex - 1), ShadingPathName);
	return ShadingPathName;
}

bool UMaterialExpressionShadingPathSwitch::IsInputConnectionRequired(int32 InputIndex) const
{
	return InputIndex == 0;
}

bool UMaterialExpressionShadingPathSwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	check(OutputIndex == 0);
	TArray<FExpressionInput*> ExpressionInputs = GetInputs();

	for (FExpressionInput* ExpressionInput : ExpressionInputs)
	{
		// If there is a loop anywhere in this expression's inputs then we can't risk checking them
		if (ExpressionInput->Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}

	return false;
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionNormalize
//
UMaterialExpressionNormalize::UMaterialExpressionNormalize(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
			, NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionNormalize::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!VectorInput.GetTracedInput().Expression)
		return Compiler->Errorf(TEXT("Missing Normalize input"));

	int32	V = VectorInput.Compile(Compiler);

	return Compiler->Div(V,Compiler->SquareRoot(Compiler->Dot(V,V)));
}
#endif // WITH_EDITOR

UMaterialExpressionVertexColor::UMaterialExpressionVertexColor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 0, 1));
	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVertexColor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->VertexColor();
}

void UMaterialExpressionVertexColor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Vertex Color"));
}
#endif // WITH_EDITOR

UMaterialExpressionParticleColor::UMaterialExpressionParticleColor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 0, 1));

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleColor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleColor();
}

void UMaterialExpressionParticleColor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Color"));
}
#endif // WITH_EDITOR

UMaterialExpressionParticlePositionWS::UMaterialExpressionParticlePositionWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Coordinates;
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
			, NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);
	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticlePositionWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticlePosition();
}

void UMaterialExpressionParticlePositionWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Position"));
}
#endif // WITH_EDITOR

UMaterialExpressionParticleRadius::UMaterialExpressionParticleRadius(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleRadius::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleRadius();
}

void UMaterialExpressionParticleRadius::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Radius"));
}
#endif // WITH_EDITOR

UMaterialExpressionDynamicParameter::UMaterialExpressionDynamicParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bShowOutputNameOnPin = true;
	bHidePreviewWindow = true;
#endif // WITH_EDITORONLY_DATA

	ParamNames.Add(TEXT("Param1"));
	ParamNames.Add(TEXT("Param2"));
	ParamNames.Add(TEXT("Param3"));
	ParamNames.Add(TEXT("Param4"));

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
	
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 0, 1));

	bShaderInputData = true;
#endif // WITH_EDITORONLY_DATA

	DefaultValue = FLinearColor::White;

	
	ParameterIndex = 0;
}

#if WITH_EDITOR
int32 UMaterialExpressionDynamicParameter::Compile( FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->DynamicParameter(DefaultValue, ParameterIndex);
}

TArray<FExpressionOutput>& UMaterialExpressionDynamicParameter::GetOutputs()
{
	Outputs[0].OutputName = *(ParamNames[0]);
	Outputs[1].OutputName = *(ParamNames[1]);
	Outputs[2].OutputName = *(ParamNames[2]);
	Outputs[3].OutputName = *(ParamNames[3]);
	return Outputs;
}


int32 UMaterialExpressionDynamicParameter::GetWidth() const
{
	return ME_STD_THUMBNAIL_SZ+(ME_STD_BORDER*2);
}

void UMaterialExpressionDynamicParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Dynamic Parameter"));
}

bool UMaterialExpressionDynamicParameter::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	for( int32 Index=0;Index<ParamNames.Num();Index++ )
	{
		if( ParamNames[Index].Contains(SearchQuery) )
		{
			return true;
		}
	}

	return Super::MatchesSearchQuery(SearchQuery);
}


void UMaterialExpressionDynamicParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty)
	{
		const FName PropertyName = PropertyChangedEvent.MemberProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionDynamicParameter, ParamNames))
		{
			if (GraphNode)
			{
				GraphNode->ReconstructNode();
			}
		}
	}
}

#endif // WITH_EDITOR

void UMaterialExpressionDynamicParameter::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerUE4Version() < VER_UE4_DYNAMIC_PARAMETER_DEFAULT_VALUE)
	{
		DefaultValue = FLinearColor::Black;//Old data should default to 0.0f;
	}
}

#if WITH_EDITORONLY_DATA
void UMaterialExpressionDynamicParameter::UpdateDynamicParameterProperties()
{
	check(Material);
	for (int32 ExpIndex = 0; ExpIndex < Material->Expressions.Num(); ExpIndex++)
	{
		const UMaterialExpressionDynamicParameter* DynParam = Cast<UMaterialExpressionDynamicParameter>(Material->Expressions[ExpIndex]);
		if (CopyDynamicParameterProperties(DynParam))
		{
			break;
		}
	}
}
#endif // WITH_EDITORONLY_DATA

bool UMaterialExpressionDynamicParameter::CopyDynamicParameterProperties(const UMaterialExpressionDynamicParameter* FromParam)
{
	if (FromParam && (FromParam != this) && ParameterIndex == FromParam->ParameterIndex)
	{
		for (int32 NameIndex = 0; NameIndex < 4; NameIndex++)
		{
			ParamNames[NameIndex] = FromParam->ParamNames[NameIndex];
		}
		DefaultValue = FromParam->DefaultValue;
		return true;
	}
	return false;
}

//
//	MaterialExpressionParticleSubUV
//
UMaterialExpressionParticleSubUV::UMaterialExpressionParticleSubUV(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bBlend = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Particles);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleSubUV::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Texture)
	{
		if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("ParticleSubUV")), Texture, SamplerType))
		{
			return INDEX_NONE;
		}
		int32 TextureCodeIndex = Compiler->Texture(Texture, SamplerType);
		return ParticleSubUV(Compiler, TextureCodeIndex, Texture, SamplerType, Coordinates, bBlend);
	}
	else
	{
		return Compiler->Errorf(TEXT("Missing ParticleSubUV input texture"));
	}
}

int32 UMaterialExpressionParticleSubUV::GetWidth() const
{
	return ME_STD_THUMBNAIL_SZ+(ME_STD_BORDER*2);
}

void UMaterialExpressionParticleSubUV::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle SubUV"));
}
#endif // WITH_EDITOR

//
//	MaterialExpressionParticleSubUVProperties
//
UMaterialExpressionParticleSubUVProperties::UMaterialExpressionParticleSubUVProperties(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT("Particles", "Particles"))
			, NAME_Coordinates(LOCTEXT("Coordinates", "Coordinates"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
	bShowOutputNameOnPin = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("TextureCoordinate0"), 1, 1, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("TextureCoordinate1"), 1, 1, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Blend")));
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleSubUVProperties::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleSubUVProperty(OutputIndex);
}

void UMaterialExpressionParticleSubUVProperties::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Provides direct access to properties used to implement particle UV frame animation."), 40, OutToolTip);
}

void UMaterialExpressionParticleSubUVProperties::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle SubUV Properties"));
}
#endif // WITH_EDITOR

//
//	MaterialExpressionParticleMacroUV
//
UMaterialExpressionParticleMacroUV::UMaterialExpressionParticleMacroUV(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleMacroUV::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleMacroUV();
}

void UMaterialExpressionParticleMacroUV::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle MacroUV"));
}
#endif // WITH_EDITOR

UMaterialExpressionLightVector::UMaterialExpressionLightVector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLightVector::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->LightVector();
}

void UMaterialExpressionLightVector::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Light Vector"));
}
#endif // WITH_EDITOR

UMaterialExpressionScreenPosition::UMaterialExpressionScreenPosition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
	bShowOutputNameOnPin = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("ViewportUV")));
	Outputs.Add(FExpressionOutput(TEXT("PixelPosition")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionScreenPosition::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex == 1)
	{
		return Compiler->GetPixelPosition();
	}
	return Compiler->GetViewportUV();
}

void UMaterialExpressionScreenPosition::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ScreenPosition"));
}
#endif // WITH_EDITOR

UMaterialExpressionViewProperty::UMaterialExpressionViewProperty(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
	bShowOutputNameOnPin = true;
	
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Property")));
	Outputs.Add(FExpressionOutput(TEXT("InvProperty")));
#endif

	Property = MEVP_FieldOfView;
}

#if WITH_EDITOR
int32 UMaterialExpressionViewProperty::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// To make sure any material that were correctly handling BufferUV != ViewportUV, we just lie to material
	// to make it believe ViewSize == BufferSize, so they are still compatible with SceneTextureLookup().
	// TODO: Remove MEVP_BufferSize, MEVP_ViewportOffset and do this at material load time. 
	if (Property == MEVP_BufferSize)
	{
		return Compiler->ViewProperty(MEVP_ViewSize, OutputIndex == 1);
	}
	else if (Property == MEVP_ViewportOffset)
	{
		// We don't care about OutputIndex == 1 because doesn't have any meaning and 
		// was already returning NaN on unconstrained unique view rendering.
		return Compiler->Constant2(0.0f, 0.0f);
	}

	return Compiler->ViewProperty(Property, OutputIndex == 1);
}

void UMaterialExpressionViewProperty::GetCaption(TArray<FString>& OutCaptions) const
{
	const UEnum* ViewPropertyEnum = StaticEnum<EMaterialExposedViewProperty>();
	check(ViewPropertyEnum);

	OutCaptions.Add(ViewPropertyEnum->GetDisplayNameTextByValue(Property).ToString());
}
#endif // WITH_EDITOR

UMaterialExpressionViewSize::UMaterialExpressionViewSize(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionViewSize::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ViewProperty(MEVP_ViewSize);
}

void UMaterialExpressionViewSize::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ViewSize"));
}
#endif // WITH_EDITOR

UMaterialExpressionDeltaTime::UMaterialExpressionDeltaTime(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT("Constants", "Constants"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDeltaTime::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->DeltaTime();
}

void UMaterialExpressionDeltaTime::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("DeltaTime"));
}
#endif // WITH_EDITOR

UMaterialExpressionSceneTexelSize::UMaterialExpressionSceneTexelSize(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSceneTexelSize::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// To make sure any material that were correctly handling BufferUV != ViewportUV, we just lie to material
	// to make it believe ViewSize == BufferSize, so they are still compatible with SceneTextureLookup().
	return Compiler->ViewProperty(MEVP_ViewSize, /* InvProperty = */ true);
}

void UMaterialExpressionSceneTexelSize::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SceneTexelSize"));
}
#endif // WITH_EDITOR

UMaterialExpressionSquareRoot::UMaterialExpressionSquareRoot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSquareRoot::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing square root input"));
	}
	return Compiler->SquareRoot(Input.Compile(Compiler));
}

void UMaterialExpressionSquareRoot::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Sqrt"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPixelDepth
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPixelDepth::UMaterialExpressionPixelDepth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Depth;
		FConstructorStatics()
			: NAME_Depth(LOCTEXT( "Depth", "Depth" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Depth);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPixelDepth::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// resulting index to compiled code chunk
	// add the code chunk for the pixel's depth     
	int32 Result = Compiler->PixelDepth();
	return Result;
}

void UMaterialExpressionPixelDepth::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("PixelDepth"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSceneDepth
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSceneDepth::UMaterialExpressionSceneDepth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Depth;
		FConstructorStatics()
			: NAME_Depth(LOCTEXT( "Depth", "Depth" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Depth);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	bShaderInputData = true;
#endif

	ConstInput = FVector2D(0.f, 0.f);
}

void UMaterialExpressionSceneDepth::PostLoad()
{
	Super::PostLoad();

	if(GetLinkerUE4Version() < VER_UE4_REFACTOR_MATERIAL_EXPRESSION_SCENECOLOR_AND_SCENEDEPTH_INPUTS)
	{
		// Connect deprecated UV input to new expression input
		InputMode = EMaterialSceneAttributeInputMode::Coordinates;
		Input = Coordinates_DEPRECATED;
	}
}

#if WITH_EDITOR
int32 UMaterialExpressionSceneDepth::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{    
	int32 OffsetIndex = INDEX_NONE;
	int32 CoordinateIndex = INDEX_NONE;
	bool bUseOffset = false;

	if(InputMode == EMaterialSceneAttributeInputMode::OffsetFraction)
	{
		if (Input.GetTracedInput().Expression)
		{
			OffsetIndex = Input.Compile(Compiler);
		} 
		else
		{
			OffsetIndex = Compiler->Constant2(ConstInput.X, ConstInput.Y);
		}
		bUseOffset = true;
	}
	else if(InputMode == EMaterialSceneAttributeInputMode::Coordinates)
	{
		if (Input.GetTracedInput().Expression)
		{
			CoordinateIndex = Input.Compile(Compiler);
		} 
	}

	int32 Result = Compiler->SceneDepth(OffsetIndex, CoordinateIndex, bUseOffset);
	return Result;
}

void UMaterialExpressionSceneDepth::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Scene Depth"));
}

FName UMaterialExpressionSceneDepth::GetInputName(int32 InputIndex) const
{
	if(InputIndex == 0)
	{
		// Display the current InputMode enum's display name.
		FByteProperty* InputModeProperty = FindFProperty<FByteProperty>( UMaterialExpressionSceneDepth::StaticClass(), "InputMode" );
		// Can't use GetNameByValue as GetNameStringByValue does name mangling that GetNameByValue does not
		return *InputModeProperty->Enum->GetNameStringByValue((int64)InputMode.GetValue());
	}
	return NAME_None;
}

#endif // WITH_EDITOR


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSceneTexture
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSceneTexture::UMaterialExpressionSceneTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Texture;
		FConstructorStatics()
			: NAME_Texture(LOCTEXT( "Texture", "Texture" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Texture);

	bShaderInputData = true;
	bShowOutputNameOnPin = true;
#endif

	// by default faster, most lookup are read/write the same pixel so this is ralrely needed
	bFiltered = false;

#if WITH_EDITORONLY_DATA
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Color"), 1, 1, 1, 1, 1));
	Outputs.Add(FExpressionOutput(TEXT("Size")));
	Outputs.Add(FExpressionOutput(TEXT("InvSize")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSceneTexture::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{    
	int32 ViewportUV = INDEX_NONE;

	if (Coordinates.GetTracedInput().Expression)
	{
		ViewportUV = Coordinates.Compile(Compiler);
	}

	if(OutputIndex == 0)
	{
		// Color
		return Compiler->SceneTextureLookup(ViewportUV, SceneTextureId, bFiltered);
	}
	else if(OutputIndex == 1 || OutputIndex == 2)
	{
		return Compiler->GetSceneTextureViewSize(SceneTextureId, /* InvProperty = */ OutputIndex == 2);
	}

	return Compiler->Errorf(TEXT("Invalid input parameter"));
}

void UMaterialExpressionSceneTexture::GetCaption(TArray<FString>& OutCaptions) const
{
	UEnum* Enum = StaticEnum<ESceneTextureId>();

	check(Enum);

	FString Name = Enum->GetDisplayNameTextByValue(SceneTextureId).ToString();

	OutCaptions.Add(FString(TEXT("SceneTexture:")) + Name);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSceneColor
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSceneColor::UMaterialExpressionSceneColor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Texture;
		FConstructorStatics()
			: NAME_Texture(LOCTEXT( "Texture", "Texture" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Texture);

	bShaderInputData = true;
#endif
	ConstInput = FVector2D(0.f, 0.f);
}

void UMaterialExpressionSceneColor::PostLoad()
{
	Super::PostLoad();

	if(GetLinkerUE4Version() < VER_UE4_REFACTOR_MATERIAL_EXPRESSION_SCENECOLOR_AND_SCENEDEPTH_INPUTS)
	{
		// Connect deprecated UV input to new expression input
		InputMode = EMaterialSceneAttributeInputMode::OffsetFraction;
		Input = OffsetFraction_DEPRECATED;
	}
}

#if WITH_EDITOR
int32 UMaterialExpressionSceneColor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 OffsetIndex = INDEX_NONE;
	int32 CoordinateIndex = INDEX_NONE;
	bool bUseOffset = false;


	if(InputMode == EMaterialSceneAttributeInputMode::OffsetFraction)
	{
		if (Input.GetTracedInput().Expression)
		{
			OffsetIndex = Input.Compile(Compiler);
		}
		else
		{
			OffsetIndex = Compiler->Constant2(ConstInput.X, ConstInput.Y);
		}

		bUseOffset = true;
	}
	else if(InputMode == EMaterialSceneAttributeInputMode::Coordinates)
	{
		if (Input.GetTracedInput().Expression)
		{
			CoordinateIndex = Input.Compile(Compiler);
		} 
	}	

	int32 Result = Compiler->SceneColor(OffsetIndex, CoordinateIndex, bUseOffset);
	return Result;
}

void UMaterialExpressionSceneColor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Scene Color"));
}
#endif // WITH_EDITOR

UMaterialExpressionPower::UMaterialExpressionPower(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif

	ConstExponent = 2;
}

#if WITH_EDITOR
int32 UMaterialExpressionPower::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Base.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Power Base input"));
	}

	int32 Arg1 = Base.Compile(Compiler);
	int32 Arg2 = Exponent.GetTracedInput().Expression ? Exponent.Compile(Compiler) : Compiler->Constant(ConstExponent);
	return Compiler->Power(
		Arg1,
		Arg2
		);
}

void UMaterialExpressionPower::GetCaption(TArray<FString>& OutCaptions) const
{
	FString ret = TEXT("Power");

	if (!Exponent.GetTracedInput().Expression)
	{
		ret += FString::Printf( TEXT("(X, %.4g)"), ConstExponent);
	}

	OutCaptions.Add(ret);
}

void UMaterialExpressionPower::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns the Base value raised to the power of Exponent. Base value must be positive, values less than 0 will be clamped."), 40, OutToolTip);
}
#endif // WITH_EDITOR

UMaterialExpressionLogarithm2::UMaterialExpressionLogarithm2(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLogarithm2::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!X.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Log2 X input"));
	}

	return Compiler->Logarithm2(X.Compile(Compiler));
}

void UMaterialExpressionLogarithm2::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Log2"));
}

void UMaterialExpressionLogarithm2::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns the base-2 logarithm of the input. Input should be greater than 0."), 40, OutToolTip);
}
#endif // WITH_EDITOR

UMaterialExpressionLogarithm10::UMaterialExpressionLogarithm10(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLogarithm10::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!X.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Log10 X input"));
	}

	return Compiler->Logarithm10(X.Compile(Compiler));
}

void UMaterialExpressionLogarithm10::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Log10"));
}

void UMaterialExpressionLogarithm10::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns the base-10 logarithm of the input. Input should be greater than 0."), 40, OutToolTip);
}

FName UMaterialExpressionSceneColor::GetInputName(int32 InputIndex) const
{
	if(InputIndex == 0)
	{
		// Display the current InputMode enum's display name.
		FByteProperty* InputModeProperty = FindFProperty<FByteProperty>( UMaterialExpressionSceneColor::StaticClass(), "InputMode" );
		// Can't use GetNameByValue as GetNameStringByValue does name mangling that GetNameByValue does not
		return *InputModeProperty->Enum->GetNameStringByValue((int64)InputMode.GetValue());
	}
	return NAME_None;
}
#endif // WITH_EDITOR

UMaterialExpressionIf::UMaterialExpressionIf(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif

	EqualsThreshold = 0.00001f;
	ConstB = 0.0f;
}

#if WITH_EDITOR
int32 UMaterialExpressionIf::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing If A input"));
	}
	if(!AGreaterThanB.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing If AGreaterThanB input"));
	}
	if(!ALessThanB.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing If ALessThanB input"));
	}

	int32 CompiledA = A.Compile(Compiler);
	int32 CompiledB = B.GetTracedInput().Expression ? B.Compile(Compiler) : Compiler->Constant(ConstB);

	if(Compiler->GetType(CompiledA) != MCT_Float)
	{
		return Compiler->Errorf(TEXT("If input A must be of type float."));
	}

	if(Compiler->GetType(CompiledB) != MCT_Float)
	{
		return Compiler->Errorf(TEXT("If input B must be of type float."));
	}

	int32 Arg3 = AGreaterThanB.Compile(Compiler);
	int32 Arg4 = AEqualsB.GetTracedInput().Expression ? AEqualsB.Compile(Compiler) : INDEX_NONE;
	int32 Arg5 = ALessThanB.Compile(Compiler);
	int32 ThresholdArg = Compiler->Constant(EqualsThreshold);

	if (Arg3 == INDEX_NONE)
	{
		return Compiler->Errorf(TEXT("Failed to compile AGreaterThanB input."));
	}

	if (Arg5 == INDEX_NONE)
	{
		return Compiler->Errorf(TEXT("Failed to compile ALessThanB input."));
	}

	return Compiler->If(CompiledA,CompiledB,Arg3,Arg4,Arg5,ThresholdArg);
}

void UMaterialExpressionIf::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("If"));
}

uint32 UMaterialExpressionIf::GetInputType(int32 InputIndex)
{
	// First two inputs are always float
	if (InputIndex == 0 || InputIndex == 1)
	{
		if ((A.GetTracedInput().Expression && !A.Expression->ContainsInputLoop() && A.Expression->IsResultMaterialAttributes(A.OutputIndex)) ||
			(B.GetTracedInput().Expression && !B.Expression->ContainsInputLoop() && B.Expression->IsResultMaterialAttributes(B.OutputIndex)))
		{
			return MCT_MaterialAttributes;
		}
		else if ((A.GetTracedInput().Expression && !A.Expression->ContainsInputLoop() && A.Expression->GetOutputType(0) == MCT_ShadingModel) &&
			(B.GetTracedInput().Expression && !B.Expression->ContainsInputLoop() && B.Expression->GetOutputType(0) == MCT_ShadingModel))
		{
			return MCT_ShadingModel;
		}
		else
		{
			return MCT_Float;
		}
	}

	return MCT_Unknown;
}

bool UMaterialExpressionIf::IsResultMaterialAttributes(int32 OutputIndex)
{
	if ((AGreaterThanB.GetTracedInput().Expression && !AGreaterThanB.Expression->ContainsInputLoop() && AGreaterThanB.Expression->IsResultMaterialAttributes(AGreaterThanB.OutputIndex))
		&& (!AEqualsB.GetTracedInput().Expression || (!AEqualsB.Expression->ContainsInputLoop() && AEqualsB.Expression->IsResultMaterialAttributes(AEqualsB.OutputIndex)))
		&& (ALessThanB.GetTracedInput().Expression && !ALessThanB.Expression->ContainsInputLoop() && ALessThanB.Expression->IsResultMaterialAttributes(ALessThanB.OutputIndex))
		)
	{
		return true;
	}
	else
	{
		return false;
	}
}
#endif // WITH_EDITOR

UMaterialExpressionOneMinus::UMaterialExpressionOneMinus(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionOneMinus::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing 1-x input"));
	}
	return Compiler->Sub(Compiler->Constant(1.0f),Input.Compile(Compiler));
}

void UMaterialExpressionOneMinus::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("1-x"));
}
#endif // WITH_EDITOR

UMaterialExpressionAbs::UMaterialExpressionAbs(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Math;
		FConstructorStatics()
			: NAME_Math(LOCTEXT( "Math", "Math" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Math);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionAbs::Compile( FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result=INDEX_NONE;

	if( !Input.GetTracedInput().Expression )
	{
		// an input expression must exist
		Result = Compiler->Errorf( TEXT("Missing Abs input") );
	}
	else
	{
		// evaluate the input expression first and use that as
		// the parameter for the Abs expression
		Result = Compiler->Abs( Input.Compile(Compiler) );
	}

	return Result;
}

void UMaterialExpressionAbs::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Abs"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionTransform
///////////////////////////////////////////////////////////////////////////////

static EMaterialCommonBasis GetMaterialCommonBasis(EMaterialVectorCoordTransformSource X)
{
	static const EMaterialCommonBasis ConversionTable[TRANSFORMSOURCE_MAX] = {
		MCB_Tangent,					// TRANSFORMSOURCE_Tangent
		MCB_Local,						// TRANSFORMSOURCE_Local
		MCB_World,						// TRANSFORMSOURCE_World
		MCB_View,						// TRANSFORMSOURCE_View
		MCB_Camera,						// TRANSFORMSOURCE_Camera
		MCB_MeshParticle,
	};
	return ConversionTable[X];
}

static EMaterialCommonBasis GetMaterialCommonBasis(EMaterialVectorCoordTransform X)
{
	static const EMaterialCommonBasis ConversionTable[TRANSFORM_MAX] = {
		MCB_Tangent,					// TRANSFORM_Tangent
		MCB_Local,						// TRANSFORM_Local
		MCB_World,						// TRANSFORM_World
		MCB_View,						// TRANSFORM_View
		MCB_Camera,						// TRANSFORM_Camera
		MCB_MeshParticle,
	};
	return ConversionTable[X];
}

#if WITH_EDITOR
int32 UMaterialExpressionTransform::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result = INDEX_NONE;

	if (!Input.GetTracedInput().Expression)
	{
		Result = Compiler->Errorf(TEXT("Missing Transform input vector"));
	}
	else
	{
		int32 VecInputIdx = Input.Compile(Compiler);
		const auto TransformSourceBasis = GetMaterialCommonBasis(TransformSourceType);
		const auto TransformDestBasis = GetMaterialCommonBasis(TransformType);
		Result = Compiler->TransformVector(TransformSourceBasis, TransformDestBasis, VecInputIdx);
	}

	return Result;
}

void UMaterialExpressionTransform::GetCaption(TArray<FString>& OutCaptions) const
{
#if WITH_EDITOR
	const UEnum* MVCTSEnum = StaticEnum<EMaterialVectorCoordTransformSource>();
	const UEnum* MVCTEnum = StaticEnum<EMaterialVectorCoordTransform>();
	check(MVCTSEnum);
	check(MVCTEnum);
	
	FString TransformDesc;
	TransformDesc += MVCTSEnum->GetDisplayNameTextByValue(TransformSourceType).ToString();
	TransformDesc += TEXT(" to ");
	TransformDesc += MVCTEnum->GetDisplayNameTextByValue(TransformType).ToString();
	OutCaptions.Add(TransformDesc);
#else
	OutCaptions.Add(TEXT(""));
#endif

	OutCaptions.Add(TEXT("TransformVector"));
}
#endif // WITH_EDITOR

UMaterialExpressionTransform::UMaterialExpressionTransform(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif

	TransformSourceType = TRANSFORMSOURCE_Tangent;
	TransformType = TRANSFORM_World;
}


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionTransformPosition
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionTransformPosition::UMaterialExpressionTransformPosition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif

	TransformSourceType = TRANSFORMPOSSOURCE_Local;
	TransformType = TRANSFORMPOSSOURCE_Local;
}

static EMaterialCommonBasis GetMaterialCommonBasis(EMaterialPositionTransformSource X)
{
	static const EMaterialCommonBasis ConversionTable[TRANSFORMPOSSOURCE_MAX] = {
		MCB_Local,						// TRANSFORMPOSSOURCE_Local
		MCB_World,						// TRANSFORMPOSSOURCE_World
		MCB_TranslatedWorld,			// TRANSFORMPOSSOURCE_TranslatedWorld
		MCB_View,						// TRANSFORMPOSSOURCE_View
		MCB_Camera,						// TRANSFORMPOSSOURCE_Camera
		MCB_MeshParticle,	
	};
	return ConversionTable[X];
}

#if WITH_EDITOR
int32 UMaterialExpressionTransformPosition::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result=INDEX_NONE;
	
	if( !Input.GetTracedInput().Expression )
	{
		Result = Compiler->Errorf(TEXT("Missing Transform Position input vector"));
	}
	else
	{
		int32 VecInputIdx = Input.Compile(Compiler);
		const auto TransformSourceBasis = GetMaterialCommonBasis(TransformSourceType);
		const auto TransformDestBasis = GetMaterialCommonBasis(TransformType);
		Result = Compiler->TransformPosition(TransformSourceBasis, TransformDestBasis, VecInputIdx);
	}

	return Result;
}

void UMaterialExpressionTransformPosition::GetCaption(TArray<FString>& OutCaptions) const
{
#if WITH_EDITOR
	const UEnum* MPTSEnum = StaticEnum<EMaterialPositionTransformSource>();
	check(MPTSEnum);
	
	FString TransformDesc;
	TransformDesc += MPTSEnum->GetDisplayNameTextByValue(TransformSourceType).ToString();
	TransformDesc += TEXT(" to ");
	TransformDesc += MPTSEnum->GetDisplayNameTextByValue(TransformType).ToString();
	OutCaptions.Add(TransformDesc);
#else
	OutCaptions.Add(TEXT(""));
#endif
	
	OutCaptions.Add(TEXT("TransformPosition"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionComment
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionComment::UMaterialExpressionComment(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CommentColor(FLinearColor::White)
	, FontSize(18)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR

void UMaterialExpressionComment::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty)
	{
		const FName PropertyName = PropertyChangedEvent.MemberProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionComment, Text))
		{
			if (GraphNode)
			{
				GraphNode->Modify();
				GraphNode->NodeComment = Text;
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionComment, CommentColor))
		{
			if (GraphNode)
			{
				GraphNode->Modify();
				CastChecked<UMaterialGraphNode_Comment>(GraphNode)->CommentColor = CommentColor;
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionComment, FontSize))
		{
			if (GraphNode)
			{
				GraphNode->Modify();
				CastChecked<UMaterialGraphNode_Comment>(GraphNode)->FontSize = FontSize;
			}
		}

		// Don't need to update preview after changing comments
		bNeedToUpdatePreview = false;
	}
}

bool UMaterialExpressionComment::Modify( bool bAlwaysMarkDirty/*=true*/ )
{
	bool bResult = Super::Modify(bAlwaysMarkDirty);

	// Don't need to update preview after changing comments
	bNeedToUpdatePreview = false;

	return bResult;
}

void UMaterialExpressionComment::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Comment"));
}

bool UMaterialExpressionComment::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if( Text.Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}
#endif // WITH_EDITOR

UMaterialExpressionFresnel::UMaterialExpressionFresnel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Exponent = 5.0f;
	BaseReflectFraction = 0.04f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFresnel::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// pow(1 - max(0,Normal dot Camera),Exponent) * (1 - BaseReflectFraction) + BaseReflectFraction
	//
	int32 NormalArg = Normal.GetTracedInput().Expression ? Normal.Compile(Compiler) : Compiler->PixelNormalWS();
	int32 DotArg = Compiler->Dot(NormalArg,Compiler->CameraVector());
	int32 MaxArg = Compiler->Max(Compiler->Constant(0.f),DotArg);
	int32 MinusArg = Compiler->Sub(Compiler->Constant(1.f),MaxArg);
	int32 ExponentArg = ExponentIn.GetTracedInput().Expression ? ExponentIn.Compile(Compiler) : Compiler->Constant(Exponent);
	// Compiler->Power got changed to call PositiveClampedPow instead of ClampedPow
	// Manually implement ClampedPow to maintain backwards compatibility in the case where the input normal is not normalized (length > 1)
	int32 AbsBaseArg = Compiler->Max(Compiler->Abs(MinusArg), Compiler->Constant(KINDA_SMALL_NUMBER));
	int32 PowArg = Compiler->Power(AbsBaseArg,ExponentArg);
	int32 BaseReflectFractionArg = BaseReflectFractionIn.GetTracedInput().Expression ? BaseReflectFractionIn.Compile(Compiler) : Compiler->Constant(BaseReflectFraction);
	int32 ScaleArg = Compiler->Mul(PowArg, Compiler->Sub(Compiler->Constant(1.f), BaseReflectFractionArg));
	
	return Compiler->Add(ScaleArg, BaseReflectFractionArg);
}
#endif // WITH_EDITOR

/*-----------------------------------------------------------------------------
UMaterialExpressionFontSample
-----------------------------------------------------------------------------*/
UMaterialExpressionFontSample::UMaterialExpressionFontSample(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Font;
		FText NAME_Texture;
		FConstructorStatics()
			: NAME_Font(LOCTEXT( "Font", "Font" ))
			, NAME_Texture(LOCTEXT( "Texture", "Texture" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Font);
	MenuCategories.Add(ConstructorStatics.NAME_Texture);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 0, 1));

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFontSample::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result = -1;
#if PLATFORM_EXCEPTIONS_DISABLED
	// if we can't throw the error below, attempt to thwart the error by using the default font
	if( !Font )
	{
		UE_LOG(LogMaterial, Log, TEXT("Using default font instead of real font!"));
		Font = GEngine->GetMediumFont();
		FontTexturePage = 0;
	}
	else if( !Font->Textures.IsValidIndex(FontTexturePage) )
	{
		UE_LOG(LogMaterial, Log, TEXT("Invalid font page %d. Max allowed is %d"),FontTexturePage,Font->Textures.Num());
		FontTexturePage = 0;
	}
#endif
	if( !Font )
	{
		Result = CompilerError(Compiler, TEXT("Missing input Font"));
	}
	else if( Font->FontCacheType == EFontCacheType::Runtime )
	{
		Result = CompilerError(Compiler, *FString::Printf(TEXT("Font '%s' is runtime cached, but only offline cached fonts can be sampled"), *Font->GetName()));
	}
	else if( !Font->Textures.IsValidIndex(FontTexturePage) )
	{
		Result = CompilerError(Compiler, *FString::Printf(TEXT("Invalid font page %d. Max allowed is %d"), FontTexturePage, Font->Textures.Num()));
	}
	else
	{
		UTexture* Texture = Font->Textures[FontTexturePage];
		if( !Texture )
		{
			UE_LOG(LogMaterial, Log, TEXT("Invalid font texture. Using default texture"));
			Texture = GEngine->DefaultTexture;
		}
		check(Texture);

		EMaterialSamplerType ExpectedSamplerType;
		if (Texture->CompressionSettings == TC_DistanceFieldFont)
		{
			ExpectedSamplerType = SAMPLERTYPE_DistanceFieldFont;
		}
		else
		{
			ExpectedSamplerType = Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
		}

		if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("FontSample")), Texture, ExpectedSamplerType))
		{
			return INDEX_NONE;
		}

		int32 TextureCodeIndex = Compiler->Texture(Texture, ExpectedSamplerType);
		Result = Compiler->TextureSample(
			TextureCodeIndex,
			Compiler->TextureCoordinate(0, false, false),
			ExpectedSamplerType
		);
	}
	return Result;
}

int32 UMaterialExpressionFontSample::GetWidth() const
{
	return ME_STD_THUMBNAIL_SZ+(ME_STD_BORDER*2);
}

void UMaterialExpressionFontSample::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Font Sample"));
}

bool UMaterialExpressionFontSample::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if( Font != nullptr && Font->GetName().Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}
#endif // WITH_EDITOR

UObject* UMaterialExpressionFontSample::GetReferencedTexture() const
{
	if (Font && Font->Textures.IsValidIndex(FontTexturePage))
	{
		UTexture* Texture = Font->Textures[FontTexturePage];
		return Texture;
	}

	return nullptr;
}

/*-----------------------------------------------------------------------------
UMaterialExpressionFontSampleParameter
-----------------------------------------------------------------------------*/
UMaterialExpressionFontSampleParameter::UMaterialExpressionFontSampleParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Font;
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_Font(LOCTEXT( "Font", "Font" ))
			, NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bIsParameterExpression = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Font);
	MenuCategories.Add(ConstructorStatics.NAME_Parameters);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionFontSampleParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 Result = -1;
	if( !ParameterName.IsValid() || 
		ParameterName.IsNone() || 
		!Font ||
		!Font->Textures.IsValidIndex(FontTexturePage) )
	{
		Result = UMaterialExpressionFontSample::Compile(Compiler, OutputIndex);
	}
	else 
	{
		UTexture* Texture = Font->Textures[FontTexturePage];
		if( !Texture )
		{
			UE_LOG(LogMaterial, Log, TEXT("Invalid font texture. Using default texture"));
			Texture = GEngine->DefaultTexture;
		}
		check(Texture);

		EMaterialSamplerType ExpectedSamplerType;
		if (Texture->CompressionSettings == TC_DistanceFieldFont)
		{
			ExpectedSamplerType = SAMPLERTYPE_DistanceFieldFont;
		}
		else
		{
			ExpectedSamplerType = Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
		}

		if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("FontSampleParameter")), Texture, ExpectedSamplerType))
		{
			return INDEX_NONE;
		}
		int32 TextureCodeIndex = Compiler->TextureParameter(ParameterName,Texture, ExpectedSamplerType);
		Result = Compiler->TextureSample(
			TextureCodeIndex,
			Compiler->TextureCoordinate(0, false, false),
			ExpectedSamplerType
		);
	}
	return Result;
}

void UMaterialExpressionFontSampleParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Font Param")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

void UMaterialExpressionFontSampleParameter::ValidateParameterName(const bool bAllowDuplicateName)
{
	ValidateParameterNameInternal(this, Material, bAllowDuplicateName);
}

void UMaterialExpressionFontSampleParameter::SetValueToMatchingExpression(UMaterialExpression* OtherExpression)
{
	UFont* FontValue;
	int32 FontPage;
	if (Material->GetFontParameterValue(FMaterialParameterInfo(OtherExpression->GetParameterName()), FontValue, FontPage))
	{
		Font = FontValue;
		FontTexturePage = FontPage;
		FProperty* ParamProperty = FindFProperty<FProperty>(UMaterialExpressionFontSampleParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionFontSampleParameter, Font));
		FPropertyChangedEvent PropertyChangedEvent(ParamProperty);
		PostEditChangeProperty(PropertyChangedEvent);
		ParamProperty = FindFProperty<FProperty>(UMaterialExpressionFontSampleParameter::StaticClass(), GET_MEMBER_NAME_STRING_CHECKED(UMaterialExpressionFontSampleParameter, FontTexturePage));
		FPropertyChangedEvent PageChangedEvent(ParamProperty);
		PostEditChangeProperty(PageChangedEvent);
	}
}
#endif // WITH_EDITOR

bool UMaterialExpressionFontSampleParameter::IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, UFont*& OutFontValue, int32& OutFontPage) const
{
	if (ParameterInfo.Name == ParameterName)
	{
		OutFontValue = Font;
		OutFontPage = FontTexturePage;
		return true;
	}

	return false;
}

#if WITH_EDITOR
bool UMaterialExpressionFontSampleParameter::SetParameterValue(FName InParameterName, UFont* InFontValue, int32 InFontPage)
{
	if (InParameterName == ParameterName)
	{
		Font = InFontValue;
		FontTexturePage = InFontPage;
		return true;
	}

	return false;
}
#endif

void UMaterialExpressionFontSampleParameter::SetDefaultFont()
{
	GEngine->GetMediumFont();
}

#if WITH_EDITOR

bool UMaterialExpressionFontSampleParameter::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if( ParameterName.ToString().Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

FString UMaterialExpressionFontSampleParameter::GetEditableName() const
{
	return ParameterName.ToString();
}

void UMaterialExpressionFontSampleParameter::SetEditableName(const FString& NewName)
{
	ParameterName = *NewName;
}
#endif

void UMaterialExpressionFontSampleParameter::GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const
{
	int32 CurrentSize = OutParameterInfo.Num();
	FMaterialParameterInfo NewParameter(ParameterName, InBaseParameterInfo.Association, InBaseParameterInfo.Index);
#if WITH_EDITOR
	if (HasConnectedOutputs())
#endif
	{
		OutParameterInfo.AddUnique(NewParameter);
		if (CurrentSize != OutParameterInfo.Num())
		{
			OutParameterIds.Add(ExpressionGUID);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionWorldPosition
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionWorldPosition::UMaterialExpressionWorldPosition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
	WorldPositionShaderOffset = WPT_Default;
}

#if WITH_EDITOR
int32 UMaterialExpressionWorldPosition::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// TODO: should use a separate check box for Including/Excluding Material Shader Offsets
	return Compiler->WorldPosition(WorldPositionShaderOffset);
}

void UMaterialExpressionWorldPosition::GetCaption(TArray<FString>& OutCaptions) const
{
	switch (WorldPositionShaderOffset)
	{
	case WPT_Default:
		{
			OutCaptions.Add(NSLOCTEXT("MaterialExpressions", "WorldPositonText", "Absolute World Position").ToString());
			break;
		}

	case WPT_ExcludeAllShaderOffsets:
		{
			OutCaptions.Add(NSLOCTEXT("MaterialExpressions", "WorldPositonExcludingOffsetsText", "Absolute World Position (Excluding Material Offsets)").ToString());
			break;
		}

	case WPT_CameraRelative:
		{
			OutCaptions.Add(NSLOCTEXT("MaterialExpressions", "CamRelativeWorldPositonText", "Camera Relative World Position").ToString());
			break;
		}

	case WPT_CameraRelativeNoOffsets:
		{
			OutCaptions.Add(NSLOCTEXT("MaterialExpressions", "CamRelativeWorldPositonExcludingOffsetsText", "Camera Relative World Position (Excluding Material Offsets)").ToString());
			break;
		}

	default:
		{
			UE_LOG(LogMaterial, Fatal, TEXT("Unknown world position shader offset type"));
			break;
		}
	}
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionObjectPositionWS
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionObjectPositionWS::UMaterialExpressionObjectPositionWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionObjectPositionWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material && Material->MaterialDomain == MD_DeferredDecal)
	{
		return CompilerError(Compiler, TEXT("Expression not available in the deferred decal material domain."));
	}

	return Compiler->ObjectWorldPosition();
}

void UMaterialExpressionObjectPositionWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Object Position"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionObjectRadius
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionObjectRadius::UMaterialExpressionObjectRadius(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionObjectRadius::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material && Material->MaterialDomain == MD_DeferredDecal)
	{
		return CompilerError(Compiler, TEXT("Expression not available in the deferred decal material domain."));
	}

	return Compiler->ObjectRadius();
}

void UMaterialExpressionObjectRadius::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Object Radius"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionObjectBoundingBox
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionObjectBounds::UMaterialExpressionObjectBounds(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionObjectBounds::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material && Material->MaterialDomain == MD_DeferredDecal)
	{
		return CompilerError(Compiler, TEXT("Expression not available in the deferred decal material domain."));
	}

	return Compiler->ObjectBounds();
}

void UMaterialExpressionObjectBounds::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Object Bounds"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPreSkinnedLocalBounds
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPreSkinnedLocalBounds::UMaterialExpressionPreSkinnedLocalBounds(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT("Vectors", "Vectors"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;
	bShowOutputNameOnPin = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Half Extents"), 1, 1, 1, 1, 0));
	OutputToolTips.Add("Half the extent (width, depth and height) of the pre-skinned bounding box. In local space.");
	Outputs.Add(FExpressionOutput(TEXT("Extents"), 1, 1, 1, 1, 0));
	OutputToolTips.Add("Full extent (width, depth and height) of the pre-skinned bounding box. Same as 2x Half Extents. In local space.");
	Outputs.Add(FExpressionOutput(TEXT("Min"), 1, 1, 1, 1, 0));
	OutputToolTips.Add("Minimum 3D point of the pre-skinned bounding box. In local space.");
	Outputs.Add(FExpressionOutput(TEXT("Max"), 1, 1, 1, 1, 0));
	OutputToolTips.Add("Maximum 3D point of the pre-skinned bounding box. In local space.");
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPreSkinnedLocalBounds::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material && Material->MaterialDomain == MD_DeferredDecal)
	{
		return CompilerError(Compiler, TEXT("Expression not available in the deferred decal material domain."));
	}

	return Compiler->PreSkinnedLocalBounds(OutputIndex);
}

void UMaterialExpressionPreSkinnedLocalBounds::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Pre-Skinned Local Bounds"));
}

void UMaterialExpressionPreSkinnedLocalBounds::GetConnectorToolTip(int32 InputIndex, int32 OutputIndex, TArray<FString>& OutToolTip) 
{
#if WITH_EDITORONLY_DATA
	if (OutputIndex >= 0 && OutputIndex < OutputToolTips.Num())
	{
		ConvertToMultilineToolTip(OutputToolTips[OutputIndex], 40, OutToolTip);
	}
#endif // WITH_EDITORONLY_DATA
}

void UMaterialExpressionPreSkinnedLocalBounds::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns various info about the pre-skinned local bounding box for skeletal meshes."
		"Will return the regular local space bounding box for static meshes."
		"Usable in vertex or pixel shader (no need to pipe this through vertex interpolators)."
		"Hover the output pins for more information."), 40, OutToolTip);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDistanceCullFade
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDistanceCullFade::UMaterialExpressionDistanceCullFade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDistanceCullFade::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->DistanceCullFade();
}

void UMaterialExpressionDistanceCullFade::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Distance Cull Fade"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDistanceFieldsRenderingSwitch
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionDistanceFieldsRenderingSwitch::UMaterialExpressionDistanceFieldsRenderingSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_DistanceFieldsRendering;
		FConstructorStatics()
			: NAME_DistanceFieldsRendering(LOCTEXT("DistanceFieldsRendering", "DistanceFieldsRendering"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_DistanceFieldsRendering);
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionDistanceFieldsRenderingSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Yes.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing DistanceFieldsRenderingSwitch input 'Yes'"));
	}

	if (!No.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing DistanceFieldsRenderingSwitch input 'No'"));
	}

	if (IsUsingDistanceFields(Compiler->GetShaderPlatform()))
	{
		return Yes.Compile(Compiler);
	}

	return No.Compile(Compiler);
}

bool UMaterialExpressionDistanceFieldsRenderingSwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	for (FExpressionInput* ExpressionInput : GetInputs())
	{
		if (ExpressionInput->GetTracedInput().Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}
	return false;
}

void UMaterialExpressionDistanceFieldsRenderingSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("DistanceFieldsRenderingSwitch"));
}

#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionActorPositionWS
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionActorPositionWS::UMaterialExpressionActorPositionWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionActorPositionWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material != nullptr && (Material->MaterialDomain != MD_Surface) && (Material->MaterialDomain != MD_DeferredDecal) && (Material->MaterialDomain != MD_Volume))
	{
		return CompilerError(Compiler, TEXT("Expression only available in the Surface and Deferred Decal material domains."));
	}

	return Compiler->ActorWorldPosition();
}

void UMaterialExpressionActorPositionWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Actor Position"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDeriveNormalZ
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDeriveNormalZ::UMaterialExpressionDeriveNormalZ(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_VectorOps(LOCTEXT( "VectorOps", "VectorOps" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}
	
#if WITH_EDITOR
int32 UMaterialExpressionDeriveNormalZ::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!InXY.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input normal xy vector whose z should be derived."));
	}

	//z = sqrt(1 - ( x * x + y * y));
	int32 InputVector = Compiler->ForceCast(InXY.Compile(Compiler), MCT_Float2);
	int32 DotResult = Compiler->Dot(InputVector, InputVector);
	int32 InnerResult = Compiler->Sub(Compiler->Constant(1), DotResult);
	int32 DerivedZ = Compiler->SquareRoot(InnerResult);
	int32 AppendedResult = Compiler->ForceCast(Compiler->AppendVector(InputVector, DerivedZ), MCT_Float3);

	return AppendedResult;
}

void UMaterialExpressionDeriveNormalZ::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("DeriveNormalZ"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionConstantBiasScale
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionConstantBiasScale::UMaterialExpressionConstantBiasScale(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Bias = 1.0f;
	Scale = 0.5f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionConstantBiasScale::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing ConstantBiasScale input"));
	}

	return Compiler->Mul(Compiler->Add(Compiler->Constant(Bias), Input.Compile(Compiler)), Compiler->Constant(Scale));
}


void UMaterialExpressionConstantBiasScale::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ConstantBiasScale"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionCustom
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionCustom::UMaterialExpressionCustom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Custom;
		FConstructorStatics()
			: NAME_Custom(LOCTEXT( "Custom", "Custom" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#endif // WITH_EDITORONLY_DATA

	Description = TEXT("Custom");
	Code = TEXT("1");

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Custom);
#endif

	OutputType = CMOT_Float3;

	Inputs.Add(FCustomInput());
	Inputs[0].InputName = TEXT("");

#if WITH_EDITORONLY_DATA
	bCollapsed = false;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionCustom::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	TArray<int32> CompiledInputs;

	for( int32 i=0;i<Inputs.Num();i++ )
	{
		// skip over unnamed inputs
		if( Inputs[i].InputName.IsNone() )
		{
			CompiledInputs.Add(INDEX_NONE);
		}
		else
		{
			if(!Inputs[i].Input.GetTracedInput().Expression)
			{
				return Compiler->Errorf(TEXT("Custom material %s missing input %d (%s)"), *Description, i+1, *Inputs[i].InputName.ToString());
			}
			int32 InputCode = Inputs[i].Input.Compile(Compiler);
			if( InputCode < 0 )
			{
				return InputCode;
			}
			CompiledInputs.Add( InputCode );
		}
	}

	return Compiler->CustomExpression(this, OutputIndex, CompiledInputs);
}


void UMaterialExpressionCustom::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(Description);
}


const TArray<FExpressionInput*> UMaterialExpressionCustom::GetInputs()
{
	TArray<FExpressionInput*> Result;
	for( int32 i = 0; i < Inputs.Num(); i++ )
	{
		Result.Add(&Inputs[i].Input);
	}
	return Result;
}

FExpressionInput* UMaterialExpressionCustom::GetInput(int32 InputIndex)
{
	if( InputIndex < Inputs.Num() )
	{
		return &Inputs[InputIndex].Input;
	}
	return nullptr;
}

FName UMaterialExpressionCustom::GetInputName(int32 InputIndex) const
{
	if( InputIndex < Inputs.Num() )
	{
		return Inputs[InputIndex].InputName;
	}
	return NAME_None;
}

void UMaterialExpressionCustom::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// strip any spaces from input name
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if( PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(FCustomInput, InputName))
	{
		for( FCustomInput& Input : Inputs )
	{
			FString InputName = Input.InputName.ToString();
			if (InputName.ReplaceInline(TEXT(" "),TEXT("")) > 0)
		{
				Input.InputName = *InputName;
			}
		}
	}

	RebuildOutputs();

	if (PropertyChangedEvent.MemberProperty && GraphNode)
	{
		const FName PropertyName = PropertyChangedEvent.MemberProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, Inputs) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionCustom, AdditionalOutputs))
		{
			GraphNode->ReconstructNode();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialExpressionCustom::RebuildOutputs()
{
	Outputs.Reset(AdditionalOutputs.Num() + 1);
	if (AdditionalOutputs.Num() == 0)
	{
		bShowOutputNameOnPin = false;
		Outputs.Add(FExpressionOutput(TEXT("")));
	}
	else
	{
		bShowOutputNameOnPin = true;
		Outputs.Add(FExpressionOutput(TEXT("return")));
		for (const FCustomOutput& CustomOutput : AdditionalOutputs)
		{
			if (!CustomOutput.OutputName.IsNone())
			{
				Outputs.Add(FExpressionOutput(CustomOutput.OutputName));
			}
		}
	}
}

uint32 UMaterialExpressionCustom::GetOutputType(int32 OutputIndex)
{
	ECustomMaterialOutputType Type = CMOT_MAX;
	if (OutputIndex == 0)
	{
		Type = OutputType;
	}
	else if (OutputIndex >= 1 && OutputIndex - 1 < AdditionalOutputs.Num())
	{
		Type = AdditionalOutputs[OutputIndex - 1].OutputType;
	}

	switch (Type)
	{
	case CMOT_Float1:
		return MCT_Float;
	case CMOT_Float2:
		return MCT_Float2;
	case CMOT_Float3:
		return MCT_Float3;
	case CMOT_Float4:
		return MCT_Float4;
	case CMOT_MaterialAttributes:
		return MCT_MaterialAttributes;
	default:
		return MCT_Unknown;
	}
}

bool UMaterialExpressionCustom::IsResultMaterialAttributes(int32 OutputIndex)
{
	return GetOutputType(OutputIndex) == MCT_MaterialAttributes;
}
#endif // WITH_EDITOR

void UMaterialExpressionCustom::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();

	UnderlyingArchive.UsingCustomVersion(FRenderingObjectVersion::GUID);

	// Make a copy of the current code before we change it
	const FString PreFixUp = Code;

	bool bDidUpdate = false;

	if (UnderlyingArchive.UE4Ver() < VER_UE4_INSTANCED_STEREO_UNIFORM_UPDATE)
	{
		// Look for WorldPosition rename
		if (Code.ReplaceInline(TEXT("Parameters.WorldPosition"), TEXT("Parameters.AbsoluteWorldPosition"), ESearchCase::CaseSensitive) > 0)
		{
			bDidUpdate = true;
		}
	}
	// Fix up uniform references that were moved from View to Frame as part of the instanced stereo implementation
	else if (UnderlyingArchive.UE4Ver() < VER_UE4_INSTANCED_STEREO_UNIFORM_REFACTOR)
	{
		// Uniform members that were moved from View to Frame
		static const FString UniformMembers[] = {
			FString(TEXT("FieldOfViewWideAngles")),
			FString(TEXT("PrevFieldOfViewWideAngles")),
			FString(TEXT("ViewRectMin")),
			FString(TEXT("ViewSizeAndInvSize")),
			FString(TEXT("BufferSizeAndInvSize")),
			FString(TEXT("ExposureScale")),
			FString(TEXT("DiffuseOverrideParameter")),
			FString(TEXT("SpecularOverrideParameter")),
			FString(TEXT("NormalOverrideParameter")),
			FString(TEXT("RoughnessOverrideParameter")),
			FString(TEXT("PrevFrameGameTime")),
			FString(TEXT("PrevFrameRealTime")),
			FString(TEXT("OutOfBoundsMask")),
			FString(TEXT("WorldCameraMovementSinceLastFrame")),
			FString(TEXT("CullingSign")),
			FString(TEXT("NearPlane")),
			FString(TEXT("AdaptiveTessellationFactor")),
			FString(TEXT("GameTime")),
			FString(TEXT("RealTime")),
			FString(TEXT("Random")),
			FString(TEXT("FrameNumber")),
			FString(TEXT("CameraCut")),
			FString(TEXT("UseLightmaps")),
			FString(TEXT("UnlitViewmodeMask")),
			FString(TEXT("DirectionalLightColor")),
			FString(TEXT("DirectionalLightDirection")),
			FString(TEXT("DirectionalLightShadowTransition")),
			FString(TEXT("DirectionalLightShadowSize")),
			FString(TEXT("DirectionalLightScreenToShadow")),
			FString(TEXT("DirectionalLightShadowDistances")),
			FString(TEXT("UpperSkyColor")),
			FString(TEXT("LowerSkyColor")),
			FString(TEXT("TranslucencyLightingVolumeMin")),
			FString(TEXT("TranslucencyLightingVolumeInvSize")),
			FString(TEXT("TemporalAAParams")),
			FString(TEXT("CircleDOFParams")),
			FString(TEXT("DepthOfFieldFocalDistance")),
			FString(TEXT("DepthOfFieldScale")),
			FString(TEXT("DepthOfFieldFocalLength")),
			FString(TEXT("DepthOfFieldFocalRegion")),
			FString(TEXT("DepthOfFieldNearTransitionRegion")),
			FString(TEXT("DepthOfFieldFarTransitionRegion")),
			FString(TEXT("MotionBlurNormalizedToPixel")),
			FString(TEXT("GeneralPurposeTweak")),
			FString(TEXT("DemosaicVposOffset")),
			FString(TEXT("IndirectLightingColorScale")),
			FString(TEXT("HDR32bppEncodingMode")),
			FString(TEXT("AtmosphericFogSunDirection")),
			FString(TEXT("AtmosphericFogSunPower")),
			FString(TEXT("AtmosphericFogPower")),
			FString(TEXT("AtmosphericFogDensityScale")),
			FString(TEXT("AtmosphericFogDensityOffset")),
			FString(TEXT("AtmosphericFogGroundOffset")),
			FString(TEXT("AtmosphericFogDistanceScale")),
			FString(TEXT("AtmosphericFogAltitudeScale")),
			FString(TEXT("AtmosphericFogHeightScaleRayleigh")),
			FString(TEXT("AtmosphericFogStartDistance")),
			FString(TEXT("AtmosphericFogDistanceOffset")),
			FString(TEXT("AtmosphericFogSunDiscScale")),
			FString(TEXT("AtmosphericFogRenderMask")),
			FString(TEXT("AtmosphericFogInscatterAltitudeSampleNum")),
			FString(TEXT("AtmosphericFogSunColor")),
			FString(TEXT("AmbientCubemapTint")),
			FString(TEXT("AmbientCubemapIntensity")),
			FString(TEXT("RenderTargetSize")),
			FString(TEXT("SkyLightParameters")),
			FString(TEXT("SceneFString(TEXTureMinMax")),
			FString(TEXT("SkyLightColor")),
			FString(TEXT("SkyIrradianceEnvironmentMap")),
			FString(TEXT("MobilePreviewMode")),
			FString(TEXT("HMDEyePaddingOffset")),
			FString(TEXT("DirectionalLightShadowFString(TEXTure")),
			FString(TEXT("SamplerState")),
		};

		const FString ViewUniformName(TEXT("View."));
		const FString FrameUniformName(TEXT("Frame."));
		for (const FString& Member : UniformMembers)
		{
			const FString SearchString = FrameUniformName + Member;
			const FString ReplaceString = ViewUniformName + Member;
			if (Code.ReplaceInline(*SearchString, *ReplaceString, ESearchCase::CaseSensitive) > 0)
			{
				bDidUpdate = true;
			}
		}
	}

	if (UnderlyingArchive.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::RemovedRenderTargetSize)
	{
		if (Code.ReplaceInline(TEXT("View.RenderTargetSize"), TEXT("View.BufferSizeAndInvSize.xy"), ESearchCase::CaseSensitive) > 0)
		{
			bDidUpdate = true;
		}
	}

#if WITH_EDITORONLY_DATA
	// If we made changes, copy the original into the description just in case
	if (bDidUpdate)
	{
		Desc += TEXT("\n*** Original source before expression upgrade ***\n");
		Desc += PreFixUp;
		UE_LOG(LogMaterial, Log, TEXT("Uniform references updated for custom material expression %s."), *Description);
	}
#endif // WITH_EDITORONLY_DATA
}

///////////////////////////////////////////////////////////////////////////////
// UMaterialFunctionInterface
///////////////////////////////////////////////////////////////////////////////
UMaterialFunctionInterface::UMaterialFunctionInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaterialFunctionUsage(EMaterialFunctionUsage::Default)
{
}

void UMaterialFunctionInterface::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

#if WITH_EDITORONLY_DATA
	for (FAssetRegistryTag& AssetTag : OutTags)
	{
		// Hide the combined input/output types as they are only needed in code
		if (AssetTag.Name == GET_MEMBER_NAME_CHECKED(UMaterialFunctionInterface, CombinedInputTypes)
		|| AssetTag.Name == GET_MEMBER_NAME_CHECKED(UMaterialFunctionInterface, CombinedOutputTypes))
		{
			AssetTag.Type = UObject::FAssetRegistryTag::TT_Hidden;
		}
	}
#endif
}

#if WITH_EDITOR
void UMaterialFunctionInterface::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Go through all materials in memory and recompile them if they use this function
	FMaterialUpdateContext UpdateContext;
	for (TObjectIterator<UMaterial> It; It; ++It)
	{
		UMaterial* CurrentMaterial = *It;

		bool bRecompile = false;
		for (const FMaterialFunctionInfo& FunctionInfo : CurrentMaterial->GetCachedExpressionData().FunctionInfos)
		{
			if (FunctionInfo.Function == this)
			{
				bRecompile = true;
				break;
			}
		}

		if (bRecompile)
		{
			UpdateContext.AddMaterial(CurrentMaterial);

			// Propagate the change to this material
			CurrentMaterial->PreEditChange(nullptr);
			CurrentMaterial->PostEditChange();
			CurrentMaterial->MarkPackageDirty();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialFunctionMaterialLayer
///////////////////////////////////////////////////////////////////////////////
UMaterialFunctionMaterialLayer::UMaterialFunctionMaterialLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

///////////////////////////////////////////////////////////////////////////////
// UMaterialFunctionMaterialLayerBlend
///////////////////////////////////////////////////////////////////////////////
UMaterialFunctionMaterialLayerBlend::UMaterialFunctionMaterialLayerBlend(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


///////////////////////////////////////////////////////////////////////////////
// UMaterialFunctionMaterialLayerInstance
///////////////////////////////////////////////////////////////////////////////
UMaterialFunctionMaterialLayerInstance::UMaterialFunctionMaterialLayerInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

///////////////////////////////////////////////////////////////////////////////
// UMaterialFunctionMaterialLayerBlendInstance
///////////////////////////////////////////////////////////////////////////////
UMaterialFunctionMaterialLayerBlendInstance::UMaterialFunctionMaterialLayerBlendInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

///////////////////////////////////////////////////////////////////////////////
// UMaterialFunction
///////////////////////////////////////////////////////////////////////////////
UMaterialFunction::UMaterialFunction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	LibraryCategoriesText.Add(LOCTEXT("Misc", "Misc"));
#endif
#if WITH_EDITORONLY_DATA
	PreviewMaterial = nullptr;
	ThumbnailInfo = nullptr;
#endif
}

#if WITH_EDITOR
UMaterialInterface* UMaterialFunction::GetPreviewMaterial()
{
	if( nullptr == PreviewMaterial )
	{
		PreviewMaterial = NewObject<UMaterial>(this, NAME_None, RF_Transient | RF_Public);

		PreviewMaterial->Expressions = FunctionExpressions;

		//Find the first output expression and use that. 
		for( int32 i=0; i < FunctionExpressions.Num() ; ++i)
		{
			UMaterialExpressionFunctionOutput* Output = Cast<UMaterialExpressionFunctionOutput>(FunctionExpressions[i]);
			if( Output )
			{
				Output->ConnectToPreviewMaterial(PreviewMaterial, 0);
			}
		}

		//Compile the material.
		PreviewMaterial->PreEditChange(nullptr);
		PreviewMaterial->PostEditChange();
	}
	return PreviewMaterial;
}

void UMaterialFunction::UpdateInputOutputTypes()
{
	CombinedInputTypes = 0;
	CombinedOutputTypes = 0;

	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = FunctionExpressions[ExpressionIndex];
		UMaterialExpressionFunctionOutput* OutputExpression = Cast<UMaterialExpressionFunctionOutput>(CurrentExpression);
		UMaterialExpressionFunctionInput* InputExpression = Cast<UMaterialExpressionFunctionInput>(CurrentExpression);

		if (InputExpression)
		{
			CombinedInputTypes |= InputExpression->GetInputType(0);
		}
		else if (OutputExpression)
		{
			CombinedOutputTypes |= OutputExpression->GetOutputType(0);
		}
	}
}

#if WITH_EDITOR
void UMaterialFunction::UpdateDependentFunctionCandidates()
{
	DependentFunctionExpressionCandidates.Reset();
	for (UMaterialExpression* CurrentExpression : FunctionExpressions)
	{
		if (UMaterialExpressionMaterialFunctionCall* MaterialFunctionExpression = Cast<UMaterialExpressionMaterialFunctionCall>(CurrentExpression))
		{
			DependentFunctionExpressionCandidates.Add(MaterialFunctionExpression);
		}
	}
}
#endif

void UMaterialFunction::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
#if WITH_EDITORONLY_DATA
	UpdateInputOutputTypes();
	UpdateDependentFunctionCandidates();
#endif

	//@todo - recreate guid only when needed, not when a comment changes
	StateId = FGuid::NewGuid();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void UMaterialFunction::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.UE4Ver() < VER_UE4_FLIP_MATERIAL_COORDS)
	{
		GMaterialFunctionsThatNeedExpressionsFlipped.Set(this);
	}
	else if (Ar.UE4Ver() < VER_UE4_FIX_MATERIAL_COORDS)
	{
		GMaterialFunctionsThatNeedCoordinateCheck.Set(this);
	}
	else if (Ar.UE4Ver() < VER_UE4_FIX_MATERIAL_COMMENTS)
	{
		GMaterialFunctionsThatNeedCommentFix.Set(this);
	}

	if (Ar.UE4Ver() < VER_UE4_ADD_LINEAR_COLOR_SAMPLER)
	{
		GMaterialFunctionsThatNeedSamplerFixup.Set(this);
	}

	if (Ar.UE4Ver() < VER_UE4_LIBRARY_CATEGORIES_AS_FTEXT)
	{
		for (FString& Category : LibraryCategories_DEPRECATED)
		{
			LibraryCategoriesText.Add(FText::FromString(Category));
		}
	}
#endif // #if WITH_EDITOR
}

void UMaterialFunction::PostLoad()
{
	LLM_SCOPE(ELLMTag::Materials);

	Super::PostLoad();
	
	if (!StateId.IsValid())
	{
		StateId = FGuid::NewGuid();
	}

#if WITH_EDITORONLY_DATA
	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		// Expressions whose type was removed can be nullptr
		if (FunctionExpressions[ExpressionIndex])
		{
			FunctionExpressions[ExpressionIndex]->ConditionalPostLoad();
		}
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	if (CombinedOutputTypes == 0)
	{
		UpdateInputOutputTypes();
	}
	UpdateDependentFunctionCandidates();

	if (GIsEditor)
	{
		// Clean up any removed material expression classes	
		if (FunctionExpressions.Remove(nullptr) != 0)
		{
			// Force this function to recompile because its expressions have changed
			// Warning: any content taking this path will recompile every load until saved!
			// Which means removing an expression class will cause the need for a resave of all materials affected
			StateId = FGuid::NewGuid();
		}
	}

	if (GMaterialFunctionsThatNeedExpressionsFlipped.Get(this))
	{
		GMaterialFunctionsThatNeedExpressionsFlipped.Clear(this);
		UMaterial::FlipExpressionPositions(FunctionExpressions, FunctionEditorComments, true);
	}
	else if (GMaterialFunctionsThatNeedCoordinateCheck.Get(this))
	{
		GMaterialFunctionsThatNeedCoordinateCheck.Clear(this);
		if (HasFlippedCoordinates())
		{
			UMaterial::FlipExpressionPositions(FunctionExpressions, FunctionEditorComments, false);
		}
		UMaterial::FixCommentPositions(FunctionEditorComments);
	}
	else if (GMaterialFunctionsThatNeedCommentFix.Get(this))
	{
		GMaterialFunctionsThatNeedCommentFix.Clear(this);
		UMaterial::FixCommentPositions(FunctionEditorComments);
	}

	if (GMaterialFunctionsThatNeedSamplerFixup.Get(this))
	{
		GMaterialFunctionsThatNeedSamplerFixup.Clear(this);
		const int32 ExpressionCount = FunctionExpressions.Num();
		for (int32 ExpressionIndex = 0; ExpressionIndex < ExpressionCount; ++ExpressionIndex)
		{
			UMaterialExpressionTextureBase* TextureExpression = Cast<UMaterialExpressionTextureBase>(FunctionExpressions[ExpressionIndex]);
			if (TextureExpression && TextureExpression->Texture)
			{
				switch (TextureExpression->Texture->CompressionSettings)
				{
				case TC_Normalmap:
					TextureExpression->SamplerType = SAMPLERTYPE_Normal;
					break;
				case TC_Grayscale:
					TextureExpression->SamplerType = TextureExpression->Texture->SRGB ? SAMPLERTYPE_Grayscale : SAMPLERTYPE_LinearGrayscale;
					break;

				case TC_Masks:
					TextureExpression->SamplerType = SAMPLERTYPE_Masks;
					break;

				case TC_Alpha:
					TextureExpression->SamplerType = SAMPLERTYPE_Alpha;
					break;
				default:
					TextureExpression->SamplerType = TextureExpression->Texture->SRGB ? SAMPLERTYPE_Color : SAMPLERTYPE_LinearColor;
					break;
				}
			}
		}
	}
#endif // #if WITH_EDITOR
}

#if WITH_EDITOR

void UMaterialFunction::UpdateFromFunctionResource()
{
	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = FunctionExpressions[ExpressionIndex];
		UMaterialExpressionMaterialFunctionCall* MaterialFunctionExpression = Cast<UMaterialExpressionMaterialFunctionCall>(CurrentExpression);
		if (MaterialFunctionExpression)
		{
			MaterialFunctionExpression->UpdateFromFunctionResource();
		}
	}
}

void UMaterialFunction::GetInputsAndOutputs(TArray<FFunctionExpressionInput>& OutInputs, TArray<FFunctionExpressionOutput>& OutOutputs) const
{
	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = FunctionExpressions[ExpressionIndex];
		UMaterialExpressionFunctionOutput* OutputExpression = Cast<UMaterialExpressionFunctionOutput>(CurrentExpression);
		UMaterialExpressionFunctionInput* InputExpression = Cast<UMaterialExpressionFunctionInput>(CurrentExpression);

		if (InputExpression)
		{
			// Create an input
			FFunctionExpressionInput NewInput;
			NewInput.ExpressionInput = InputExpression;
			NewInput.ExpressionInputId = InputExpression->Id;
			NewInput.Input.InputName = InputExpression->InputName;
			NewInput.Input.OutputIndex = INDEX_NONE;
			OutInputs.Add(NewInput);
		}
		else if (OutputExpression)
		{
			// Create an output
			FFunctionExpressionOutput NewOutput;
			NewOutput.ExpressionOutput = OutputExpression;
			NewOutput.ExpressionOutputId = OutputExpression->Id;
			NewOutput.Output.OutputName = OutputExpression->OutputName;
			OutOutputs.Add(NewOutput);
		}
	}

	// Sort by display priority
	struct FCompareInputSortPriority
	{
		FORCEINLINE bool operator()( const FFunctionExpressionInput& A, const FFunctionExpressionInput& B ) const 
		{ 
			return A.ExpressionInput->SortPriority < B.ExpressionInput->SortPriority; 
		}
	};
	OutInputs.Sort( FCompareInputSortPriority() );

	struct FCompareOutputSortPriority
	{
		FORCEINLINE bool operator()( const FFunctionExpressionOutput& A, const FFunctionExpressionOutput& B ) const 
		{ 
			return A.ExpressionOutput->SortPriority < B.ExpressionOutput->SortPriority; 
		}
	};
	OutOutputs.Sort( FCompareOutputSortPriority() );
}

/** Finds an input in the passed in array with a matching Id. */
static const FFunctionExpressionInput* FindInputById(const FGuid& Id, const TArray<FFunctionExpressionInput>& Inputs)
{
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); InputIndex++)
	{
		const FFunctionExpressionInput& CurrentInput = Inputs[InputIndex];
		if (CurrentInput.ExpressionInputId == Id)
		{
			return &CurrentInput;
		}
	}
	return nullptr;
}

/** Finds an input in the passed in array with a matching name. */
static const FFunctionExpressionInput* FindInputByName(const FName& Name, const TArray<FFunctionExpressionInput>& Inputs)
{
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); InputIndex++)
	{
		const FFunctionExpressionInput& CurrentInput = Inputs[InputIndex];
		if (CurrentInput.ExpressionInput->InputName == Name)
		{
			return &CurrentInput;
		}
	}
	return nullptr;
}

/** Finds an input in the passed in array with a matching expression object. */
static const FExpressionInput* FindInputByExpression(UMaterialExpressionFunctionInput* InputExpression, const TArray<FFunctionExpressionInput>& Inputs)
{
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); InputIndex++)
	{
		const FFunctionExpressionInput& CurrentInput = Inputs[InputIndex];
		if (CurrentInput.ExpressionInput == InputExpression)
		{
			return &CurrentInput.Input;
		}
	}
	return nullptr;
}

/** Finds an output in the passed in array with a matching Id. */
static int32 FindOutputIndexById(const FGuid& Id, const TArray<FFunctionExpressionOutput>& Outputs)
{
	for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); OutputIndex++)
	{
		const FFunctionExpressionOutput& CurrentOutput = Outputs[OutputIndex];
		if (CurrentOutput.ExpressionOutputId == Id)
		{
			return OutputIndex;
		}
	}
	return INDEX_NONE;
}

/** Finds an output in the passed in array with a matching name. */
static int32 FindOutputIndexByName(const FName& Name, const TArray<FFunctionExpressionOutput>& Outputs)
{
	for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); OutputIndex++)
	{
		const FFunctionExpressionOutput& CurrentOutput = Outputs[OutputIndex];
		if (CurrentOutput.ExpressionOutput->OutputName == Name)
		{
			return OutputIndex;
		}
	}
	return INDEX_NONE;
}
#endif

bool UMaterialFunction::ValidateFunctionUsage(FMaterialCompiler* Compiler, const FFunctionExpressionOutput& Output)
{
	bool bHasValidOutput = true;
	int32 NumInputs = 0;
	int32 NumOutputs = 0;

#if WITH_EDITOR
	if (GetMaterialFunctionUsage() == EMaterialFunctionUsage::MaterialLayer)
	{
		// Material layers must have a single MA input and output only
		for (UMaterialExpression* Expression : FunctionExpressions)
		{
			if (UMaterialExpressionFunctionInput* InputExpression = Cast<UMaterialExpressionFunctionInput>(Expression))
			{
				++NumInputs;
				if (NumInputs > 1 || !InputExpression->IsResultMaterialAttributes(0))
				{
					Compiler->Errorf(TEXT("Layer graphs only support a single material attributes input."));
					bHasValidOutput = false;
				}
			}
			else if (UMaterialExpressionFunctionOutput* OutputExpression = Cast<UMaterialExpressionFunctionOutput>(Expression))
			{
				++NumOutputs;
				if (NumOutputs > 1 || !OutputExpression->IsResultMaterialAttributes(0))
				{
					Compiler->Errorf(TEXT("Layer graphs only support a single material attributes output."));
					bHasValidOutput = false;
				}
			}
			else if (UMaterialExpressionMaterialAttributeLayers* RecursiveLayer = Cast<UMaterialExpressionMaterialAttributeLayers>(Expression))
			{
				Compiler->Errorf(TEXT("Layer graphs do not support layers within layers."));
					bHasValidOutput = false;
			}
		}

		if ( NumInputs > 1 || NumOutputs < 1)
		{
			Compiler->Errorf(TEXT("Layer graphs require a single material attributes output and optionally, a single material attributes input."));
			bHasValidOutput = false;
		}
	}
	else if (GetMaterialFunctionUsage() == EMaterialFunctionUsage::MaterialLayerBlend)
	{
		// Material layer blends can have up to two MA inputs and single MA output only
		for (UMaterialExpression* Expression : FunctionExpressions)
		{
			if (UMaterialExpressionFunctionInput* InputExpression = Cast<UMaterialExpressionFunctionInput>(Expression))
			{
				++NumInputs;
				if (NumInputs > 2 || !InputExpression->IsResultMaterialAttributes(0))
				{
					Compiler->Errorf(TEXT("Layer blend graphs only support two material attributes inputs."));
					bHasValidOutput = false;
				}
			}
			else if (UMaterialExpressionFunctionOutput* OutputExpression = Cast<UMaterialExpressionFunctionOutput>(Expression))
			{
				++NumOutputs;
				if (NumOutputs > 1 || !OutputExpression->IsResultMaterialAttributes(0))
				{
					Compiler->Errorf(TEXT("Layer blend graphs only support a single material attributes output."));
					bHasValidOutput = false;
				}
			}
			else if (UMaterialExpressionMaterialAttributeLayers* RecursiveLayer = Cast<UMaterialExpressionMaterialAttributeLayers>(Expression))
			{
				Compiler->Errorf(TEXT("Layer blend graphs do not support layers within layers."));
					bHasValidOutput = false;
			}
		}

		if (NumOutputs < 1)
		{
			Compiler->Errorf(TEXT("Layer blend graphs can have up to two material attributes inputs and a single output."));
			bHasValidOutput = false;
		}
	}
#endif

	return bHasValidOutput;
}

#if WITH_EDITOR
int32 UMaterialFunction::Compile(FMaterialCompiler* Compiler, const FFunctionExpressionOutput& Output)
{
	int32 ReturnValue = INDEX_NONE;

	if (ValidateFunctionUsage(Compiler, Output))
	{
		if (Output.ExpressionOutput->A.GetTracedInput().Expression)
		{
			// Compile the given function output
			ReturnValue = Output.ExpressionOutput->A.Compile(Compiler);
		}
		else
		{
			ReturnValue = Compiler->Errorf(TEXT("Missing function output connection '%s'"), *Output.ExpressionOutput->OutputName.ToString());
		}
	}

	return ReturnValue;
}

void UMaterialFunction::LinkIntoCaller(const TArray<FFunctionExpressionInput>& CallerInputs)
{
	// Go through all the function's input expressions and hook their inputs up to the corresponding expression in the material being compiled.
	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = FunctionExpressions[ExpressionIndex];
		UMaterialExpressionFunctionInput* InputExpression = Cast<UMaterialExpressionFunctionInput>(CurrentExpression);

		if (InputExpression)
		{
			// Mark that we are compiling the function as used in a material
			InputExpression->bCompilingFunctionPreview = false;
			// Initialize for this function call
			InputExpression->EffectivePreviewDuringCompile = InputExpression->Preview;

			// Get the FExpressionInput which stores information about who this input node should be linked to in order to compile
			const FExpressionInput* MatchingInput = FindInputByExpression(InputExpression, CallerInputs);

			if (MatchingInput 
				// Only change the connection if the input has a valid connection,
				// Otherwise we will need what's connected to the Preview input if bCompilingFunctionPreview is true
				&& (MatchingInput->Expression || !InputExpression->bUsePreviewValueAsDefault))
			{
				// Connect this input to the expression in the material that it should be connected to
				InputExpression->EffectivePreviewDuringCompile.Expression = MatchingInput->Expression;
				InputExpression->EffectivePreviewDuringCompile.OutputIndex = MatchingInput->OutputIndex;
				InputExpression->EffectivePreviewDuringCompile.Mask = MatchingInput->Mask;
				InputExpression->EffectivePreviewDuringCompile.MaskR = MatchingInput->MaskR;
				InputExpression->EffectivePreviewDuringCompile.MaskG = MatchingInput->MaskG;
				InputExpression->EffectivePreviewDuringCompile.MaskB = MatchingInput->MaskB;
				InputExpression->EffectivePreviewDuringCompile.MaskA = MatchingInput->MaskA;		
			}
		}
	}
}

void UMaterialFunction::UnlinkFromCaller()
{
	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = FunctionExpressions[ExpressionIndex];
		UMaterialExpressionFunctionInput* InputExpression = Cast<UMaterialExpressionFunctionInput>(CurrentExpression);

		if (InputExpression)
		{
			// Restore the default value
			InputExpression->bCompilingFunctionPreview = true;
			// Clear the reference to make stale accesses obvious
			InputExpression->EffectivePreviewDuringCompile.Expression = nullptr;
		}
	}
}

#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
bool UMaterialFunction::IsDependent(UMaterialFunctionInterface* OtherFunction)
{
	if (!OtherFunction)
	{
		return false;
	}

	bool bIsChild = false;
#if WITH_EDITORONLY_DATA
	UMaterialFunction* AsFunction = Cast<UMaterialFunction>(OtherFunction);
	bIsChild = AsFunction && AsFunction->ParentFunction == this;
#endif

	if (OtherFunction == this || bIsChild)
	{
		return true;
	}

#if WITH_EDITOR
	SetReentrantFlag(true);
#endif

	bool bIsDependent = false;
	for (int32 ExpressionIndex = 0; ExpressionIndex < FunctionExpressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = FunctionExpressions[ExpressionIndex];
		UMaterialExpressionMaterialFunctionCall* MaterialFunctionExpression = Cast<UMaterialExpressionMaterialFunctionCall>(CurrentExpression);
		if (MaterialFunctionExpression && MaterialFunctionExpression->MaterialFunction)
		{
			// Recurse to handle nesting
			bIsDependent = bIsDependent 
				|| MaterialFunctionExpression->MaterialFunction->GetReentrantFlag()
				|| MaterialFunctionExpression->MaterialFunction->IsDependent(OtherFunction);
		}
	}

#if WITH_EDITOR
	SetReentrantFlag(false);
#endif

	return bIsDependent;
}

bool UMaterialFunction::IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
{
#if WITH_EDITOR
	check(!HasAnyFlags(RF_NeedPostLoad));
	for (UMaterialExpressionMaterialFunctionCall* MaterialFunctionExpression : DependentFunctionExpressionCandidates)
	{
		if(!MaterialFunctionExpression->IterateDependentFunctions(Predicate))
		{
			return false;
		}
	}
#else
	for (UMaterialExpression* CurrentExpression : FunctionExpressions)
	{
		if (UMaterialExpressionMaterialFunctionCall* MaterialFunctionExpression = Cast<UMaterialExpressionMaterialFunctionCall>(CurrentExpression))
		{
			if (!MaterialFunctionExpression->IterateDependentFunctions(Predicate))
			{
				return false;
			}
		}
	}
#endif
	return true;
}

void UMaterialFunction::GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const
{
	IterateDependentFunctions([&DependentFunctions](UMaterialFunctionInterface* MaterialFunction) -> bool
	{
		DependentFunctions.AddUnique(MaterialFunction);
		return true;
	});
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
bool UMaterialFunction::HasFlippedCoordinates() const
{
	uint32 ReversedInputCount = 0;
	uint32 StandardInputCount = 0;

	for (int32 Index = 0; Index < FunctionExpressions.Num(); ++Index)
	{
		UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(FunctionExpressions[Index]);
		if (FunctionOutput && FunctionOutput->A.Expression)
		{
			if (FunctionOutput->A.Expression->MaterialExpressionEditorX > FunctionOutput->MaterialExpressionEditorX)
			{
				++ReversedInputCount;
			}
			else
			{
				++StandardInputCount;
			}
		}
	}

	// Can't be sure coords are flipped if most are set out correctly
	return ReversedInputCount > StandardInputCount;
}

bool UMaterialFunction::SetVectorParameterValueEditorOnly(FName ParameterName, FLinearColor InValue)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionVectorParameter* Parameter = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, InValue))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionVectorParameter* FunctionExpressionParameter = Cast<UMaterialExpressionVectorParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, InValue))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
}

bool UMaterialFunction::SetScalarParameterValueEditorOnly(FName ParameterName, float InValue)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, InValue))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionScalarParameter* FunctionExpressionParameter = Cast<UMaterialExpressionScalarParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, InValue))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
};

bool UMaterialFunction::SetTextureParameterValueEditorOnly(FName ParameterName, class UTexture* InValue)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionTextureSampleParameter* Parameter = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, InValue))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionTextureSampleParameter* FunctionExpressionParameter = Cast<UMaterialExpressionTextureSampleParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, InValue))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
};

bool UMaterialFunction::SetRuntimeVirtualTextureParameterValueEditorOnly(FName ParameterName, class URuntimeVirtualTexture* InValue)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionRuntimeVirtualTextureSampleParameter* Parameter = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, InValue))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionRuntimeVirtualTextureSampleParameter* FunctionExpressionParameter = Cast<UMaterialExpressionRuntimeVirtualTextureSampleParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, InValue))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
};

bool UMaterialFunction::SetFontParameterValueEditorOnly(FName ParameterName, class UFont* InFontValue, int32 InFontPage)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionFontSampleParameter* Parameter = Cast<UMaterialExpressionFontSampleParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, InFontValue, InFontPage))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionFontSampleParameter* FunctionExpressionParameter = Cast<UMaterialExpressionFontSampleParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, InFontValue, InFontPage))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
};

bool UMaterialFunction::SetStaticSwitchParameterValueEditorOnly(FName ParameterName, bool OutValue, FGuid OutExpressionGuid)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionStaticSwitchParameter* Parameter = Cast<UMaterialExpressionStaticSwitchParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, OutValue, OutExpressionGuid))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionStaticSwitchParameter* FunctionExpressionParameter = Cast<UMaterialExpressionStaticSwitchParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, OutValue, OutExpressionGuid))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
};

bool UMaterialFunction::SetStaticComponentMaskParameterValueEditorOnly(FName ParameterName, bool R, bool G, bool B, bool A, FGuid OutExpressionGuid)
{
	for (UMaterialExpression* Expression : FunctionExpressions)
	{
		if (UMaterialExpressionStaticComponentMaskParameter* Parameter = Cast<UMaterialExpressionStaticComponentMaskParameter>(Expression))
		{
			if (Parameter->SetParameterValue(ParameterName, R, G, B, A, OutExpressionGuid))
			{
				return true;
				// Warning: in the case of duplicate parameters with different default values, this will find the first in the expression array, not necessarily the one that's used for rendering
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (FunctionCall->MaterialFunction)
			{
				TArray<UMaterialFunctionInterface*> Functions;
				Functions.Add(FunctionCall->MaterialFunction);
				FunctionCall->MaterialFunction->GetDependentFunctions(Functions);

				for (UMaterialFunctionInterface* Function : Functions)
				{
					const TArray<UMaterialExpression*>* ExpressionPtr = Function->GetFunctionExpressions();
					if (ExpressionPtr)
					{
						for (UMaterialExpression* FunctionExpression : *ExpressionPtr)
						{
							if (UMaterialExpressionStaticComponentMaskParameter* FunctionExpressionParameter = Cast<UMaterialExpressionStaticComponentMaskParameter>(FunctionExpression))
							{
								if (FunctionExpressionParameter->SetParameterValue(ParameterName, R, G, B, A, OutExpressionGuid))
								{
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	return false;
};
#endif //WITH_EDITORONLY_DATA

///////////////////////////////////////////////////////////////////////////////
// UMaterialFunctionInstance
///////////////////////////////////////////////////////////////////////////////

UMaterialFunctionInstance::UMaterialFunctionInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	PreviewMaterial = nullptr;
	ThumbnailInfo = nullptr;
#endif
}

#if WITH_EDITOR
void UMaterialFunctionInstance::UpdateParameterSet()
{
	if (UMaterialFunction* BaseFunction = Cast<UMaterialFunction>(GetBaseFunction()))
	{
		TArray<UMaterialFunctionInterface*> Functions;
		BaseFunction->GetDependentFunctions(Functions);
		Functions.AddUnique(BaseFunction);

		// Loop through all contained parameters and update names as needed
		for (UMaterialFunctionInterface* Function : Functions)
		{
			for (UMaterialExpression* FunctionExpression : *Function->GetFunctionExpressions())
			{
				if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<const UMaterialExpressionScalarParameter>(FunctionExpression))
				{
					for (FScalarParameterValue& ScalarParameterValue : ScalarParameterValues)
					{
						if (ScalarParameterValue.ExpressionGUID == ScalarParameter->ExpressionGUID)
						{
							ScalarParameterValue.ParameterInfo.Name = ScalarParameter->ParameterName;
							break;
						}
					}
				}
				else if (const UMaterialExpressionVectorParameter* VectorParameter = Cast<const UMaterialExpressionVectorParameter>(FunctionExpression))
				{
					for (FVectorParameterValue& VectorParameterValue : VectorParameterValues)
					{
						if (VectorParameterValue.ExpressionGUID == VectorParameter->ExpressionGUID)
						{
							VectorParameterValue.ParameterInfo.Name = VectorParameter->ParameterName;
							break;
						}
					}
				}
				else if (const UMaterialExpressionTextureSampleParameter* TextureParameter = Cast<const UMaterialExpressionTextureSampleParameter>(FunctionExpression))
				{
					for (FTextureParameterValue& TextureParameterValue : TextureParameterValues)
					{
						if (TextureParameterValue.ExpressionGUID == TextureParameter->ExpressionGUID)
						{
							TextureParameterValue.ParameterInfo.Name = TextureParameter->ParameterName;
							break;
						}
					}
				}
				else if (const UMaterialExpressionRuntimeVirtualTextureSampleParameter* RuntimeVirtualTextureParameter = Cast<const UMaterialExpressionRuntimeVirtualTextureSampleParameter>(FunctionExpression))
				{
					for (FRuntimeVirtualTextureParameterValue& RuntimeVirtualTextureParameterValue : RuntimeVirtualTextureParameterValues)
					{
						if (RuntimeVirtualTextureParameterValue.ExpressionGUID == RuntimeVirtualTextureParameter->ExpressionGUID)
						{
							RuntimeVirtualTextureParameterValue.ParameterInfo.Name = RuntimeVirtualTextureParameter->ParameterName;
							break;
						}
					}
				}
				else if (const UMaterialExpressionFontSampleParameter* FontParameter = Cast<const UMaterialExpressionFontSampleParameter>(FunctionExpression))
				{
					for (FFontParameterValue& FontParameterValue : FontParameterValues)
					{
						if (FontParameterValue.ExpressionGUID == FontParameter->ExpressionGUID)
						{
							FontParameterValue.ParameterInfo.Name = FontParameter->ParameterName;
							break;
						}
					}
				}
				else if (const UMaterialExpressionStaticBoolParameter* StaticSwitchParameter = Cast<const UMaterialExpressionStaticBoolParameter>(FunctionExpression))
				{
					for (FStaticSwitchParameter& StaticSwitchParameterValue : StaticSwitchParameterValues)
					{
						if (StaticSwitchParameterValue.ExpressionGUID == StaticSwitchParameter->ExpressionGUID)
						{
							StaticSwitchParameterValue.ParameterInfo.Name = StaticSwitchParameter->ParameterName;
							break;
						}
					}
				}
				else if (const UMaterialExpressionStaticComponentMaskParameter* StaticComponentMaskParameter = Cast<const UMaterialExpressionStaticComponentMaskParameter>(FunctionExpression))
				{
					for (FStaticComponentMaskParameter& StaticComponentMaskParameterValue : StaticComponentMaskParameterValues)
					{
						if (StaticComponentMaskParameterValue.ExpressionGUID == StaticComponentMaskParameter->ExpressionGUID)
						{
							StaticComponentMaskParameterValue.ParameterInfo.Name = StaticComponentMaskParameter->ParameterName;
							break;
						}
					}
				}
			}
		}
	}
}

void UMaterialFunctionInstance::OverrideMaterialInstanceParameterValues(UMaterialInstance* Instance)
{
	// Dynamic parameters
	Instance->ScalarParameterValues = ScalarParameterValues;
	Instance->VectorParameterValues = VectorParameterValues;
	Instance->TextureParameterValues = TextureParameterValues;
	Instance->RuntimeVirtualTextureParameterValues = RuntimeVirtualTextureParameterValues;
	Instance->FontParameterValues = FontParameterValues;

	// Static parameters
	FStaticParameterSet StaticParametersOverride = Instance->GetStaticParameters();
	StaticParametersOverride.StaticSwitchParameters = StaticSwitchParameterValues;
	StaticParametersOverride.StaticComponentMaskParameters = StaticComponentMaskParameterValues;
	Instance->UpdateStaticPermutation(StaticParametersOverride);
}

void UMaterialFunctionInstance::UpdateFromFunctionResource()
{
	if (Parent)
	{
		Parent->UpdateFromFunctionResource();
	}
}

void UMaterialFunctionInstance::GetInputsAndOutputs(TArray<struct FFunctionExpressionInput>& OutInputs, TArray<struct FFunctionExpressionOutput>& OutOutputs) const
{
	if (Parent)
	{
		Parent->GetInputsAndOutputs(OutInputs, OutOutputs);
	}
}
#endif // WITH_EDITOR

bool UMaterialFunctionInstance::ValidateFunctionUsage(class FMaterialCompiler* Compiler, const FFunctionExpressionOutput& Output)
{
	return Parent ? Parent->ValidateFunctionUsage(Compiler, Output) : false;
}

void UMaterialFunctionInstance::PostLoad()
{
	Super::PostLoad();

	if (Parent)
	{
		Parent->ConditionalPostLoad();
	}
}

#if WITH_EDITOR
int32 UMaterialFunctionInstance::Compile(class FMaterialCompiler* Compiler, const struct FFunctionExpressionOutput& Output)
{
	return Parent ? Parent->Compile(Compiler, Output) : INDEX_NONE;
}

void UMaterialFunctionInstance::LinkIntoCaller(const TArray<FFunctionExpressionInput>& CallerInputs)
{
	if (Parent)
	{
		Parent->LinkIntoCaller(CallerInputs);
	}
}

void UMaterialFunctionInstance::UnlinkFromCaller()
{
	if (Parent)
	{
		Parent->UnlinkFromCaller();
	}
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
bool UMaterialFunctionInstance::IsDependent(UMaterialFunctionInterface* OtherFunction)
{
	return Parent ? Parent->IsDependent(OtherFunction) : false;
}

bool UMaterialFunctionInstance::IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
{
	if (Parent)
	{
		if (!Parent->IterateDependentFunctions(Predicate))
		{
			return false;
		}
		if (!Predicate(Parent))
		{
			return false;
		}
	}
	return true;
}

void UMaterialFunctionInstance::GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const
{
	if (Parent)
	{
		Parent->GetDependentFunctions(DependentFunctions);
		DependentFunctions.AddUnique(Parent);
	}
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
UMaterialInterface* UMaterialFunctionInstance::GetPreviewMaterial()
{
	if (nullptr == PreviewMaterial)
	{
		PreviewMaterial = NewObject<UMaterialInstanceConstant>((UObject*)GetTransientPackage(), FName(TEXT("None")), RF_Transient);
		PreviewMaterial->SetParentEditorOnly(Parent ? Parent->GetPreviewMaterial() : nullptr);
		OverrideMaterialInstanceParameterValues(PreviewMaterial);
		PreviewMaterial->PreEditChange(nullptr);
		PreviewMaterial->PostEditChange();

	}
	return PreviewMaterial;
}

void UMaterialFunctionInstance::UpdateInputOutputTypes()
{
	if (Parent)
	{
		Parent->UpdateInputOutputTypes();
	}
}

bool UMaterialFunctionInstance::HasFlippedCoordinates() const
{
	return Parent ? Parent->HasFlippedCoordinates() : false;
}
#endif

bool UMaterialFunctionInstance::OverrideNamedScalarParameter(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue)
{
	for (const FScalarParameterValue& ScalarParameter : ScalarParameterValues)
	{
		if (ScalarParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutValue = ScalarParameter.ParameterValue;
			return true;
		}
	}

	return false;
}

bool UMaterialFunctionInstance::OverrideNamedVectorParameter(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue)
{
	for (const FVectorParameterValue& VectorParameter : VectorParameterValues)
	{
		if (VectorParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutValue = VectorParameter.ParameterValue;
			return true;
		}
	}

	return false;
}

bool UMaterialFunctionInstance::OverrideNamedTextureParameter(const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue)
{
	for (const FTextureParameterValue& TextureParameter : TextureParameterValues)
	{
		if (TextureParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutValue = TextureParameter.ParameterValue;
			return true;
		}
	}

	return false;
}

bool UMaterialFunctionInstance::OverrideNamedRuntimeVirtualTextureParameter(const FHashedMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue)
{
	for (const FRuntimeVirtualTextureParameterValue& RuntimeVirtualTextureParameter : RuntimeVirtualTextureParameterValues)
	{
		if (RuntimeVirtualTextureParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutValue = RuntimeVirtualTextureParameter.ParameterValue;
			return true;
		}
	}

	return false;
}

bool UMaterialFunctionInstance::OverrideNamedFontParameter(const FHashedMaterialParameterInfo& ParameterInfo, UFont*& OutFontValue, int32& OutFontPage)
{
	for (const FFontParameterValue& FontParameter : FontParameterValues)
	{
		if (FontParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutFontValue = FontParameter.FontValue;
			OutFontPage = FontParameter.FontPage;
			return true;
		}
	}

	return false;
}

bool UMaterialFunctionInstance::OverrideNamedStaticSwitchParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid)
{
	for (const FStaticSwitchParameter& StaticSwitchParameter : StaticSwitchParameterValues)
	{
		if (StaticSwitchParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutValue = StaticSwitchParameter.Value;
			OutExpressionGuid = StaticSwitchParameter.ExpressionGUID;
			return true;
		}
	}

	return false;
}

bool UMaterialFunctionInstance::OverrideNamedStaticComponentMaskParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid& OutExpressionGuid)
{
	for (const FStaticComponentMaskParameter& StaticComponentMaskParameter : StaticComponentMaskParameterValues)
	{
		if (StaticComponentMaskParameter.ParameterInfo.Name == ParameterInfo.Name)
		{
			OutR = StaticComponentMaskParameter.R;
			OutG = StaticComponentMaskParameter.G;
			OutB = StaticComponentMaskParameter.B;
			OutA = StaticComponentMaskParameter.A;
			OutExpressionGuid = StaticComponentMaskParameter.ExpressionGUID;
			return true;
		}
	}

	return false;
}


///////////////////////////////////////////////////////////////////////////////
// FMaterialLayersFunctions::ID
///////////////////////////////////////////////////////////////////////////////

bool FMaterialLayersFunctions::ID::operator==(const ID& Reference) const
{
	return LayerIDs == Reference.LayerIDs && BlendIDs == Reference.BlendIDs && LayerStates == Reference.LayerStates;
}


void FMaterialLayersFunctions::ID::SerializeForDDC(FArchive& Ar)
{
	Ar << LayerIDs;
	Ar << BlendIDs;
	Ar << LayerStates;
}


void FMaterialLayersFunctions::ID::UpdateHash(FSHA1& HashState) const
{
	for (const FGuid &Guid : LayerIDs)
	{
		HashState.Update((const uint8*)&Guid, sizeof(FGuid));
	}
	for (const FGuid &Guid : BlendIDs)
	{
		HashState.Update((const uint8*)&Guid, sizeof(FGuid));
	}
	HashState.Update((const uint8*)LayerStates.GetData(), LayerStates.Num()*LayerStates.GetTypeSize());
}


void FMaterialLayersFunctions::ID::AppendKeyString(FString& KeyString) const
{
	for (const FGuid &Guid : LayerIDs)
	{
		KeyString += Guid.ToString();
	}
	for (const FGuid &Guid : BlendIDs)
	{
		KeyString += Guid.ToString();
	}
	for (bool State : LayerStates)
	{
		KeyString += FString::FromInt(State);
	}
}


///////////////////////////////////////////////////////////////////////////////
// FMaterialLayersFunctions
///////////////////////////////////////////////////////////////////////////////

const FGuid FMaterialLayersFunctions::BackgroundGuid(2u, 0u, 0u, 0u);

const FMaterialLayersFunctions::ID FMaterialLayersFunctions::GetID() const
{
	FMaterialLayersFunctions::ID Result;

	// Store the layer IDs in following format - stateID per function
	Result.LayerIDs.SetNum(Layers.Num());
	for (int i=0; i<Layers.Num(); ++i)
	{
		const UMaterialFunctionInterface* Layer = Layers[i];
		Result.LayerIDs[i] = (Layer) ? Layer->StateId : FGuid();
	}

	// Store the blend IDs in following format - stateID per function
	Result.BlendIDs.SetNum(Blends.Num());
	for (int i = 0; i < Blends.Num(); ++i)
	{
		const UMaterialFunctionInterface* Blend = Blends[i];
		Result.BlendIDs[i] = (Blend) ? Blend->StateId : FGuid();
	}

	// Store the states copy
	Result.LayerStates = LayerStates;

	return Result;
}

FString FMaterialLayersFunctions::GetStaticPermutationString() const
{
	FString StaticKeyString;
	for (UMaterialFunctionInterface* Layer : Layers)
	{
		UMaterialFunctionInterface* Parent = Layer ? Layer->GetBaseFunction() : nullptr;
		if (Parent)
		{
			StaticKeyString += TEXT("_") + Parent->GetOutermost()->GetFullName();
		}
		else
		{
			StaticKeyString += TEXT("_NullLayer");
		}
	}

	for (UMaterialFunctionInterface* Blend : Blends)
	{
		UMaterialFunctionInterface* Parent = Blend ? Blend->GetBaseFunction() : nullptr;
		if (Parent)
		{
			StaticKeyString += TEXT("_") + Parent->GetOutermost()->GetFullName();
		}
		else
		{
			StaticKeyString += TEXT("_NullBlend");
		}
	}

	// @TODO: This will generate unique permutations for disabled layers but
	// should append layers/blends in reverse order, stopping at opaque and
	// skipping inactive to allow maximum DDC re-use where possible
	StaticKeyString += TEXT("_");
	for (const bool State : LayerStates)
	{
		StaticKeyString += State ? TEXT("1") : TEXT("0");
	}

	return StaticKeyString;
}

void FMaterialLayersFunctions::SerializeForDDC(FArchive& Ar)
{
	if (!Ar.IsCooking())
	{
		KeyString_DEPRECATED = GetStaticPermutationString();
	}
	Ar << KeyString_DEPRECATED;
}

void FMaterialLayersFunctions::PostSerialize(const FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		if (LayerGuids.Num() != Layers.Num() ||
			LayerLinkStates.Num() != Layers.Num())
		{
			const int32 NumLayers = Layers.Num();
			LayerGuids.Empty(NumLayers);
			LayerLinkStates.Empty(NumLayers);

			if (NumLayers > 0)
			{
				LayerGuids.Add(BackgroundGuid);
				LayerLinkStates.Add(EMaterialLayerLinkState::Uninitialized);

				for (int32 i = 1; i < NumLayers; ++i)
				{
					// Need to allocate deterministic guids for layers loaded from old data
					// These guids will be saved into any child material layers that have this material as their parent,
					// But it's possible *this* material may not actually be saved in that case
					// If that happens, need to ensure that the guids remain consistent if this material is loaded again;
					// otherwise they will no longer match the guids that were saved into the child material
					LayerGuids.Add(FGuid(3u, 0u, 0u, i));
					LayerLinkStates.Add(EMaterialLayerLinkState::Uninitialized);
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
}

int32 FMaterialLayersFunctions::AppendBlendedLayer()
{
	const int32 LayerIndex = Layers.AddDefaulted();
	Blends.AddDefaulted();
	LayerStates.Add(true);
#if WITH_EDITOR
	FText LayerName = FText::Format(LOCTEXT("LayerPrefix", "Layer {0}"), Layers.Num() - 1);
	LayerNames.Add(LayerName);
	RestrictToLayerRelatives.Add(false);
	RestrictToBlendRelatives.Add(false);
	LayerGuids.Add(FGuid::NewGuid());
	LayerLinkStates.Add(EMaterialLayerLinkState::NotFromParent);
#endif
	return LayerIndex;
}

int32 FMaterialLayersFunctions::AddLayerCopy(const FMaterialLayersFunctions& Source, int32 SourceLayerIndex, EMaterialLayerLinkState LinkState)
{
	check(LinkState != EMaterialLayerLinkState::Uninitialized);
	const int32 LayerIndex = Layers.Num();

	Layers.Add(Source.Layers[SourceLayerIndex]);
	if (LayerIndex > 0)
	{
		Blends.Add(Source.Blends[SourceLayerIndex - 1]);
	}
	LayerStates.Add(Source.LayerStates[SourceLayerIndex]);
#if WITH_EDITOR
	LayerNames.Add(Source.LayerNames[SourceLayerIndex]);
	RestrictToLayerRelatives.Add(Source.RestrictToLayerRelatives[SourceLayerIndex]);
	if (LayerIndex > 0)
	{
		RestrictToBlendRelatives.Add(Source.RestrictToBlendRelatives[SourceLayerIndex - 1]);
	}
	LayerGuids.Add(Source.LayerGuids[SourceLayerIndex]);
	LayerLinkStates.Add(LinkState);
#endif
	return LayerIndex;
}

void FMaterialLayersFunctions::InsertLayerCopy(const FMaterialLayersFunctions& Source, int32 SourceLayerIndex, EMaterialLayerLinkState LinkState, int32 LayerIndex)
{
	check(LinkState != EMaterialLayerLinkState::Uninitialized);
	check(LayerIndex > 0);
	Layers.Insert(Source.Layers[SourceLayerIndex], LayerIndex);
	Blends.Insert(Source.Blends[SourceLayerIndex - 1], LayerIndex - 1);
	LayerStates.Insert(Source.LayerStates[SourceLayerIndex], LayerIndex);
#if WITH_EDITOR
	LayerNames.Insert(Source.LayerNames[SourceLayerIndex], LayerIndex);
	RestrictToLayerRelatives.Insert(Source.RestrictToLayerRelatives[SourceLayerIndex], LayerIndex);
	RestrictToBlendRelatives.Insert(Source.RestrictToBlendRelatives[SourceLayerIndex - 1], LayerIndex - 1);
	LayerGuids.Insert(Source.LayerGuids[SourceLayerIndex], LayerIndex);
	LayerLinkStates.Insert(LinkState, LayerIndex);
#endif
}

void FMaterialLayersFunctions::RemoveBlendedLayerAt(int32 Index)
{
	if (Layers.IsValidIndex(Index))
	{
		check(Layers.IsValidIndex(Index) && Blends.IsValidIndex(Index - 1) && LayerStates.IsValidIndex(Index));
		Layers.RemoveAt(Index);
		Blends.RemoveAt(Index - 1);
		LayerStates.RemoveAt(Index);
#if WITH_EDITOR
		check(LayerNames.IsValidIndex(Index) && RestrictToLayerRelatives.IsValidIndex(Index) && RestrictToBlendRelatives.IsValidIndex(Index - 1));

		if (LayerLinkStates[Index] != EMaterialLayerLinkState::NotFromParent)
		{
			// Save the parent guid as explicitly deleted, so it's not added back
			const FGuid& LayerGuid = LayerGuids[Index];
			check(!DeletedParentLayerGuids.Contains(LayerGuid));
			DeletedParentLayerGuids.Add(LayerGuid);
		}

		LayerNames.RemoveAt(Index);
		RestrictToLayerRelatives.RemoveAt(Index);
		RestrictToBlendRelatives.RemoveAt(Index - 1);
		LayerGuids.RemoveAt(Index);
		LayerLinkStates.RemoveAt(Index);
#endif //WITH_EDITOR
	}
}

void FMaterialLayersFunctions::MoveBlendedLayer(int32 SrcLayerIndex, int32 DstLayerIndex)
{
	check(SrcLayerIndex > 0);
	check(DstLayerIndex > 0);
	if (SrcLayerIndex != DstLayerIndex)
	{
		Layers.Swap(SrcLayerIndex, DstLayerIndex);
		Blends.Swap(SrcLayerIndex - 1, DstLayerIndex - 1);
		LayerStates.Swap(SrcLayerIndex, DstLayerIndex);
#if WITH_EDITOR
		LayerNames.Swap(SrcLayerIndex, DstLayerIndex);
		RestrictToLayerRelatives.Swap(SrcLayerIndex, DstLayerIndex);
		RestrictToBlendRelatives.Swap(SrcLayerIndex - 1, DstLayerIndex - 1);
		LayerGuids.Swap(SrcLayerIndex, DstLayerIndex);
		LayerLinkStates.Swap(SrcLayerIndex, DstLayerIndex);
#endif //WITH_EDITOR
	}
}

#if WITH_EDITOR
void FMaterialLayersFunctions::UnlinkLayerFromParent(int32 Index)
{
	if (LayerLinkStates[Index] == EMaterialLayerLinkState::LinkedToParent)
	{
		LayerLinkStates[Index] = EMaterialLayerLinkState::UnlinkedFromParent;
	}
}

bool FMaterialLayersFunctions::IsLayerLinkedToParent(int32 Index) const
{
	if (LayerLinkStates.IsValidIndex(Index))
	{
		return LayerLinkStates[Index] == EMaterialLayerLinkState::LinkedToParent;
	}
	return false;
}

void FMaterialLayersFunctions::RelinkLayersToParent()
{
	for (int32 Index = 0; Index < LayerLinkStates.Num(); ++Index)
	{
		if (LayerLinkStates[Index] == EMaterialLayerLinkState::UnlinkedFromParent)
		{
			LayerLinkStates[Index] = EMaterialLayerLinkState::LinkedToParent;
		}
	}
	DeletedParentLayerGuids.Empty();
}

bool FMaterialLayersFunctions::HasAnyUnlinkedLayers() const
{
	if (DeletedParentLayerGuids.Num() > 0)
	{
		return true;
	}
	for (int32 Index = 0; Index < LayerLinkStates.Num(); ++Index)
	{
		if (LayerLinkStates[Index] == EMaterialLayerLinkState::UnlinkedFromParent)
		{
			return true;
		}
	}
	return false;
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void FMaterialLayersFunctions::LinkAllLayersToParent()
{
	for (int32 Index = 0; Index < LayerLinkStates.Num(); ++Index)
	{
		LayerLinkStates[Index] = EMaterialLayerLinkState::LinkedToParent;
	}
}

bool FMaterialLayersFunctions::ResolveParent(const FMaterialLayersFunctions& Parent, TArray<int32>& OutRemapLayerIndices)
{
	check(LayerGuids.Num() == Layers.Num());
	check(LayerLinkStates.Num() == Layers.Num());

	FMaterialLayersFunctions ResolvedLayers;
	TArray<int32> ParentLayerIndices;

	ResolvedLayers.Empty();

	bool bHasUninitializedLinks = false;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		const FText& LayerName = LayerNames[LayerIndex];
		const FGuid& LayerGuid = LayerGuids[LayerIndex];
		const EMaterialLayerLinkState LinkState = LayerLinkStates[LayerIndex];

		int32 ParentLayerIndex = INDEX_NONE;
		if (LinkState == EMaterialLayerLinkState::Uninitialized)
		{
			bHasUninitializedLinks = true;
			if (LayerIndex == 0)
			{
				// Base layer must match against base layer
				if (Parent.Layers.Num() > 0)
				{
					ParentLayerIndex = 0;
				}
			}
			else
			{
				for (int32 CheckLayerIndex = 1; CheckLayerIndex < Parent.Layers.Num(); ++CheckLayerIndex)
				{
					// check if name matches, and if we haven't already linked to this parent layer
					if (LayerName.EqualTo(Parent.LayerNames[CheckLayerIndex]) &&
						!ParentLayerIndices.Contains(CheckLayerIndex))
					{
						ParentLayerIndex = CheckLayerIndex;
						break;
					}
				}
			}

			int32 ResolvedLayerIndex = INDEX_NONE;
			if (ParentLayerIndex == INDEX_NONE)
			{
				// Didn't find layer in the parent, assume it's local to this material
				ResolvedLayerIndex = ResolvedLayers.AddLayerCopy(*this, LayerIndex, EMaterialLayerLinkState::NotFromParent);
				ParentLayerIndices.Add(INDEX_NONE);
			}
			else
			{
				// See if we match layer in parent
				if (Layers[LayerIndex] == Parent.Layers[ParentLayerIndex] &&
					(LayerIndex == 0 || Blends[LayerIndex - 1] == Parent.Blends[ParentLayerIndex - 1]))
				{
					// Parent layer matches, so link to parent
					ResolvedLayerIndex = ResolvedLayers.AddLayerCopy(Parent, ParentLayerIndex, EMaterialLayerLinkState::LinkedToParent);
				}
				else
				{
					// Parent layer does NOT match, so make the child overriden
					ResolvedLayerIndex = ResolvedLayers.AddLayerCopy(*this, LayerIndex, EMaterialLayerLinkState::UnlinkedFromParent);
					ResolvedLayers.LayerGuids[ResolvedLayerIndex] = Parent.LayerGuids[ParentLayerIndex]; // Still need to match guid to parent
				}

				check(!ParentLayerIndices.Contains(ParentLayerIndex));
				ParentLayerIndices.Add(ParentLayerIndex);
			}

			// If link state is Uninitialized, we *always* need to accept the layer in some way, otherwise we risk changing legacy data when loading in new engine
			check(ResolvedLayerIndex != INDEX_NONE);
		}
		else if (LinkState == EMaterialLayerLinkState::LinkedToParent)
		{
			check(LayerGuid.IsValid());
			ParentLayerIndex = Parent.LayerGuids.Find(LayerGuid);
			if (ParentLayerIndex != INDEX_NONE)
			{
				// Layer comes from parent
				ResolvedLayers.AddLayerCopy(Parent, ParentLayerIndex, EMaterialLayerLinkState::LinkedToParent);
				check(!ParentLayerIndices.Contains(ParentLayerIndex));
				ParentLayerIndices.Add(ParentLayerIndex);
			}
			// if we didn't find the layer in the parent, that means it was deleted from parent...so it's also deleted in the child
		}
		else
		{
			// layer not connected to parent
			check(LayerGuid.IsValid());
			check(LinkState == EMaterialLayerLinkState::UnlinkedFromParent || LinkState == EMaterialLayerLinkState::NotFromParent);

			if (LinkState == EMaterialLayerLinkState::UnlinkedFromParent)
			{
				// If we are unlinked from parent, track the layer index we were previously linked to
				ParentLayerIndex = Parent.LayerGuids.Find(LayerGuid);
			}
			check(ParentLayerIndex == INDEX_NONE || !ParentLayerIndices.Contains(ParentLayerIndex));

			// Update the link state, depending on if we can find this layer in the parent
			ResolvedLayers.AddLayerCopy(*this, LayerIndex, (ParentLayerIndex == INDEX_NONE) ? EMaterialLayerLinkState::NotFromParent : EMaterialLayerLinkState::UnlinkedFromParent);
			ParentLayerIndices.Add(ParentLayerIndex);
		}
	}

	check(ResolvedLayers.Layers.Num() == ParentLayerIndices.Num());

	// See if parent has any added layers
	for (int32 ParentLayerIndex = 1; ParentLayerIndex < Parent.Layers.Num(); ++ParentLayerIndex)
	{
		if (ParentLayerIndices.Contains(ParentLayerIndex))
		{
			// We already linked this layer to an existing child layer
			continue;
		}

		const FGuid& ParentLayerGuid = Parent.LayerGuids[ParentLayerIndex];
		if (DeletedParentLayerGuids.Contains(ParentLayerGuid))
		{
			// Parent layer was previously explicitly overiden/deleted
			ResolvedLayers.DeletedParentLayerGuids.Add(ParentLayerGuid);
			continue;
		}

		if (bHasUninitializedLinks)
		{
			// If we had any unitialized links, this means we're loading data saved by a previous version of UE4
			// In this case, we have no way of determining if this layer was added to parent (and should therefore be kept),
			// or if this layer was explicitly deleted from child (and should therefore remain deleted).
			// In order to avoid needlessly changing legacy materials, we assume the layer was explicitly deleted, so we keep it deleted here
			// If desired, the relink functionality in the editor should allow it to be brought back
			ResolvedLayers.DeletedParentLayerGuids.Add(ParentLayerGuid);
			continue;
		}

		// Find the layer before the newly inserted layer...we insert the new layer in the child at this same position
		int32 InsertLayerIndex = INDEX_NONE;
		{
			int32 CheckLayerIndex = ParentLayerIndex;
			while (InsertLayerIndex == INDEX_NONE)
			{
				--CheckLayerIndex;
				if (CheckLayerIndex == 0)
				{
					InsertLayerIndex = 0;
				}
				else
				{
					InsertLayerIndex = ParentLayerIndices.Find(CheckLayerIndex);
				}
			}
		}

		ParentLayerIndices.Insert(ParentLayerIndex, InsertLayerIndex + 1);
		ResolvedLayers.InsertLayerCopy(Parent, ParentLayerIndex, EMaterialLayerLinkState::LinkedToParent, InsertLayerIndex + 1);
	}

	bool bUpdatedLayerIndices = Layers.Num() != ResolvedLayers.Layers.Num() ||
		DeletedParentLayerGuids.Num() != ResolvedLayers.DeletedParentLayerGuids.Num();

	OutRemapLayerIndices.SetNumUninitialized(Layers.Num());
	for (int32 PrevLayerIndex = 0; PrevLayerIndex < Layers.Num(); ++PrevLayerIndex)
	{
		const FGuid& LayerGuid = LayerGuids[PrevLayerIndex];
		const int32 ResolvedLayerIndex = ResolvedLayers.LayerGuids.Find(LayerGuid);
		OutRemapLayerIndices[PrevLayerIndex] = ResolvedLayerIndex;

		if (PrevLayerIndex != ResolvedLayerIndex)
		{
			bUpdatedLayerIndices = true;
		}
	}

	*this = MoveTemp(ResolvedLayers);

	return bUpdatedLayerIndices;
}
#endif // WITH_EDITORONLY_DATA

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionMaterialFunctionCall
///////////////////////////////////////////////////////////////////////////////

UMaterialFunctionInterface* SavedMaterialFunction = nullptr;

UMaterialExpressionMaterialFunctionCall::UMaterialExpressionMaterialFunctionCall(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Functions;
		FConstructorStatics()
			: NAME_Functions(LOCTEXT( "Functions", "Functions" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bShowOutputNameOnPin = true;
	bHidePreviewWindow = true;

	MenuCategories.Add(ConstructorStatics.NAME_Functions);

	// Function calls created without a function should be pinless by default
	FunctionInputs.Empty();
	FunctionOutputs.Empty();
	Outputs.Empty();
#endif
}

void UMaterialExpressionMaterialFunctionCall::PostLoad()
{
	if (MaterialFunction)
	{
		MaterialFunction->ConditionalPostLoad();
	}

	Super::PostLoad();
}

#if WITH_EDITORONLY_DATA
bool UMaterialExpressionMaterialFunctionCall::IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
{
	if (MaterialFunction)
	{
		if (!MaterialFunction->IterateDependentFunctions(Predicate))
		{
			return false;
		}
		if (!Predicate(MaterialFunction))
		{
			return false;
		}
	}
	return true;
}

void UMaterialExpressionMaterialFunctionCall::GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const
{
	IterateDependentFunctions([&DependentFunctions](UMaterialFunctionInterface* InMaterialFunction) -> bool
	{
		DependentFunctions.AddUnique(InMaterialFunction);
		return true;
	});
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
void UMaterialExpressionMaterialFunctionCall::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == FName(TEXT("MaterialFunction")))
	{
		// Save off the previous MaterialFunction value
		SavedMaterialFunction = MaterialFunction;
	}
	Super::PreEditChange(PropertyAboutToChange);
}

void UMaterialExpressionMaterialFunctionCall::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == FName(TEXT("MaterialFunction")))
	{
		// Set the new material function
		SetMaterialFunctionEx(SavedMaterialFunction, MaterialFunction);
		SavedMaterialFunction = nullptr;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialExpressionMaterialFunctionCall::LinkFunctionIntoCaller(FMaterialCompiler* Compiler)
{
	MaterialFunction->LinkIntoCaller(FunctionInputs);
	// Update parameter owner when stepping into layer functions
	if (MaterialFunction->GetMaterialFunctionUsage() != EMaterialFunctionUsage::Default)
	{
		Compiler->PushParameterOwner(FunctionParameterInfo);
	}
}

void UMaterialExpressionMaterialFunctionCall::UnlinkFunctionFromCaller(FMaterialCompiler* Compiler)
{
	if (MaterialFunction->GetMaterialFunctionUsage() != EMaterialFunctionUsage::Default)
	{
		const FMaterialParameterInfo PoppedParameterInfo = Compiler->PopParameterOwner();
		check(PoppedParameterInfo == FunctionParameterInfo);
	}
	MaterialFunction->UnlinkFromCaller();
}

int32 UMaterialExpressionMaterialFunctionCall::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!MaterialFunction)
	{
		return Compiler->Errorf(TEXT("Missing Material Function"));
	}

	// Verify that all function inputs and outputs are in a valid state to be linked into this material for compiling
	for (int32 i = 0; i < FunctionInputs.Num(); i++)
	{
		check(FunctionInputs[i].ExpressionInput);
	}

	for (int32 i = 0; i < FunctionOutputs.Num(); i++)
	{
		check(FunctionOutputs[i].ExpressionOutput);
	}

	if (!FunctionOutputs.IsValidIndex(OutputIndex))
	{
		return Compiler->Errorf(TEXT("Invalid function output"));
	}

	// Link the function's inputs into the caller graph before entering
	LinkFunctionIntoCaller(Compiler);

	// Some functions (e.g. layers) don't benefit from re-using state so we locally create one as we did before sharing was added
	FMaterialFunctionCompileState LocalState(this);

	// Tell the compiler that we are entering a function
	const int32 ExpressionStackCheckSize = SharedCompileState ? SharedCompileState->ExpressionStack.Num() : 0;
	Compiler->PushFunction(SharedCompileState ? SharedCompileState : &LocalState);

	// Compile the requested output
	const int32 ReturnValue = MaterialFunction->Compile(Compiler, FunctionOutputs[OutputIndex]);

	// Tell the compiler that we are leaving a function
	FMaterialFunctionCompileState* CompileState = Compiler->PopFunction();
	check(!SharedCompileState || CompileState->ExpressionStack.Num() == ExpressionStackCheckSize);

	// Restore the function since we are leaving it
	UnlinkFunctionFromCaller(Compiler);

	return ReturnValue;
}

void UMaterialExpressionMaterialFunctionCall::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(MaterialFunction ? MaterialFunction->GetName() : TEXT("Unspecified Function"));
}

const TArray<FExpressionInput*> UMaterialExpressionMaterialFunctionCall::GetInputs()
{
	TArray<FExpressionInput*> Result;
	for (int32 i = 0; i < FunctionInputs.Num(); i++)
	{
		Result.Add(&FunctionInputs[i].Input);
	}
	return Result;
}

FExpressionInput* UMaterialExpressionMaterialFunctionCall::GetInput(int32 InputIndex)
{
	if (InputIndex < FunctionInputs.Num())
	{
		return &FunctionInputs[InputIndex].Input;
	}
	return nullptr;
}


static const TCHAR* GetInputTypeName(uint8 InputType)
{
	const static TCHAR* TypeNames[FunctionInput_MAX] =
	{
		TEXT("S"),
		TEXT("V2"),
		TEXT("V3"),
		TEXT("V4"),
		TEXT("T2d"),
		TEXT("TCube"),
		TEXT("T2dArr"),
		TEXT("TVol"),
		TEXT("B"),
		TEXT("MA"),
		TEXT("TExt")
	};

	check(InputType < FunctionInput_MAX);
	return TypeNames[InputType];
}

FName UMaterialExpressionMaterialFunctionCall::GetInputNameWithType(int32 InputIndex, bool bWithType) const
{
	if (InputIndex < FunctionInputs.Num())
	{
		if (FunctionInputs[InputIndex].ExpressionInput != nullptr && bWithType)
		{
			return *FString::Printf(TEXT("%s (%s)"), *FunctionInputs[InputIndex].Input.InputName.ToString(), GetInputTypeName(FunctionInputs[InputIndex].ExpressionInput->InputType));
		}
		else
		{
			return FunctionInputs[InputIndex].Input.InputName;
		}
	}
	return NAME_None;
}

FName UMaterialExpressionMaterialFunctionCall::GetInputName(int32 InputIndex) const
{
	return GetInputNameWithType(InputIndex, true);
}

bool UMaterialExpressionMaterialFunctionCall::IsInputConnectionRequired(int32 InputIndex) const
{
	if (InputIndex < FunctionInputs.Num() && FunctionInputs[InputIndex].ExpressionInput != nullptr)
	{
		return !FunctionInputs[InputIndex].ExpressionInput->bUsePreviewValueAsDefault;
	}
	return true;
}

static FString GetInputDefaultValueString(EFunctionInputType InputType, const FVector4& PreviewValue)
{
	static_assert(FunctionInput_Scalar < FunctionInput_Vector4, "Enum values out of order.");
	check(InputType <= FunctionInput_Vector4);

	FString ValueString = FString::Printf(TEXT("DefaultValue = (%.2f"), PreviewValue.X);
	
	if (InputType >= FunctionInput_Vector2)
	{
		ValueString += FString::Printf(TEXT(", %.2f"), PreviewValue.Y);
	}

	if (InputType >= FunctionInput_Vector3)
	{
		ValueString += FString::Printf(TEXT(", %.2f"), PreviewValue.Z);
	}

	if (InputType >= FunctionInput_Vector4)
	{
		ValueString += FString::Printf(TEXT(", %.2f"), PreviewValue.W);
	}

	return ValueString + TEXT(")");
}

FString UMaterialExpressionMaterialFunctionCall::GetDescription() const
{
	FString Result = FString(*GetClass()->GetName()).Mid(FCString::Strlen(TEXT("MaterialExpression")));
	Result += TEXT(" (");
	Result += Super::GetDescription();
	Result += TEXT(")");
	return Result;
}

void UMaterialExpressionMaterialFunctionCall::GetConnectorToolTip(int32 InputIndex, int32 OutputIndex, TArray<FString>& OutToolTip) 
{
	if (MaterialFunction)
	{
		if (InputIndex != INDEX_NONE)
		{
			if (FunctionInputs.IsValidIndex(InputIndex))
			{
				UMaterialExpressionFunctionInput* InputExpression = FunctionInputs[InputIndex].ExpressionInput;

				ConvertToMultilineToolTip(InputExpression->Description, 40, OutToolTip);

				if (InputExpression->bUsePreviewValueAsDefault)
				{
					// Can't build a tooltip of an arbitrary expression chain
					if (InputExpression->Preview.Expression)
					{
						OutToolTip.Insert(FString(TEXT("DefaultValue = Custom expressions")), 0);

						// Add a line after the default value string
						OutToolTip.Insert(FString(TEXT("")), 1);
					}
					else if (InputExpression->InputType <= FunctionInput_Vector4)
					{
						// Add a string for the default value at the top
						OutToolTip.Insert(GetInputDefaultValueString((EFunctionInputType)InputExpression->InputType, InputExpression->PreviewValue), 0);

						// Add a line after the default value string
						OutToolTip.Insert(FString(TEXT("")), 1);
					}
				}
			}
		}
		else if (OutputIndex != INDEX_NONE)
		{
			if (FunctionOutputs.IsValidIndex(OutputIndex))
			{
				ConvertToMultilineToolTip(FunctionOutputs[OutputIndex].ExpressionOutput->Description, 40, OutToolTip);
			}
		}
	}
}

void UMaterialExpressionMaterialFunctionCall::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	if (MaterialFunction)
	{
		const FString* Description = MaterialFunction->GetDescription();
		ConvertToMultilineToolTip(Description ? *Description : TEXT(""), 40, OutToolTip);
	}
}

bool UMaterialExpressionMaterialFunctionCall::SetMaterialFunction(UMaterialFunctionInterface* NewMaterialFunction)
{
	// Remember the current material function
	UMaterialFunctionInterface* OldFunction = MaterialFunction;

	return SetMaterialFunctionEx(OldFunction, NewMaterialFunction);
}


bool UMaterialExpressionMaterialFunctionCall::SetMaterialFunctionEx(
	UMaterialFunctionInterface* OldFunctionResource, 
	UMaterialFunctionInterface* NewFunctionResource)
{
	// See if Outer is another material function
	UMaterialFunctionInterface* ThisFunctionResource = Cast<UMaterialFunction>(GetOuter());

	if (NewFunctionResource 
		&& ThisFunctionResource
		&& NewFunctionResource->IsDependent(ThisFunctionResource))
	{
		// Prevent recursive function call graphs
		FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("MaterialExpressions", "Error_CircularFunctionDependency", "Can't use that material function as it would cause a circular dependency.") );
		NewFunctionResource = nullptr;
	}

	MaterialFunction = NewFunctionResource;

	// Store the original inputs and outputs
	TArray<FFunctionExpressionInput> OriginalInputs = FunctionInputs;
	TArray<FFunctionExpressionOutput> OriginalOutputs = FunctionOutputs;

	FunctionInputs.Empty();
	FunctionOutputs.Empty();
	Outputs.Empty();

	if (NewFunctionResource)
	{
		// Get the current inputs and outputs
		NewFunctionResource->GetInputsAndOutputs(FunctionInputs, FunctionOutputs);

		for (int32 InputIndex = 0; InputIndex < FunctionInputs.Num(); InputIndex++)
		{
			FFunctionExpressionInput& CurrentInput = FunctionInputs[InputIndex];
			check(CurrentInput.ExpressionInput);
			const FFunctionExpressionInput* OriginalInput = FindInputByName(CurrentInput.ExpressionInput->InputName, OriginalInputs);

			if (OriginalInput)
			{
				// If there is an input whose name matches the original input, even if they are from different functions, maintain the connection
				CurrentInput.Input = OriginalInput->Input;
			}
		}

		for (int32 OutputIndex = 0; OutputIndex < FunctionOutputs.Num(); OutputIndex++)
		{
			Outputs.Add(FunctionOutputs[OutputIndex].Output);
		}
	}

	// Fixup even if NewFunctionResource is nullptr, because we have to clear old connections
	if (OldFunctionResource && OldFunctionResource != NewFunctionResource)
	{
		TArray<FExpressionInput*> MaterialInputs;
		if (Material)
		{
			MaterialInputs.Empty(MP_MAX);
			for (int32 InputIndex = 0; InputIndex < MP_MAX; InputIndex++)
			{
				auto Input = Material->GetExpressionInputForProperty((EMaterialProperty)InputIndex);

				if(Input)
				{
					MaterialInputs.Add(Input);
				}
			}

			// Fixup any references that the material or material inputs had to the function's outputs, maintaining links with the same output name
			FixupReferencingExpressions(FunctionOutputs, OriginalOutputs, Material->Expressions, MaterialInputs, true);
		}
		else if (Function)
		{
			// Fixup any references that the material function had to the function's outputs, maintaining links with the same output name
			FixupReferencingExpressions(FunctionOutputs, OriginalOutputs, Function->FunctionExpressions, MaterialInputs, true);
		}
	}

	if (GraphNode)
	{
		// Recreate the pins of this node after material function set
		CastChecked<UMaterialGraphNode>(GraphNode)->RecreateAndLinkNode();
	}

	return NewFunctionResource != nullptr;
}

void UMaterialExpressionMaterialFunctionCall::UpdateFromFunctionResource(bool bRecreateAndLinkNode)
{
	TArray<FFunctionExpressionInput> OriginalInputs = MoveTemp(FunctionInputs);
	TArray<FFunctionExpressionOutput> OriginalOutputs = MoveTemp(FunctionOutputs);

	FunctionInputs.Reserve(OriginalInputs.Num());
	FunctionOutputs.Reserve(OriginalOutputs.Num());
	Outputs.Reset();

	if (MaterialFunction)
	{
		// Recursively update any function call nodes in the function
		MaterialFunction->UpdateFromFunctionResource();

		// Get the function's current inputs and outputs
		MaterialFunction->GetInputsAndOutputs(FunctionInputs, FunctionOutputs);

		for (int32 InputIndex = 0; InputIndex < FunctionInputs.Num(); InputIndex++)
		{
			FFunctionExpressionInput& CurrentInput = FunctionInputs[InputIndex];
			check(CurrentInput.ExpressionInput);
			const FFunctionExpressionInput* OriginalInput = FindInputById(CurrentInput.ExpressionInputId, OriginalInputs);

			if (OriginalInput)
			{
				// Maintain the input connection if an input with matching Id is found, but propagate the new name
				// This way function inputs names can be changed without affecting material connections
				const FName TempInputName = CurrentInput.Input.InputName;
				CurrentInput.Input = OriginalInput->Input;
				CurrentInput.Input.InputName = TempInputName;
			}
		}

		for (int32 OutputIndex = 0; OutputIndex < FunctionOutputs.Num(); OutputIndex++)
		{
			Outputs.Add(FunctionOutputs[OutputIndex].Output);
		}

		TArray<FExpressionInput*> MaterialInputs;
		if (Material)
		{
			MaterialInputs.Empty(MP_MAX);
			for (int32 InputIndex = 0; InputIndex < MP_MAX; InputIndex++)
			{
				auto Input = Material->GetExpressionInputForProperty((EMaterialProperty)InputIndex);

				if(Input)
				{
					MaterialInputs.Add(Input);
				}
			}
			
			// Fixup any references that the material or material inputs had to the function's outputs
			FixupReferencingExpressions(FunctionOutputs, OriginalOutputs, Material->Expressions, MaterialInputs, false);
		}
		else if (Function)
		{
			// Fixup any references that the material function had to the function's outputs
			FixupReferencingExpressions(FunctionOutputs, OriginalOutputs, Function->FunctionExpressions, MaterialInputs, false);
		}
	}

	if (GraphNode && bRecreateAndLinkNode)
	{
		// Check whether number of input/outputs or transient pointers have changed
		bool bUpdatedFromFunction = false;
		if (OriginalInputs.Num() != FunctionInputs.Num()
			|| OriginalOutputs.Num() != FunctionOutputs.Num()
			|| OriginalOutputs.Num() != Outputs.Num())
		{
			bUpdatedFromFunction = true;
		}
		for (int32 Index = 0; Index < OriginalInputs.Num() && !bUpdatedFromFunction; ++Index)
		{
			if (OriginalInputs[Index].ExpressionInput != FunctionInputs[Index].ExpressionInput)
			{
				bUpdatedFromFunction = true;
			}
		}
		for (int32 Index = 0; Index < OriginalOutputs.Num() && !bUpdatedFromFunction; ++Index)
		{
			if (OriginalOutputs[Index].ExpressionOutput != FunctionOutputs[Index].ExpressionOutput)
			{
				bUpdatedFromFunction = true;
			}
		}
		if (bUpdatedFromFunction)
		{
			// Recreate the pins of this node after Expression links are made
			CastChecked<UMaterialGraphNode>(GraphNode)->RecreateAndLinkNode();
		}
	}
}

/** Goes through the Inputs array and fixes up each input's OutputIndex, or breaks the connection if necessary. */
static void FixupReferencingInputs(
	const TArray<FFunctionExpressionOutput>& NewOutputs,
	const TArray<FFunctionExpressionOutput>& OriginalOutputs,
	const TArray<FExpressionInput*>& Inputs, 
	UMaterialExpressionMaterialFunctionCall* FunctionExpression,
	bool bMatchByName)
{
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); InputIndex++)
	{
		FExpressionInput* CurrentInput = Inputs[InputIndex];

		if (CurrentInput->Expression == FunctionExpression)
		{
			if (OriginalOutputs.IsValidIndex(CurrentInput->OutputIndex))
			{
				if (bMatchByName)
				{
					CurrentInput->OutputIndex = FindOutputIndexByName(OriginalOutputs[CurrentInput->OutputIndex].ExpressionOutput->OutputName, NewOutputs);
				}
				else
				{
					const FGuid OutputId = OriginalOutputs[CurrentInput->OutputIndex].ExpressionOutputId;
					CurrentInput->OutputIndex = FindOutputIndexById(OutputId, NewOutputs);
				}

				if (CurrentInput->OutputIndex == INDEX_NONE)
				{
					// The output that this input was connected to no longer exists, break the connection
					CurrentInput->Expression = nullptr;
				}
			}
			else
			{
				// The output that this input was connected to no longer exists, break the connection
				CurrentInput->OutputIndex = INDEX_NONE;
				CurrentInput->Expression = nullptr;
			}
		}
	}
}


void UMaterialExpressionMaterialFunctionCall::FixupReferencingExpressions(
	const TArray<FFunctionExpressionOutput>& NewOutputs,
	const TArray<FFunctionExpressionOutput>& OriginalOutputs,
	TArray<UMaterialExpression*>& Expressions, 
	TArray<FExpressionInput*>& MaterialInputs,
	bool bMatchByName)
{
	for (int32 ExpressionIndex = 0; ExpressionIndex < Expressions.Num(); ExpressionIndex++)
	{
		UMaterialExpression* CurrentExpression = Expressions[ExpressionIndex];
		if (CurrentExpression)
		{
			TArray<FExpressionInput*> Inputs = CurrentExpression->GetInputs();
			FixupReferencingInputs(NewOutputs, OriginalOutputs, Inputs, this, bMatchByName);
		}
	}

	FixupReferencingInputs(NewOutputs, OriginalOutputs, MaterialInputs, this, bMatchByName);
}

bool UMaterialExpressionMaterialFunctionCall::MatchesSearchQuery( const TCHAR* SearchQuery )
{
	if (MaterialFunction && MaterialFunction->GetName().Contains(SearchQuery) )
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

bool UMaterialExpressionMaterialFunctionCall::IsResultMaterialAttributes(int32 OutputIndex)
{
	if( OutputIndex >= 0 && OutputIndex < FunctionOutputs.Num() && FunctionOutputs[OutputIndex].ExpressionOutput)
	{
		return FunctionOutputs[OutputIndex].ExpressionOutput->IsResultMaterialAttributes(0);
	}
	else
	{
		return false;
	}
}

uint32 UMaterialExpressionMaterialFunctionCall::GetInputType(int32 InputIndex)
{
	if (InputIndex < FunctionInputs.Num())
	{
		if (FunctionInputs[InputIndex].ExpressionInput)
		{
			return FunctionInputs[InputIndex].ExpressionInput->GetInputType(0);
		}
	}
	return MCT_Unknown;
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionFunctionInput
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionFunctionInput::UMaterialExpressionFunctionInput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Functions;
		FConstructorStatics()
			: NAME_Functions(LOCTEXT( "Functions", "Functions" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#endif

	bCompilingFunctionPreview = true;
	InputType = FunctionInput_Vector3;
	InputName = TEXT("In");

#if WITH_EDITORONLY_DATA
	bCollapsed = false;

	MenuCategories.Add(ConstructorStatics.NAME_Functions);
#endif
}

void UMaterialExpressionFunctionInput::PostLoad()
{
	Super::PostLoad();
	ConditionallyGenerateId(false);
}

void UMaterialExpressionFunctionInput::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	ConditionallyGenerateId(false);
}

#if WITH_EDITOR

void UMaterialExpressionFunctionInput::PostEditImport()
{
	Super::PostEditImport();
	ConditionallyGenerateId(true);
}

FName InputNameBackup;

void UMaterialExpressionFunctionInput::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UMaterialExpressionFunctionInput, InputName))
	{
		InputNameBackup = InputName;
	}
	Super::PreEditChange(PropertyAboutToChange);
}

void UMaterialExpressionFunctionInput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UMaterialExpressionFunctionInput, InputName))
	{
		if (Material)
		{
			for (int32 ExpressionIndex = 0; ExpressionIndex < Material->Expressions.Num(); ExpressionIndex++)
			{
				UMaterialExpressionFunctionInput* OtherFunctionInput = Cast<UMaterialExpressionFunctionInput>(Material->Expressions[ExpressionIndex]);
				if (OtherFunctionInput && OtherFunctionInput != this && OtherFunctionInput->InputName == InputName)
				{
					FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_InputNamesMustBeUnique", "Function input names must be unique"));
					InputName = InputNameBackup;
					break;
				}
			}
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FEditorSupportDelegates::ForcePropertyWindowRebuild.Broadcast(this);
}

void UMaterialExpressionFunctionInput::GetCaption(TArray<FString>& OutCaptions) const
{
	const static TCHAR* TypeNames[FunctionInput_MAX] =
	{
		TEXT("Scalar"),
		TEXT("Vector2"),
		TEXT("Vector3"),
		TEXT("Vector4"),
		TEXT("Texture2D"),
		TEXT("TextureCube"),
		TEXT("Texture2DArray"),
		TEXT("VolumeTexture"),
		TEXT("StaticBool"),
		TEXT("MaterialAttributes"),
		TEXT("External")
	};
	check(InputType < FunctionInput_MAX);
	OutCaptions.Add(FString(TEXT("Input ")) + InputName.ToString() + TEXT(" (") + TypeNames[InputType] + TEXT(")"));
}

void UMaterialExpressionFunctionInput::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(Description, 40, OutToolTip);
}

int32 UMaterialExpressionFunctionInput::CompilePreviewValue(FMaterialCompiler* Compiler)
{
	if (Preview.GetTracedInput().Expression)
	{
		int32 ExpressionResult;
		if (Preview.Expression->GetOuter() == GetOuter())
		{
			ExpressionResult = Preview.Compile(Compiler);
		}
		else
		{
			FMaterialFunctionCompileState* FunctionState = Compiler->PopFunction();
			ExpressionResult = Preview.Compile(Compiler);
			Compiler->PushFunction(FunctionState);
		}
		return ExpressionResult;
	}
	else
	{
		const FGuid AttributeID = Compiler->GetMaterialAttribute();

		// Compile PreviewValue if Preview was not connected
		switch (InputType)
		{
		case FunctionInput_Scalar:
			return Compiler->Constant(PreviewValue.X);
		case FunctionInput_Vector2:
			return Compiler->Constant2(PreviewValue.X, PreviewValue.Y);
		case FunctionInput_Vector3:
			return Compiler->Constant3(PreviewValue.X, PreviewValue.Y, PreviewValue.Z);
		case FunctionInput_Vector4:
			return Compiler->Constant4(PreviewValue.X, PreviewValue.Y, PreviewValue.Z, PreviewValue.W);
		case FunctionInput_MaterialAttributes:		
			return FMaterialAttributeDefinitionMap::CompileDefaultExpression(Compiler, AttributeID);
		case FunctionInput_Texture2D:
		case FunctionInput_TextureCube:
		case FunctionInput_Texture2DArray:
		case FunctionInput_TextureExternal:
		case FunctionInput_StaticBool:
			return Compiler->Errorf(TEXT("Missing Preview connection for function input '%s'"), *InputName.ToString());
		default:
			return Compiler->Errorf(TEXT("Unknown input type"));
		}
	}
}

int32 UMaterialExpressionFunctionInput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const static EMaterialValueType FunctionTypeMapping[FunctionInput_MAX] =
	{
		MCT_Float1,
		MCT_Float2,
		MCT_Float3,
		MCT_Float4,
		MCT_Texture2D,
		MCT_TextureCube,
		MCT_Texture2DArray,
		MCT_VolumeTexture,
		MCT_StaticBool,
		MCT_MaterialAttributes,
		MCT_TextureExternal
	};
	check(InputType < FunctionInput_MAX);

	// If we are being compiled as part of a material which calls this function
	FExpressionInput EffectivePreviewDuringCompileTracedInput = EffectivePreviewDuringCompile.GetTracedInput();
	if (EffectivePreviewDuringCompileTracedInput.Expression && !bCompilingFunctionPreview)
	{
		int32 ExpressionResult;

		// Stay in this function if we are compiling an expression that is in the current function
		// This can happen if bUsePreviewValueAsDefault is true and the calling material didn't override the input
		if (bUsePreviewValueAsDefault && EffectivePreviewDuringCompileTracedInput.Expression->GetOuter() == GetOuter())
		{
			// Compile the function input
			ExpressionResult = EffectivePreviewDuringCompile.Compile(Compiler);
		}
		else
		{
			// Tell the compiler that we are leaving the function
			FMaterialFunctionCompileState* FunctionState = Compiler->PopFunction();

			// Backup EffectivePreviewDuringCompile which will be modified by UnlinkFromCaller and LinkIntoCaller of any potential chained function calls to the same function
			FExpressionInput LocalPreviewDuringCompile = EffectivePreviewDuringCompile;

			// Restore the function since we are leaving it
			FunctionState->FunctionCall->UnlinkFunctionFromCaller(Compiler);

			// Compile the function input
			ExpressionResult = LocalPreviewDuringCompile.Compile(Compiler);

			// Link the function's inputs into the caller graph before entering
			FunctionState->FunctionCall->LinkFunctionIntoCaller(Compiler);

			// Tell the compiler that we are re-entering the function
			Compiler->PushFunction(FunctionState);
		}

		// Cast to the type that the function author specified
		// This will truncate (float4 -> float3) but not add components (float2 -> float3)
		return Compiler->ValidCast(ExpressionResult, FunctionTypeMapping[InputType]);
	}
	else
	{
		if (bCompilingFunctionPreview || bUsePreviewValueAsDefault)
		{
			// If we are compiling the function in a preview material, such as when editing the function,
			// Compile the preview value or texture and output a texture object.
			return Compiler->ValidCast(CompilePreviewValue(Compiler), FunctionTypeMapping[InputType]);
		}
		else
		{
			return Compiler->Errorf(TEXT("Missing function input '%s'"), *InputName.ToString());
		}
	}
}

int32 UMaterialExpressionFunctionInput::CompilePreview(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Compile the preview value, outputting a float type
	return Compiler->ValidCast(CompilePreviewValue(Compiler), MCT_Float3);
}
#endif // WITH_EDITOR

void UMaterialExpressionFunctionInput::ConditionallyGenerateId(bool bForce)
{
	if (bForce || !Id.IsValid())
	{
		Id = FGuid::NewGuid();
	}
}

#if WITH_EDITOR
void UMaterialExpressionFunctionInput::ValidateName()
{
	if (Material)
	{
		int32 InputNameIndex = 1;
		bool bResultNameIndexValid = true;
		FName PotentialInputName;

		// Find an available unique name
		do 
		{
			PotentialInputName = InputName;
			if (InputNameIndex != 1)
			{
				PotentialInputName.SetNumber(InputNameIndex);
			}

			bResultNameIndexValid = true;
			for (int32 ExpressionIndex = 0; ExpressionIndex < Material->Expressions.Num(); ExpressionIndex++)
			{
				UMaterialExpressionFunctionInput* OtherFunctionInput = Cast<UMaterialExpressionFunctionInput>(Material->Expressions[ExpressionIndex]);
				if (OtherFunctionInput && OtherFunctionInput != this && OtherFunctionInput->InputName == PotentialInputName)
				{
					bResultNameIndexValid = false;
					break;
				}
			}

			InputNameIndex++;
		} 
		while (!bResultNameIndexValid);

		InputName = PotentialInputName;
	}
}

bool UMaterialExpressionFunctionInput::IsResultMaterialAttributes(int32 OutputIndex)
{
	if( FunctionInput_MaterialAttributes == InputType )
	{
		return true;
	}
	else
	{
		return false;
	}
}

uint32 UMaterialExpressionFunctionInput::GetInputType(int32 InputIndex)
{
	switch (InputType)
	{
	case FunctionInput_Scalar:
		return MCT_Float;
	case FunctionInput_Vector2:
		return MCT_Float2;
	case FunctionInput_Vector3:
		return MCT_Float3;
	case FunctionInput_Vector4:
		return MCT_Float4;
	case FunctionInput_Texture2D:
		return MCT_Texture2D;
	case FunctionInput_TextureCube:
		return MCT_TextureCube;
	case FunctionInput_Texture2DArray:
		return MCT_Texture2DArray;
	case FunctionInput_TextureExternal:
		return MCT_TextureExternal;
	case FunctionInput_VolumeTexture:
		return MCT_VolumeTexture;
	case FunctionInput_StaticBool:
		return MCT_StaticBool;
	case FunctionInput_MaterialAttributes:
		return MCT_MaterialAttributes;
	default:
		return MCT_Unknown;
	}
}

uint32 UMaterialExpressionFunctionInput::GetOutputType(int32 OutputIndex)
{
	return GetInputType(0);
}
#endif // WITH_EDITOR


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionFunctionOutput
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionFunctionOutput::UMaterialExpressionFunctionOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Functions;
		FConstructorStatics()
			: NAME_Functions(LOCTEXT( "Functions", "Functions" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bShowOutputs = false;
#endif

	OutputName = TEXT("Result");

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Functions);
	bCollapsed = false;
#endif
}

void UMaterialExpressionFunctionOutput::PostLoad()
{
	Super::PostLoad();
	ConditionallyGenerateId(false);
}

void UMaterialExpressionFunctionOutput::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	// Ideally we would like to regenerate the Id here, but this is used when propagating 
	// To the preview material function when editing a material function and back
	// So instead we regenerate the Id when copy pasting in the material editor, see UMaterialExpression::CopyMaterialExpressions
	ConditionallyGenerateId(false);
}

#if WITH_EDITOR
void UMaterialExpressionFunctionOutput::PostEditImport()
{
	Super::PostEditImport();
	ConditionallyGenerateId(true);
}
#endif	//#if WITH_EDITOR

FName OutputNameBackup;

#if WITH_EDITOR
void UMaterialExpressionFunctionOutput::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UMaterialExpressionFunctionOutput, OutputName))
	{
		OutputNameBackup = OutputName;
	}
	Super::PreEditChange(PropertyAboutToChange);
}

void UMaterialExpressionFunctionOutput::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UMaterialExpressionFunctionOutput, OutputName))
	{
		if (Material)
		{
			for (int32 ExpressionIndex = 0; ExpressionIndex < Material->Expressions.Num(); ExpressionIndex++)
			{
				UMaterialExpressionFunctionOutput* OtherFunctionOutput = Cast<UMaterialExpressionFunctionOutput>(Material->Expressions[ExpressionIndex]);
				if (OtherFunctionOutput && OtherFunctionOutput != this && OtherFunctionOutput->OutputName == OutputName)
				{
					FMessageDialog::Open( EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "Error_OutputNamesMustBeUnique", "Function output names must be unique"));
					OutputName = OutputNameBackup;
					break;
				}
			}
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UMaterialExpressionFunctionOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Output ")) + OutputName.ToString());
}

void UMaterialExpressionFunctionOutput::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(Description, 40, OutToolTip);
}

uint32 UMaterialExpressionFunctionOutput::GetInputType(int32 InputIndex)
{
	// Acceptable types for material function outputs
	return MCT_Float | MCT_MaterialAttributes;
}

int32 UMaterialExpressionFunctionOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing function output '%s'"), *OutputName.ToString());
	}
	return A.Compile(Compiler);
}
#endif // WITH_EDITOR

void UMaterialExpressionFunctionOutput::ConditionallyGenerateId(bool bForce)
{
	if (bForce || !Id.IsValid())
	{
		Id = FGuid::NewGuid();
	}
}

#if WITH_EDITOR
void UMaterialExpressionFunctionOutput::ValidateName()
{
	if (Material)
	{
		int32 OutputNameIndex = 1;
		bool bResultNameIndexValid = true;
		FName PotentialOutputName;

		// Find an available unique name
		do 
		{
			PotentialOutputName = OutputName;
			if (OutputNameIndex != 1)
			{
				PotentialOutputName.SetNumber(OutputNameIndex);
			}

			bResultNameIndexValid = true;
			for (int32 ExpressionIndex = 0; ExpressionIndex < Material->Expressions.Num(); ExpressionIndex++)
			{
				UMaterialExpressionFunctionOutput* OtherFunctionOutput = Cast<UMaterialExpressionFunctionOutput>(Material->Expressions[ExpressionIndex]);
				if (OtherFunctionOutput && OtherFunctionOutput != this && OtherFunctionOutput->OutputName == PotentialOutputName)
				{
					bResultNameIndexValid = false;
					break;
				}
			}

			OutputNameIndex++;
		} 
		while (!bResultNameIndexValid);

		OutputName = PotentialOutputName;
	}
}

bool UMaterialExpressionFunctionOutput::IsResultMaterialAttributes(int32 OutputIndex)
{
	// If there is a loop anywhere in this expression's inputs then we can't risk checking them
	if( A.GetTracedInput().Expression && !A.Expression->ContainsInputLoop() )
	{
		return A.Expression->IsResultMaterialAttributes(A.OutputIndex);
	}
	else
	{
		return false;
	}
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionMaterialLayerOutput
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionMaterialLayerOutput::UMaterialExpressionMaterialLayerOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	OutputName = TEXT("Material Attributes");
}


//
//	UMaterialExpressionCollectionParameter
//
UMaterialExpressionCollectionParameter::UMaterialExpressionCollectionParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Parameters;
		FConstructorStatics()
			: NAME_Parameters(LOCTEXT( "Parameters", "Parameters" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Parameters);

	bCollapsed = false;
#endif
}


void UMaterialExpressionCollectionParameter::PostLoad()
{
	if (Collection)
	{
		Collection->ConditionalPostLoad();
		ParameterName = Collection->GetParameterName(ParameterId);
	}

	Super::PostLoad();
}

#if WITH_EDITOR
void UMaterialExpressionCollectionParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (Collection)
	{
		ParameterId = Collection->GetParameterId(ParameterName);
	}
	else
	{
		ParameterId = FGuid();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

int32 UMaterialExpressionCollectionParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 ParameterIndex = -1;
	int32 ComponentIndex = -1;

	if (Collection)
	{
		Collection->GetParameterIndex(ParameterId, ParameterIndex, ComponentIndex);
	}

	if (ParameterIndex != -1)
	{
		return Compiler->AccessCollectionParameter(Collection, ParameterIndex, ComponentIndex);
	}
	else
	{
		if (!Collection)
		{
			return Compiler->Errorf(TEXT("CollectionParameter has invalid Collection!"));
		}
		else
		{
			return Compiler->Errorf(TEXT("CollectionParameter has invalid parameter %s"), *ParameterName.ToString());
		}
	}
}

void UMaterialExpressionCollectionParameter::GetCaption(TArray<FString>& OutCaptions) const
{
	FString TypePrefix;

	if (Collection)
	{
		int32 ParameterIndex = -1;
		int32 ComponentIndex = -1;
		Collection->GetParameterIndex(ParameterId, ParameterIndex, ComponentIndex);

		if (ComponentIndex == -1)
		{
			TypePrefix = TEXT("(float4) ");
		}
		else
		{
			TypePrefix = TEXT("(float1) ");
		}
	}

	OutCaptions.Add(TypePrefix + TEXT("Collection Param"));

	if (Collection)
	{
		OutCaptions.Add(Collection->GetName());
		OutCaptions.Add(FString(TEXT("'")) + ParameterName.ToString() + TEXT("'"));
	}
	else
	{
		OutCaptions.Add(TEXT("Unspecified"));
	}
}


bool UMaterialExpressionCollectionParameter::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	if (ParameterName.ToString().Contains(SearchQuery))
	{
		return true;
	}

	if (Collection && Collection->GetName().Contains(SearchQuery))
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}
#endif // WITH_EDITOR
//
//	UMaterialExpressionLightmapUVs
//
UMaterialExpressionLightmapUVs::UMaterialExpressionLightmapUVs(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bShowOutputNameOnPin = true;
	bHidePreviewWindow = true;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 0, 0));

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLightmapUVs::Compile( FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->LightmapUVs();
}

	
void UMaterialExpressionLightmapUVs::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("LightmapUVs"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionAOMaterialMask
//
UMaterialExpressionPrecomputedAOMask::UMaterialExpressionPrecomputedAOMask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	bShowOutputNameOnPin = true;
	bHidePreviewWindow = true;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("")));

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPrecomputedAOMask::Compile( FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->PrecomputedAOMask();
}

	
void UMaterialExpressionPrecomputedAOMask::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("PrecomputedAOMask"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionLightmassReplace
//
UMaterialExpressionLightmassReplace::UMaterialExpressionLightmassReplace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLightmassReplace::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Realtime.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing LightmassReplace input Realtime"));
	}
	else if (!Lightmass.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing LightmassReplace input Lightmass"));
	}
	else
	{
		return Compiler->IsLightmassCompiler() ? Lightmass.Compile(Compiler) : Realtime.Compile(Compiler);
	}
}

void UMaterialExpressionLightmassReplace::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("LightmassReplace"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionShadowReplace
//
UMaterialExpressionShadowReplace::UMaterialExpressionShadowReplace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionShadowReplace::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Default.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input Default"));
	}
	else if (!Shadow.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input Shadow"));
	}
	else
	{
		const int32 Arg1 = Default.Compile(Compiler);
		const int32 Arg2 = Shadow.Compile(Compiler);
		return Compiler->ShadowReplace(Arg1, Arg2);
	}
}

void UMaterialExpressionShadowReplace::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Shadow Pass Switch"));
}

void UMaterialExpressionShadowReplace::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Allows material to define specialized behavior when being rendered into ShadowMap."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionShaderStageSwitch
//
UMaterialExpressionShaderStageSwitch::UMaterialExpressionShaderStageSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionShaderStageSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!PixelShader.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input PixelShader"));
	}
	else if (!VertexShader.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input VertexShader"));
	}
	else
	{
		const EShaderFrequency ShaderFrequency = Compiler->GetCurrentShaderFrequency();
		return ShouldUsePixelShaderInput(ShaderFrequency) ? PixelShader.Compile(Compiler) : VertexShader.Compile(Compiler);
	}
}


void UMaterialExpressionShaderStageSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Shader Stage Switch"));
}

void UMaterialExpressionShaderStageSwitch::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Allows material to define specialized behavior for certain shader stages."), 40, OutToolTip);
}
#endif // WITH_EDITOR


//
//	UMaterialExpressionMaterialProxy
//
UMaterialExpressionMaterialProxyReplace::UMaterialExpressionMaterialProxyReplace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMaterialProxyReplace::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Realtime.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing MaterialProxyReplace input Realtime"));
	}
	else if (!MaterialProxy.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing MaterialProxyReplace input MaterialProxy"));
	}
	else
	{
		return Compiler->IsMaterialProxyCompiler() ? MaterialProxy.Compile(Compiler) : Realtime.Compile(Compiler);
	}
}

bool UMaterialExpressionMaterialProxyReplace::IsResultMaterialAttributes(int32 OutputIndex)
{
	for (FExpressionInput* ExpressionInput : GetInputs())
	{
		if (ExpressionInput->GetTracedInput().Expression && !ExpressionInput->Expression->ContainsInputLoop() && ExpressionInput->Expression->IsResultMaterialAttributes(ExpressionInput->OutputIndex))
		{
			return true;
		}
	}
	return false;
}

void UMaterialExpressionMaterialProxyReplace::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("MaterialProxyReplace"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionGIReplace
//
UMaterialExpressionGIReplace::UMaterialExpressionGIReplace(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionGIReplace::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	FExpressionInput& LocalStaticIndirect = StaticIndirect.GetTracedInput().Expression ? StaticIndirect : Default;
	FExpressionInput& LocalDynamicIndirect = DynamicIndirect.GetTracedInput().Expression ? DynamicIndirect : Default;

	if(!Default.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing GIReplace input 'Default'"));
	}
	else
	{
		int32 Arg1 = Default.Compile(Compiler);
		int32 Arg2 = LocalStaticIndirect.Compile(Compiler);
		int32 Arg3 = LocalDynamicIndirect.Compile(Compiler);
		return Compiler->GIReplace(Arg1, Arg2, Arg3);
	}
}

void UMaterialExpressionGIReplace::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("GIReplace"));
}
#endif // WITH_EDITOR
//
// UMaterialExpressionRayTracingQualitySwitch
//
UMaterialExpressionRayTracingQualitySwitch::UMaterialExpressionRayTracingQualitySwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionRayTracingQualitySwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Normal.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RayTracingQualitySwitch input 'Normal'"));
	}
	else if (!RayTraced.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RayTracingQualitySwitch input 'RayTracing'"));
	}
	else
	{
		int32 Arg1 = Normal.Compile(Compiler);
		int32 Arg2 = RayTraced.Compile(Compiler);

		//only when both of these are real expressions do the actual code.  otherwise various output pins will
		//end up considered 'set' when really we just want a default.  This can cause us to force depth output when we don't want it for example.
		if (Arg1 != INDEX_NONE && Arg2 != INDEX_NONE)
		{
			return Compiler->RayTracingQualitySwitchReplace(Arg1, Arg2);
		}
		else if (Arg1 != INDEX_NONE)
		{
			return Arg1;
		}
		else if (Arg2 != INDEX_NONE)
		{
			return Arg2;
		}
		return INDEX_NONE;
	}
}

bool UMaterialExpressionRayTracingQualitySwitch::IsResultMaterialAttributes(int32 OutputIndex)
{
	if (Normal.Expression)
	{
		return Normal.Expression->IsResultMaterialAttributes(OutputIndex);
	}
	return false;
}

void UMaterialExpressionRayTracingQualitySwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("RayTracingQualitySwitchReplace"));
}

uint32 UMaterialExpressionRayTracingQualitySwitch::GetInputType(int32 InputIndex)
{
	return MCT_Unknown;
}
#endif // WITH_EDITOR

UMaterialExpressionObjectOrientation::UMaterialExpressionObjectOrientation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionObjectOrientation::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ObjectOrientation();
}

void UMaterialExpressionObjectOrientation::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("ObjectOrientation"));
}
#endif // WITH_EDITOR

UMaterialExpressionRerouteBase::UMaterialExpressionRerouteBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UMaterialExpression* UMaterialExpressionRerouteBase::TraceInputsToRealExpression(int32& OutputIndex) const
{
#if WITH_EDITORONLY_DATA
	TSet<FMaterialExpressionKey> VisitedExpressions;
	FExpressionInput RealInput = TraceInputsToRealExpressionInternal(VisitedExpressions);
	OutputIndex = RealInput.OutputIndex;
	return RealInput.Expression;
#else
	OutputIndex = 0;
	return nullptr;
#endif
}

FExpressionInput UMaterialExpressionRerouteBase::TraceInputsToRealInput() const
{
	TSet<FMaterialExpressionKey> VisitedExpressions;
	FExpressionInput RealInput = TraceInputsToRealExpressionInternal(VisitedExpressions);
	return RealInput;
}

FExpressionInput UMaterialExpressionRerouteBase::TraceInputsToRealExpressionInternal(TSet<FMaterialExpressionKey>& VisitedExpressions) const
{
#if WITH_EDITORONLY_DATA
	FExpressionInput Input;
	if (GetRerouteInput(Input))
	{
		// First check to see if this is a terminal node, if it is then we have a reroute to nowhere.
		if (Input.Expression != nullptr)
		{
			// Now check to see if we're also connected to another reroute. If we are, then keep going unless we hit a loop condition.
			UMaterialExpressionRerouteBase* RerouteInput = Cast<UMaterialExpressionRerouteBase>(Input.Expression);
			if (RerouteInput != nullptr)
			{
				FMaterialExpressionKey InputExpressionKey(Input.Expression, Input.OutputIndex);
				// prevent recurring visits to expressions we've already checked
				if (VisitedExpressions.Contains(InputExpressionKey))
				{
					// We have a loop! This should result in not finding the value!
					return FExpressionInput();
				}
				else
				{
					VisitedExpressions.Add(InputExpressionKey);
					FExpressionInput OutputExpressionInput = RerouteInput->TraceInputsToRealExpressionInternal(VisitedExpressions);
					return OutputExpressionInput;
				}
			}
			else
			{
				// We aren't connected to another Reroute, so we are good.
				return Input;
			}
		}
	}
#endif // WITH_EDITORONLY_DATA
	// We went to nowhere, so bail out.
	return FExpressionInput();
}

#if WITH_EDITOR
uint32 UMaterialExpressionRerouteBase::GetInputType(int32 InputIndex)
{
	FExpressionInput Input;
	if (GetRerouteInput(Input))
	{
		// Our input type should match the node that we are ultimately connected to, no matter how many reroute nodes lie between us.
		if (InputIndex == 0 && Input.IsConnected() && Input.Expression != nullptr)
		{
			int32 RealExpressionOutputIndex = -1;
			UMaterialExpression* RealExpression = TraceInputsToRealExpression(RealExpressionOutputIndex);

			// If we found a valid connection to a real output, then our type becomes that type.
			if (RealExpression != nullptr && RealExpressionOutputIndex != -1 && RealExpression->Outputs.Num() > RealExpressionOutputIndex && RealExpressionOutputIndex >= 0)
			{
				return RealExpression->GetOutputType(RealExpressionOutputIndex);
			}
		}
	}
	return MCT_Unknown;
}

uint32 UMaterialExpressionRerouteBase::GetOutputType(int32 OutputIndex)
{
	// Our node is a passthrough so input and output types must match.
	return GetInputType(0);
}

bool UMaterialExpressionRerouteBase::IsResultMaterialAttributes(int32 OutputIndex)
{
	FExpressionInput Input;
	if (GetRerouteInput(Input))
	{
		// Most code checks to make sure that there aren't loops before going here. In our case, we rely on the fact that
		// UMaterialExpressionReroute's implementation of TraceInputsToRealExpression is resistant to input loops.
		if (Input.IsConnected() && Input.Expression != nullptr && OutputIndex == 0)
		{
			int32 RealExpressionOutputIndex = -1;
			UMaterialExpression* RealExpression = TraceInputsToRealExpression(RealExpressionOutputIndex);
			if (RealExpression != nullptr)
			{
				return RealExpression->IsResultMaterialAttributes(RealExpressionOutputIndex);
			}
		}
	}

	return false;
}
#endif // WITH_EDITOR


UMaterialExpressionReroute::UMaterialExpressionReroute(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionReroute::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Because we don't want to generate *any* additional instructions, we just forward this request
	// to the node that this input is connected to. If it isn't connected, then the compile will return INDEX_NONE.
	int32 Result = Input.Compile(Compiler);
	return Result;
}

void UMaterialExpressionReroute::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Reroute Node (reroutes wires)"));
}


FText UMaterialExpressionReroute::GetCreationDescription() const 
{
	return LOCTEXT("RerouteNodeCreationDesc", "This node looks like a single pin and can be used to tidy up your graph by adding a movable control point to the connection spline.");
}

FText UMaterialExpressionReroute::GetCreationName() const
{
	return LOCTEXT("RerouteNodeCreationName", "Add Reroute Node...");
}
#endif // WITH_EDITOR

bool UMaterialExpressionReroute::GetRerouteInput(FExpressionInput& OutInput) const
{
	OutInput = Input;
	return true;
}

UMaterialExpressionNamedRerouteBase::UMaterialExpressionNamedRerouteBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UMaterialExpressionNamedRerouteDeclaration* UMaterialExpressionNamedRerouteBase::FindDeclarationInArray(const FGuid& VariableGuid, const TArray<UMaterialExpression*>& Expressions) const
{
	for (UMaterialExpression* Expression : Expressions)
	{
		auto* Declaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression);
		if (Declaration && this != Declaration && Declaration->VariableGuid == VariableGuid)
		{
			return Declaration;
		}
	}
	return nullptr;
}

UMaterialExpressionNamedRerouteDeclaration* UMaterialExpressionNamedRerouteBase::FindDeclarationInMaterial(const FGuid& VariableGuid) const
{
	UMaterialExpressionNamedRerouteDeclaration* Declaration = nullptr;
#if WITH_EDITORONLY_DATA
	if (Material)
	{
		Declaration = FindDeclarationInArray(VariableGuid, Material->Expressions);
	}
	else if (Function) // Material should always be valid, but just in case also check Function
	{
		Declaration = FindDeclarationInArray(VariableGuid, Function->FunctionExpressions);
	}
#endif // WITH_EDITORONLY_DATA
	return Declaration;
}

UMaterialExpressionNamedRerouteDeclaration::UMaterialExpressionNamedRerouteDeclaration(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
	NodeColor = FLinearColor::MakeRandomColor();
#endif
	Name = TEXT("Name");
}

void UMaterialExpressionNamedRerouteDeclaration::PostInitProperties()
{
	Super::PostInitProperties();
	// Init the GUID
	UpdateVariableGuid(false, false);
}

void UMaterialExpressionNamedRerouteDeclaration::PostLoad()
{
	Super::PostLoad();
	// Init the GUID
	UpdateVariableGuid(false, false);
}

void UMaterialExpressionNamedRerouteDeclaration::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	// We do not force a guid regen here because this function is used when the Material Editor makes a copy of a material to edit.
	// If we forced a GUID regen, it would cause all of the guids for a material to change every time a material was edited.
	UpdateVariableGuid(false, true);
}

#if WITH_EDITOR
void UMaterialExpressionNamedRerouteDeclaration::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty &&
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UMaterialExpressionNamedRerouteDeclaration, Name))
	{
		MakeNameUnique();
	}
}

int32 UMaterialExpressionNamedRerouteDeclaration::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Just forward to the input
	return Input.Compile(Compiler);
}

void UMaterialExpressionNamedRerouteDeclaration::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(Name.ToString());
}


FText UMaterialExpressionNamedRerouteDeclaration::GetCreationDescription() const 
{
	return LOCTEXT("NamedRerouteDeclCreationDesc", "Captures the value of an input, may be used at multiple other points in the graph without requiring connecting wires, allows tiding up of complex graphs");
}

FText UMaterialExpressionNamedRerouteDeclaration::GetCreationName() const
{
	return LOCTEXT("NamedRerouteDeclCreationName", "Add Named Reroute Declaration Node...");
}

bool UMaterialExpressionNamedRerouteDeclaration::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	if (Name.ToString().Contains(SearchQuery))
	{
		return true;
	}

	return Super::MatchesSearchQuery(SearchQuery);
}

bool UMaterialExpressionNamedRerouteDeclaration::CanRenameNode() const
{
	return true;
}

FString UMaterialExpressionNamedRerouteDeclaration::GetEditableName() const
{
	return Name.ToString();
}

void UMaterialExpressionNamedRerouteDeclaration::SetEditableName(const FString& NewName)
{
	Name = *NewName;
	MakeNameUnique();

	// Refresh usage names
	if (Material || Function)
	{
		auto& Expressions = Material ? Material->Expressions : Function->FunctionExpressions;
		for (UMaterialExpression* Expression : Expressions)
		{
			auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
			if (Usage && Usage->Declaration == this && Usage->GraphNode)
			{
				Usage->GraphNode->ReconstructNode();
			}
		}
	}
}

void UMaterialExpressionNamedRerouteDeclaration::PostCopyNode(const TArray<UMaterialExpression*>& CopiedExpressions)
{
	Super::PostCopyNode(CopiedExpressions);

	// Only force regeneration of Guid if there's already a variable with the same one
	if (FindDeclarationInMaterial(VariableGuid))
	{
		// Update Guid, and update the copied usages accordingly
		FGuid OldGuid = VariableGuid;
		UpdateVariableGuid(true, true);
		for (UMaterialExpression* Expression : CopiedExpressions)
		{
			auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression);
			if (Usage && Usage->DeclarationGuid == OldGuid)
			{
				Usage->Declaration = this;
				Usage->DeclarationGuid = VariableGuid;
			}
		}

		// Find a new name
		MakeNameUnique();
	}
	else
	{
		// If there's no existing variable with this GUID, only create it if needed
		UpdateVariableGuid(false, true);
	}
}
#endif // WITH_EDITOR

bool UMaterialExpressionNamedRerouteDeclaration::GetRerouteInput(FExpressionInput& OutInput) const
{
	OutInput = Input;
	return true;
}

void UMaterialExpressionNamedRerouteDeclaration::UpdateVariableGuid(bool bForceGeneration, bool bAllowMarkingPackageDirty)
{
	// If we are in the editor, and we don't have a valid GUID yet, generate one.
	if (GIsEditor && !FApp::IsGame())
	{
		if (bForceGeneration || !VariableGuid.IsValid())
		{
			VariableGuid = FGuid::NewGuid();

			if (bAllowMarkingPackageDirty)
			{
				MarkPackageDirty();
			}
		}
	}
}

void UMaterialExpressionNamedRerouteDeclaration::MakeNameUnique()
{
#if WITH_EDITORONLY_DATA
	if (Material || Function)
	{
		auto& Expressions = Material ? Material->Expressions : Function->FunctionExpressions;

		int32 NameIndex = 1;
		bool bResultNameIndexValid = true;
		FName PotentialName;

		// Find an available unique name
		do
		{
			PotentialName = Name;
			if (NameIndex != 1)
			{
				PotentialName.SetNumber(NameIndex);
			}

			bResultNameIndexValid = true;
			for (UMaterialExpression* Expression : Expressions)
			{
				auto* OtherDeclaration = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expression);
				if (OtherDeclaration && OtherDeclaration != this && OtherDeclaration->Name == PotentialName)
				{
					bResultNameIndexValid = false;
					break;
				}
			}

			NameIndex++;
		} while (!bResultNameIndexValid);

		Name = PotentialName;
	}
#endif // WITH_EDITORONLY_DATA
}


UMaterialExpressionNamedRerouteUsage::UMaterialExpressionNamedRerouteUsage(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
int32 UMaterialExpressionNamedRerouteUsage::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!IsDeclarationValid())
	{
		return Compiler->Errorf(TEXT("Invalid named reroute variable"));
	}
	return Declaration->Compile(Compiler, OutputIndex);
}

void UMaterialExpressionNamedRerouteUsage::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(IsDeclarationValid() ? Declaration->Name.ToString() : TEXT("Invalid named reroute"));
}

uint32 UMaterialExpressionNamedRerouteUsage::GetOutputType(int32 OutputIndex)
{
	if (IsDeclarationValid())
	{
		return Declaration->GetInputType(OutputIndex);
	}
	else
	{
		return Super::GetOutputType(OutputIndex);
	}
}

bool UMaterialExpressionNamedRerouteUsage::MatchesSearchQuery(const TCHAR* SearchQuery)
{
	if (IsDeclarationValid())
	{
		return Declaration->MatchesSearchQuery(SearchQuery);
	}
	return Super::MatchesSearchQuery(SearchQuery);
}

void UMaterialExpressionNamedRerouteUsage::PostCopyNode(const TArray<UMaterialExpression*>& CopiedExpressions)
{
	Super::PostCopyNode(CopiedExpressions);

	ensure(!Declaration || Declaration->VariableGuid == DeclarationGuid);

	if (!Declaration)
	{
		// First try to find the declaration in the copied expressions
		Declaration = FindDeclarationInArray(DeclarationGuid, CopiedExpressions);
		if (!Declaration)
		{
			// If unsuccessful, try to find it in the whole material
			Declaration = FindDeclarationInMaterial(DeclarationGuid);
		}
		if (Declaration)
		{
			// Save that Declaration change
			MarkPackageDirty();
		}
	}
}
#endif // WITH_EDITOR

bool UMaterialExpressionNamedRerouteUsage::IsDeclarationValid() const
{
	// Deleted expressions are marked as pending kill (see FMaterialEditor::DeleteNodes)
	return Declaration && !Declaration->IsPendingKill();
}

bool UMaterialExpressionNamedRerouteUsage::GetRerouteInput(FExpressionInput& OutInput) const
{
	if (IsDeclarationValid())
	{
		// Forward to the declaration input
		OutInput = Declaration->Input;
		return true;
	}
	else
	{
		return false;
	}
}

UMaterialExpressionRotateAboutAxis::UMaterialExpressionRotateAboutAxis(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Period = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionRotateAboutAxis::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!NormalizedRotationAxis.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RotateAboutAxis input NormalizedRotationAxis"));
	}
	else if (!RotationAngle.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RotateAboutAxis input RotationAngle"));
	}
	else if (!PivotPoint.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RotateAboutAxis input PivotPoint"));
	}
	else if (!Position.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing RotateAboutAxis input Position"));
	}
	else
	{
		const int32 AngleIndex = Compiler->Mul(RotationAngle.Compile(Compiler), Compiler->Constant(2.0f * (float)PI / Period));
		const int32 RotationIndex = Compiler->AppendVector(
			Compiler->ForceCast(NormalizedRotationAxis.Compile(Compiler), MCT_Float3), 
			Compiler->ForceCast(AngleIndex, MCT_Float1));

		return Compiler->RotateAboutAxis(
			RotationIndex, 
			PivotPoint.Compile(Compiler), 
			Position.Compile(Compiler));
	}
}

void UMaterialExpressionRotateAboutAxis::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("RotateAboutAxis"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// Static functions so it can be used other material expressions.
///////////////////////////////////////////////////////////////////////////////

/** Does not use length() to allow optimizations. */
static int32 CompileHelperLength( FMaterialCompiler* Compiler, int32 A, int32 B )
{
	int32 Delta = Compiler->Sub(A, B);

	if(Compiler->GetType(A) == MCT_Float && Compiler->GetType(B) == MCT_Float)
	{
		// optimized
		return Compiler->Abs(Delta);
	}

	int32 Dist2 = Compiler->Dot(Delta, Delta);
	return Compiler->SquareRoot(Dist2);
}

/** Used FMath::Clamp(), which will be optimized away later to a saturate(). */
static int32 CompileHelperSaturate( FMaterialCompiler* Compiler, int32 A )
{
	return Compiler->Clamp(A, Compiler->Constant(0.0f), Compiler->Constant(1.0f));
}

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSphereMask
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSphereMask::UMaterialExpressionSphereMask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	AttenuationRadius = 256.0f;
	HardnessPercent = 100.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSphereMask::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input A"));
	}
	else if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input B"));
	}
	else
	{
		int32 Arg1 = A.Compile(Compiler);
		int32 Arg2 = B.Compile(Compiler);
		int32 Distance = CompileHelperLength(Compiler, Arg1, Arg2);

		int32 ArgInvRadius;
		if(Radius.GetTracedInput().Expression)
		{
			// if the radius input is hooked up, use it
			ArgInvRadius = Compiler->Div(Compiler->Constant(1.0f), Compiler->Max(Compiler->Constant(0.00001f), Radius.Compile(Compiler)));
		}
		else
		{
			// otherwise use the internal constant
			ArgInvRadius = Compiler->Constant(1.0f / FMath::Max(0.00001f, AttenuationRadius));
		}

		int32 NormalizeDistance = Compiler->Mul(Distance, ArgInvRadius);

		int32 ArgInvHardness;
		if(Hardness.GetTracedInput().Expression)
		{
			int32 Softness = Compiler->Sub(Compiler->Constant(1.0f), Hardness.Compile(Compiler));

			// if the radius input is hooked up, use it
			ArgInvHardness = Compiler->Div(Compiler->Constant(1.0f), Compiler->Max(Softness, Compiler->Constant(0.00001f)));
		}
		else
		{
			// Hardness is in percent 0%:soft .. 100%:hard
			// Max to avoid div by 0
			float InvHardness = 1.0f / FMath::Max(1.0f - HardnessPercent * 0.01f, 0.00001f);

			// otherwise use the internal constant
			ArgInvHardness = Compiler->Constant(InvHardness);
		}

		int32 NegNormalizedDistance = Compiler->Sub(Compiler->Constant(1.0f), NormalizeDistance);
		int32 MaskUnclamped = Compiler->Mul(NegNormalizedDistance, ArgInvHardness);

		return CompileHelperSaturate(Compiler, MaskUnclamped);
	}
}

void UMaterialExpressionSphereMask::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SphereMask"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSobol
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSobol::UMaterialExpressionSobol(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstIndex = 0;
	ConstSeed = FVector2D(0.f, 0.f);
}

#if WITH_EDITOR
int32 UMaterialExpressionSobol::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 CellInput = Cell.GetTracedInput().Expression ? Cell.Compile(Compiler) : Compiler->Constant2(0.f, 0.f);
	int32 IndexInput = Index.GetTracedInput().Expression ? Index.Compile(Compiler) : Compiler->Constant(ConstIndex);
	int32 SeedInput = Seed.GetTracedInput().Expression ? Seed.Compile(Compiler) : Compiler->Constant2(ConstSeed.X, ConstSeed.Y);
	return Compiler->Sobol(CellInput, IndexInput, SeedInput);
}

void UMaterialExpressionSobol::GetCaption(TArray<FString>& OutCaptions) const
{
	FString Caption = TEXT("Sobol");

	if (!Index.GetTracedInput().Expression)
	{
		Caption += FString::Printf(TEXT(" (%d)"), ConstIndex);;
	}

	OutCaptions.Add(Caption);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionTemporalSobol
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionTemporalSobol::UMaterialExpressionTemporalSobol(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstIndex = 0;
	ConstSeed = FVector2D(0.f, 0.f);
}

#if WITH_EDITOR
int32 UMaterialExpressionTemporalSobol::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 IndexInput = Index.GetTracedInput().Expression ? Index.Compile(Compiler) : Compiler->Constant(ConstIndex);
	int32 SeedInput = Seed.GetTracedInput().Expression ? Seed.Compile(Compiler) : Compiler->Constant2(ConstSeed.X, ConstSeed.Y);
	return Compiler->TemporalSobol(IndexInput, SeedInput);
}

void UMaterialExpressionTemporalSobol::GetCaption(TArray<FString>& OutCaptions) const
{
	FString Caption = TEXT("Temporal Sobol");

	if (!Index.GetTracedInput().Expression)
	{
		Caption += FString::Printf(TEXT(" (%d)"), ConstIndex);;
	}

	OutCaptions.Add(Caption);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionNoise
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionNoise::UMaterialExpressionNoise(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Scale = 1.0f;
	Levels = 6;
	Quality = 1;
	OutputMin = -1.0f;
	OutputMax = 1.0f;
	LevelScale = 2.0f;
	NoiseFunction = NOISEFUNCTION_SimplexTex;
	bTurbulence = true;
	bTiling = false;
	RepeatSize = 512;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif

}

#if WITH_EDITOR
bool UMaterialExpressionNoise::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	if (bIsEditable && InProperty != nullptr)
	{
		FName PropertyFName = InProperty->GetFName();

		bool bTilableNoiseType = NoiseFunction == NOISEFUNCTION_GradientALU || NoiseFunction == NOISEFUNCTION_ValueALU 
			|| NoiseFunction == NOISEFUNCTION_GradientTex || NoiseFunction == NOISEFUNCTION_VoronoiALU;

		bool bSupportsQuality = (NoiseFunction == NOISEFUNCTION_VoronoiALU);

		if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, bTiling))
		{
			bIsEditable = bTilableNoiseType;
		}
		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, RepeatSize))
		{
			bIsEditable = bTilableNoiseType && bTiling;
		}

		if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionNoise, Quality))
		{
			bIsEditable = bSupportsQuality;
		}
	}

	return bIsEditable;
}

int32 UMaterialExpressionNoise::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionInput;

	if(Position.GetTracedInput().Expression)
	{
		PositionInput = Position.Compile(Compiler);
	}
	else
	{
		PositionInput = Compiler->WorldPosition(WPT_Default);
	}

	int32 FilterWidthInput;

	if(FilterWidth.GetTracedInput().Expression)
	{
		FilterWidthInput = FilterWidth.Compile(Compiler);
	}
	else
	{
		FilterWidthInput = Compiler->Constant(0);
	}

	return Compiler->Noise(PositionInput, Scale, Quality, NoiseFunction, bTurbulence, Levels, OutputMin, OutputMax, LevelScale, FilterWidthInput, bTiling, RepeatSize);
}

void UMaterialExpressionNoise::GetCaption(TArray<FString>& OutCaptions) const
{
	const UEnum* NFEnum = StaticEnum<ENoiseFunction>();
	check(NFEnum);
	OutCaptions.Add(NFEnum->GetDisplayNameTextByValue(NoiseFunction).ToString());
	OutCaptions.Add(TEXT("Noise"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionVectorNoise
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionVectorNoise::UMaterialExpressionVectorNoise(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Quality = 1;
	NoiseFunction = VNF_CellnoiseALU;
	bTiling = false;
	TileSize = 300;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
bool UMaterialExpressionVectorNoise::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	if (bIsEditable && InProperty != nullptr)
	{
		FName PropertyFName = InProperty->GetFName();

		bool bSupportsQuality = (NoiseFunction == VNF_VoronoiALU);

		if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, TileSize))
		{
			bIsEditable = bTiling;
		}

		else if (PropertyFName == GET_MEMBER_NAME_CHECKED(UMaterialExpressionVectorNoise, Quality))
		{
			bIsEditable = bSupportsQuality;
		}
	}

	return bIsEditable;
}

int32 UMaterialExpressionVectorNoise::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionInput;

	if (Position.GetTracedInput().Expression)
	{
		PositionInput = Position.Compile(Compiler);
	}
	else
	{
		PositionInput = Compiler->WorldPosition(WPT_Default);
	}

	return Compiler->VectorNoise(PositionInput, Quality, NoiseFunction, bTiling, TileSize);
}

void UMaterialExpressionVectorNoise::GetCaption(TArray<FString>& OutCaptions) const
{
	const UEnum* VNFEnum = StaticEnum<EVectorNoiseFunction>();
	check(VNFEnum);
	OutCaptions.Add(VNFEnum->GetDisplayNameTextByValue(NoiseFunction).ToString());
	OutCaptions.Add(TEXT("Vector Noise"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionBlackBody
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionBlackBody::UMaterialExpressionBlackBody(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionBlackBody::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 TempInput = INDEX_NONE;

	if( Temp.GetTracedInput().Expression )
	{
		TempInput = Temp.Compile(Compiler);
	}

	if( TempInput == INDEX_NONE )
	{
		return INDEX_NONE;
	}

	return Compiler->BlackBody( TempInput );
}

void UMaterialExpressionBlackBody::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("BlackBody"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDistanceToNearestSurface
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDistanceToNearestSurface::UMaterialExpressionDistanceToNearestSurface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDistanceToNearestSurface::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionArg = INDEX_NONE;

	if (Position.GetTracedInput().Expression)
	{
		PositionArg = Position.Compile(Compiler);
	}
	else 
	{
		PositionArg = Compiler->WorldPosition(WPT_Default);
	}

	return Compiler->DistanceToNearestSurface(PositionArg);
}

void UMaterialExpressionDistanceToNearestSurface::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("DistanceToNearestSurface"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDistanceFieldGradient
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDistanceFieldGradient::UMaterialExpressionDistanceFieldGradient(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDistanceFieldGradient::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionArg = INDEX_NONE;

	if (Position.GetTracedInput().Expression)
	{
		PositionArg = Position.Compile(Compiler);
	}
	else 
	{
		PositionArg = Compiler->WorldPosition(WPT_Default);
	}

	return Compiler->DistanceFieldGradient(PositionArg);
}

void UMaterialExpressionDistanceFieldGradient::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("DistanceFieldGradient"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSamplePhysicsVectorField
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionSamplePhysicsVectorField::UMaterialExpressionSamplePhysicsVectorField(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	FieldTarget = EFieldVectorType::Vector_LinearForce;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSamplePhysicsVectorField::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionArg = INDEX_NONE;

	if (WorldPosition.GetTracedInput().Expression)
	{
		PositionArg = WorldPosition.Compile(Compiler);
	}
	else
	{
		PositionArg = Compiler->WorldPosition(WPT_Default);
	}

	return Compiler->SamplePhysicsField(PositionArg, EFieldOutputType::Field_Output_Vector, static_cast<uint8>(FieldTarget));
}

void UMaterialExpressionSamplePhysicsVectorField::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SamplePhysicsVectorField"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSamplePhysicsScalarField
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionSamplePhysicsScalarField::UMaterialExpressionSamplePhysicsScalarField(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	FieldTarget = EFieldScalarType::Scalar_DynamicConstraint;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSamplePhysicsScalarField::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionArg = INDEX_NONE;

	if (WorldPosition.GetTracedInput().Expression)
	{
		PositionArg = WorldPosition.Compile(Compiler);
	}
	else
	{
		PositionArg = Compiler->WorldPosition(WPT_Default);
	}

	return Compiler->SamplePhysicsField(PositionArg, EFieldOutputType::Field_Output_Scalar, static_cast<uint8>(FieldTarget));
}

void UMaterialExpressionSamplePhysicsScalarField::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SamplePhysicsScalarField"));
}
#endif // WITH_EDITOR


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSamplePhysicsIntegerField
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionSamplePhysicsIntegerField::UMaterialExpressionSamplePhysicsIntegerField(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	FieldTarget = EFieldIntegerType::Integer_DynamicState;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSamplePhysicsIntegerField::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 PositionArg = INDEX_NONE;

	if (WorldPosition.GetTracedInput().Expression)
	{
		PositionArg = WorldPosition.Compile(Compiler);
	}
	else
	{
		PositionArg = Compiler->WorldPosition(WPT_Default);
	}

	return Compiler->SamplePhysicsField(PositionArg, EFieldOutputType::Field_Output_Integer, static_cast<uint8>(FieldTarget));
}

void UMaterialExpressionSamplePhysicsIntegerField::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SamplePhysicsIntegerField"));
}
#endif // WITH_EDITOR



///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDistance
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDistance::UMaterialExpressionDistance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDistance::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input A"));
	}
	else if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input B"));
	}
	else
	{
		int32 Arg1 = A.Compile(Compiler);
		int32 Arg2 = B.Compile(Compiler);
		return CompileHelperLength(Compiler, Arg1, Arg2);
	}
}

void UMaterialExpressionDistance::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Distance"));
}
#endif // WITH_EDITOR

UMaterialExpressionTwoSidedSign::UMaterialExpressionTwoSidedSign(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTwoSidedSign::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->TwoSidedSign();
}

void UMaterialExpressionTwoSidedSign::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("TwoSidedSign"));
}
#endif // WITH_EDITOR

UMaterialExpressionVertexNormalWS::UMaterialExpressionVertexNormalWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVertexNormalWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->VertexNormal();
}

void UMaterialExpressionVertexNormalWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("VertexNormalWS"));
}
#endif // WITH_EDITOR

UMaterialExpressionVertexTangentWS::UMaterialExpressionVertexTangentWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT("Vectors", "Vectors"))
			, NAME_Coordinates(LOCTEXT("Coordinates", "Coordinates"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVertexTangentWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->VertexTangent();
}

void UMaterialExpressionVertexTangentWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("VertexTangentWS"));
}
#endif // WITH_EDITOR

UMaterialExpressionPixelNormalWS::UMaterialExpressionPixelNormalWS(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT( "Vectors", "Vectors" ))
			, NAME_Coordinates(LOCTEXT( "Coordinates", "Coordinates" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);
	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPixelNormalWS::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->PixelNormalWS();
}

void UMaterialExpressionPixelNormalWS::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("PixelNormalWS"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPerInstanceRandom
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPerInstanceRandom::UMaterialExpressionPerInstanceRandom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPerInstanceRandom::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->PerInstanceRandom();
}

void UMaterialExpressionPerInstanceRandom::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("PerInstanceRandom"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPerInstanceFadeAmount
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPerInstanceFadeAmount::UMaterialExpressionPerInstanceFadeAmount(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPerInstanceFadeAmount::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->PerInstanceFadeAmount();
}

void UMaterialExpressionPerInstanceFadeAmount::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("PerInstanceFadeAmount"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPerInstanceCustomData
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPerInstanceCustomData::UMaterialExpressionPerInstanceCustomData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Custom;
		FConstructorStatics()
			: NAME_Custom(LOCTEXT("Custom", "Custom"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Custom);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPerInstanceCustomData::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 DefaultArgument = DefaultValue.GetTracedInput().Expression ? DefaultValue.Compile(Compiler) : Compiler->Constant(ConstDefaultValue);
	return Compiler->PerInstanceCustomData(DataIndex, DefaultArgument);
}

void UMaterialExpressionPerInstanceCustomData::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("PerInstanceCustomData[%d]"), DataIndex));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionAntialiasedTextureMask
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionAntialiasedTextureMask::UMaterialExpressionAntialiasedTextureMask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTexture2D> DefaultTexture;
#if WITH_EDITORONLY_DATA
		FText NAME_Utility;
#endif
		FName NAME_None;
		FConstructorStatics()
			: DefaultTexture(TEXT("/Engine/EngineResources/DefaultTexture"))
#if WITH_EDITORONLY_DATA
			, NAME_Utility(LOCTEXT( "Utility", "Utility" ))
#endif
			, NAME_None(TEXT("None"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	Texture = ConstructorStatics.DefaultTexture.Object;

#if WITH_EDITORONLY_DATA
	MenuCategories.Empty();
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif

	Threshold = 0.5f;
	ParameterName = ConstructorStatics.NAME_None;
	Channel = TCC_Alpha;

#if WITH_EDITORONLY_DATA
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionAntialiasedTextureMask::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Texture)
	{
		return Compiler->Errorf(TEXT("UMaterialExpressionAntialiasedTextureMask> Missing input texture"));
	}

	if (Texture->GetMaterialType() == MCT_TextureVirtual)
	{
		return Compiler->Errorf(TEXT("UMaterialExpressionAntialiasedTextureMask> Virtual textures are not supported"));
	}

	int32 ArgCoord = Coordinates.Expression ? Coordinates.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false);

	FString ErrorMessage;
	if (!TextureIsValid(Texture, ErrorMessage))
	{
		return CompilerError(Compiler, *ErrorMessage);
	}

	int32 TextureCodeIndex;

	if (!ParameterName.IsValid() || ParameterName.IsNone())
	{
		TextureCodeIndex = Compiler->Texture(Texture, SamplerType);
	}
	else
	{
		TextureCodeIndex = Compiler->TextureParameter(ParameterName, Texture, SamplerType);
	}

	if (!VerifySamplerType(Compiler, (Desc.Len() > 0 ? *Desc : TEXT("AntialiasedTextureMask")), Texture, SamplerType))
	{
		return INDEX_NONE;
	}

	return Compiler->AntialiasedTextureMask(TextureCodeIndex,ArgCoord,Threshold,Channel);
}

void UMaterialExpressionAntialiasedTextureMask::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("AAMasked Param2D")); 
	OutCaptions.Add(FString::Printf(TEXT("'%s'"), *ParameterName.ToString()));
}

bool UMaterialExpressionAntialiasedTextureMask::TextureIsValid(UTexture* InTexture, FString& OutMessage)
{
	if (!InTexture)
	{
		OutMessage = TEXT("Found NULL, requires Texture2D");
		return false;
	}
	// Doesn't allow virtual/external textures here
	else if (!(InTexture->GetMaterialType() & MCT_Texture2D))
	{
		OutMessage = FString::Printf(TEXT("Found %s, requires Texture2D"), *InTexture->GetClass()->GetName());
		return false;
	}
	
	return true;
}

void UMaterialExpressionAntialiasedTextureMask::SetDefaultTexture()
{
	Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"), nullptr, LOAD_None, nullptr);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionDecalDerivative
//
UMaterialExpressionDecalDerivative::UMaterialExpressionDecalDerivative(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT("Utils", "Utils"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	//bCollapsed = true;
	bShaderInputData = true;
	bShowOutputNameOnPin = true;
	
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("DDX")));
	Outputs.Add(FExpressionOutput(TEXT("DDY")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDecalDerivative::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->TextureDecalDerivative(OutputIndex == 1);
}

void UMaterialExpressionDecalDerivative::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Decal Derivative"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionDecalLifetimeOpacity
//
UMaterialExpressionDecalLifetimeOpacity::UMaterialExpressionDecalLifetimeOpacity(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT("Utils", "Utils"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bShaderInputData = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Opacity")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDecalLifetimeOpacity::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material && Material->MaterialDomain != MD_DeferredDecal)
	{
		return CompilerError(Compiler, TEXT("Node only works for the deferred decal material domain."));
	}

	return Compiler->DecalLifetimeOpacity();
}

void UMaterialExpressionDecalLifetimeOpacity::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Decal Lifetime Opacity"));
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionDecalMipmapLevel
//
UMaterialExpressionDecalMipmapLevel::UMaterialExpressionDecalMipmapLevel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ConstWidth(256.0f)
	, ConstHeight(ConstWidth)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Vectors;
		FConstructorStatics()
			: NAME_Vectors(LOCTEXT("Utils", "Utils"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Vectors);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDecalMipmapLevel::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Material && Material->MaterialDomain != MD_DeferredDecal)
	{
		return CompilerError(Compiler, TEXT("Node only works for the deferred decal material domain."));
	}

	int32 TextureSizeInput = INDEX_NONE;

	if (TextureSize.GetTracedInput().Expression)
	{
		TextureSizeInput = TextureSize.Compile(Compiler);
	}
	else
	{
		TextureSizeInput = Compiler->Constant2(ConstWidth, ConstHeight);
	}

	if (TextureSizeInput == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return Compiler->TextureDecalMipmapLevel(TextureSizeInput);
}

void UMaterialExpressionDecalMipmapLevel::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Decal Mipmap Level"));
}
#endif // WITH_EDITOR

UMaterialExpressionDepthFade::UMaterialExpressionDepthFade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Depth;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Depth(LOCTEXT( "Depth", "Depth" ))
			, NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#endif

	FadeDistanceDefault = 100.0f;
	OpacityDefault = 1.0f;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Depth);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDepthFade::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Scales Opacity by a Linear fade based on SceneDepth, from 0 at PixelDepth to 1 at FadeDistance
	// Result = Opacity * saturate((SceneDepth - PixelDepth) / max(FadeDistance, DELTA))
	const int32 OpacityIndex = InOpacity.GetTracedInput().Expression ? InOpacity.Compile(Compiler) : Compiler->Constant(OpacityDefault);
	const int32 FadeDistanceIndex = Compiler->Max(FadeDistance.GetTracedInput().Expression ? FadeDistance.Compile(Compiler) : Compiler->Constant(FadeDistanceDefault), Compiler->Constant(DELTA));

	int32 PixelDepthIndex = -1; 
	// On mobile scene depth is limited to 65500 
	// to avoid false fading on objects that are close or exceed this limit we clamp pixel depth to (65500 - FadeDistance)
	if (Compiler->GetFeatureLevel() <= ERHIFeatureLevel::ES3_1)
	{
		PixelDepthIndex = Compiler->Min(Compiler->PixelDepth(), Compiler->Sub(Compiler->Constant(65500.f), FadeDistanceIndex));
	}
	else
	{
		PixelDepthIndex = Compiler->PixelDepth();
	}
	
	const int32 FadeIndex = CompileHelperSaturate(Compiler, Compiler->Div(Compiler->Sub(Compiler->SceneDepth(INDEX_NONE, INDEX_NONE, false), PixelDepthIndex), FadeDistanceIndex));
	
	return Compiler->Mul(OpacityIndex, FadeIndex);
}
#endif // WITH_EDITOR

//
//	UMaterialExpressionSphericalParticleOpacity
//
UMaterialExpressionSphericalParticleOpacity::UMaterialExpressionSphericalParticleOpacity(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
#endif

	ConstantDensity = 1;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Particles);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSphericalParticleOpacity::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	const int32 DensityIndex = Density.GetTracedInput().Expression ? Density.Compile(Compiler) : Compiler->Constant(ConstantDensity);
	return Compiler->SphericalParticleOpacity(DensityIndex);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDepthOfFieldFunction
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDepthOfFieldFunction::UMaterialExpressionDepthOfFieldFunction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDepthOfFieldFunction::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 DepthInput;

	if(Depth.GetTracedInput().Expression)
	{
		// using the input allows more custom behavior
		DepthInput = Depth.Compile(Compiler);
	}
	else
	{
		// no input means we use the PixelDepth
		DepthInput = Compiler->PixelDepth();
	}

	if(DepthInput == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return Compiler->DepthOfFieldFunction(DepthInput, FunctionValue);
}


void UMaterialExpressionDepthOfFieldFunction::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("DepthOfFieldFunction")));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDDX
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDDX::UMaterialExpressionDDX(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDDX::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 ValueInput = INDEX_NONE;

	if(Value.GetTracedInput().Expression)
	{
		ValueInput = Value.Compile(Compiler);
	}

	if(ValueInput == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return Compiler->DDX(ValueInput);
}


void UMaterialExpressionDDX::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("DDX")));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionDDY
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionDDY::UMaterialExpressionDDY(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	bCollapsed = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionDDY::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 ValueInput = INDEX_NONE;

	if(Value.GetTracedInput().Expression)
	{
		ValueInput = Value.Compile(Compiler);
	}

	if(ValueInput == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	return Compiler->DDY(ValueInput);
}


void UMaterialExpressionDDY::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("DDY")));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Particle relative time material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionParticleRelativeTime::UMaterialExpressionParticleRelativeTime(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleRelativeTime::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleRelativeTime();
}

void UMaterialExpressionParticleRelativeTime::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Relative Time"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Particle motion blur fade material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionParticleMotionBlurFade::UMaterialExpressionParticleMotionBlurFade(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleMotionBlurFade::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleMotionBlurFade();
}

void UMaterialExpressionParticleMotionBlurFade::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Motion Blur Fade"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Particle motion blur fade material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionParticleRandom::UMaterialExpressionParticleRandom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleRandom::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleRandom();
}

void UMaterialExpressionParticleRandom::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Random Value"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Particle direction material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionParticleDirection::UMaterialExpressionParticleDirection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleDirection::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleDirection();
}

void UMaterialExpressionParticleDirection::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Direction"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Particle speed material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionParticleSpeed::UMaterialExpressionParticleSpeed(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleSpeed::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleSpeed();
}

void UMaterialExpressionParticleSpeed::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Speed"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Particle size material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionParticleSize::UMaterialExpressionParticleSize(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Particles;
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Particles(LOCTEXT( "Particles", "Particles" ))
			, NAME_Constants(LOCTEXT( "Constants", "Constants" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Particles);
	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionParticleSize::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->ParticleSize();
}

void UMaterialExpressionParticleSize::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Particle Size"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	Atmospheric fog material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionAtmosphericFogColor::UMaterialExpressionAtmosphericFogColor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Atmosphere;
		FConstructorStatics()
			: NAME_Atmosphere(LOCTEXT( "Atmosphere", "Atmosphere" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Atmosphere);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionAtmosphericFogColor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 WorldPositionInput = INDEX_NONE;

	if( WorldPosition.GetTracedInput().Expression )
	{
		WorldPositionInput = WorldPosition.Compile(Compiler);
	}

	return Compiler->AtmosphericFogColor( WorldPositionInput );
}

void UMaterialExpressionAtmosphericFogColor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Atmospheric Fog Color"));
}
#endif // WITH_EDITOR

/*------------------------------------------------------------------------------
	SpeedTree material expression.
------------------------------------------------------------------------------*/
UMaterialExpressionSpeedTree::UMaterialExpressionSpeedTree(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_SpeedTree;
		FConstructorStatics()
			: NAME_SpeedTree(LOCTEXT( "SpeedTree", "SpeedTree" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	GeometryType = STG_Branch;
	WindType = STW_None;
	LODType = STLOD_Pop;
	BillboardThreshold = 0.9f;
	bAccurateWindVelocities = false;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_SpeedTree);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSpeedTree::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 GeometryArg = (GeometryInput.GetTracedInput().Expression ? GeometryInput.Compile(Compiler) : Compiler->Constant(GeometryType));
	int32 WindArg = (WindInput.GetTracedInput().Expression ? WindInput.Compile(Compiler) : Compiler->Constant(WindType));
	int32 LODArg = (LODInput.GetTracedInput().Expression ? LODInput.Compile(Compiler) : Compiler->Constant(LODType));
	
	bool bExtraBend = (ExtraBendWS.GetTracedInput().Expression != nullptr);
	int32 ExtraBendArg = (ExtraBendWS.GetTracedInput().Expression ? ExtraBendWS.Compile(Compiler) : Compiler->Constant3(0.0f, 0.0f, 0.0f));
	 
	return Compiler->SpeedTree(GeometryArg, WindArg, LODArg, BillboardThreshold, bAccurateWindVelocities, bExtraBend, ExtraBendArg);
}

void UMaterialExpressionSpeedTree::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SpeedTree"));
}
#endif // WITH_EDITOR

void UMaterialExpressionSpeedTree::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	if (Record.GetUnderlyingArchive().UE4Ver() < VER_UE4_SPEEDTREE_WIND_V7)
	{
		// update wind presets for speedtree v7
		switch (WindType)
		{
		case STW_Fastest:
			WindType = STW_Better;
			break;
		case STW_Fast:
			WindType = STW_Palm;
			break;
		case STW_Better:
			WindType = STW_Best;
			break;
		default:
			break;
		}
	}
}

#if WITH_EDITOR

bool UMaterialExpressionSpeedTree::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);

	if (GeometryType == STG_Billboard)
	{
		if (InProperty->GetFName() == TEXT("LODType"))
		{
			bIsEditable = false;
		}
	}
	else
	{
		if (InProperty->GetFName() == TEXT("BillboardThreshold"))
		{
			bIsEditable = false;
		}
	}

	return bIsEditable;
}

#endif

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionCustomOutput
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionCustomOutput::UMaterialExpressionCustomOutput(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionEyeAdaptation
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionEyeAdaptation::UMaterialExpressionEyeAdaptation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT( "Utility", "Utility" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("EyeAdaptation")));
	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionEyeAdaptation::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{    
	return Compiler->EyeAdaptation();
}

void UMaterialExpressionEyeAdaptation::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("EyeAdaptation")));
}
#endif // WITH_EDITOR

//
// UMaterialExpressionTangentOutput
//
UMaterialExpressionTangentOutput::UMaterialExpressionTangentOutput(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Custom;
		FConstructorStatics(const FString& DisplayName, const FString& FunctionName)
			: NAME_Custom(LOCTEXT( "Custom", "Custom" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics(GetDisplayName(), GetFunctionName());

	MenuCategories.Add(ConstructorStatics.NAME_Custom);

	// No outputs
	Outputs.Reset();
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionTangentOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if( Input.GetTracedInput().Expression )
	{
		return Compiler->CustomOutput(this, OutputIndex, Input.Compile(Compiler));
	}
	else
	{
		return CompilerError(Compiler, TEXT("Input missing"));
	}

	return INDEX_NONE;
}

void UMaterialExpressionTangentOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Tangent output"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// Clear Coat Custom Normal Input
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionClearCoatNormalCustomOutput::UMaterialExpressionClearCoatNormalCustomOutput(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics(const FString& DisplayName, const FString& FunctionName)
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics(GetDisplayName(), GetFunctionName());

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	bCollapsed = true;

	// No outputs
	Outputs.Reset();
#endif
}

#if WITH_EDITOR
int32  UMaterialExpressionClearCoatNormalCustomOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Input.GetTracedInput().Expression)
	{
		return Compiler->CustomOutput(this, OutputIndex, Input.Compile(Compiler));
	}
	else
	{
		return CompilerError(Compiler, TEXT("Input missing"));
	}
	return INDEX_NONE;
}


void UMaterialExpressionClearCoatNormalCustomOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("ClearCoatBottomNormal")));
}

FExpressionInput* UMaterialExpressionClearCoatNormalCustomOutput::GetInput(int32 InputIndex)
{
	return &Input;
}
#endif // WITH_EDITOR


///////////////////////////////////////////////////////////////////////////////
// Bent Normal Output
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionBentNormalCustomOutput::UMaterialExpressionBentNormalCustomOutput(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics(const FString& DisplayName, const FString& FunctionName)
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics(GetDisplayName(), GetFunctionName());

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	bCollapsed = true;

	// No outputs
	Outputs.Reset();
#endif
}

#if WITH_EDITOR
int32  UMaterialExpressionBentNormalCustomOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Input.GetTracedInput().Expression)
	{
		return Compiler->CustomOutput(this, OutputIndex, Input.Compile(Compiler));
	}
	else
	{
		return CompilerError(Compiler, TEXT("Input missing"));
	}
	return INDEX_NONE;
}


void UMaterialExpressionBentNormalCustomOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("BentNormal")));
}

FExpressionInput* UMaterialExpressionBentNormalCustomOutput::GetInput(int32 InputIndex)
{
	return &Input;
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// Vertex to pixel interpolated data handler
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionVertexInterpolator::UMaterialExpressionVertexInterpolator(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Utility);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("PS"), 0, 0, 0, 0, 0));
	bShowOutputNameOnPin = true;
#endif

	InterpolatorIndex = INDEX_NONE;
	InterpolatedType = MCT_Unknown;
	InterpolatorOffset = INDEX_NONE;
}

#if WITH_EDITOR
int32 UMaterialExpressionVertexInterpolator::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Input.GetTracedInput().Expression)
	{
		if (Compiler->IsVertexInterpolatorBypass())
		{
			// Certain types of compilers don't support vertex interpolators, just evaluate the input directly in that case
			return Input.Compile(Compiler);
		}
		else if (InterpolatorIndex == INDEX_NONE || CompileErrors.Num() > 0)
		{
			// Now this node is confirmed part of the graph, append all errors from the input compilation
			check(CompileErrors.Num() == CompileErrorExpressions.Num());
			for (int32 Error = 0; Error < CompileErrors.Num(); ++Error)
			{
				if (CompileErrorExpressions[Error])
				{
					Compiler->AppendExpressionError(CompileErrorExpressions[Error], *CompileErrors[Error]);
				}
				else
				{
					Compiler->Errorf(*CompileErrors[Error]);
				}
			}
			
			return Compiler->Errorf(TEXT("Failed to compile interpolator input."));
		}
		else
		{
			return Compiler->VertexInterpolator(InterpolatorIndex);
		}
	}
	else
	{
		return CompilerError(Compiler, TEXT("Input missing"));
	}
}

int32 UMaterialExpressionVertexInterpolator::CompileInput(class FMaterialCompiler* Compiler, int32 AssignedInterpolatorIndex)
{
	int32 Ret = INDEX_NONE;
	InterpolatorIndex = INDEX_NONE;
	InterpolatedType = MCT_Unknown;
	InterpolatorOffset = INDEX_NONE;

	ensure(!Compiler->IsVertexInterpolatorBypass());

	CompileErrors.Empty();
	CompileErrorExpressions.Empty();

	if (Input.GetTracedInput().Expression)
	{
		int32 InternalCode = Input.Compile(Compiler);
		Compiler->CustomOutput(this, AssignedInterpolatorIndex, InternalCode);
		InterpolatorIndex = AssignedInterpolatorIndex;
		InterpolatedType = Compiler->GetType(InternalCode);
		Ret = InternalCode;
	}

	return Ret;
}

void UMaterialExpressionVertexInterpolator::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("VertexInterpolator")));
}

FExpressionInput* UMaterialExpressionVertexInterpolator::GetInput(int32 InputIndex)
{
	return &Input;
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionrAtmosphericLightVector
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionAtmosphericLightVector::UMaterialExpressionAtmosphericLightVector(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionAtmosphericLightVector::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{

	return Compiler->AtmosphericLightVector();
}

void UMaterialExpressionAtmosphericLightVector::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("AtmosphericLightVector"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionrAtmosphericLightColor
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionAtmosphericLightColor ::UMaterialExpressionAtmosphericLightColor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionAtmosphericLightColor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{

	return Compiler->AtmosphericLightColor();
}

void UMaterialExpressionAtmosphericLightColor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("AtmosphericLightColor"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkyAtmosphereLightIlluminance
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSkyAtmosphereLightIlluminance::UMaterialExpressionSkyAtmosphereLightIlluminance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Sky;
		FConstructorStatics()
			: NAME_Sky(LOCTEXT("Sky", "Sky"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Sky);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSkyAtmosphereLightIlluminance::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 WorldPositionInput;
	if (WorldPosition.GetTracedInput().Expression)
	{
		WorldPositionInput = WorldPosition.Compile(Compiler);
	}
	else
	{
		WorldPositionInput = Compiler->WorldPosition(WPT_Default);
	}
	return Compiler->SkyAtmosphereLightIlluminance(WorldPositionInput, LightIndex);
}

void UMaterialExpressionSkyAtmosphereLightIlluminance::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("SkyAtmosphereLightIlluminance[%i]"), LightIndex));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkyAtmosphereLightDirection
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSkyAtmosphereLightDirection::UMaterialExpressionSkyAtmosphereLightDirection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Sky;
		FConstructorStatics()
			: NAME_Sky(LOCTEXT("Sky", "Sky"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Sky);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSkyAtmosphereLightDirection::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->SkyAtmosphereLightDirection(LightIndex);
}

void UMaterialExpressionSkyAtmosphereLightDirection::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("SkyAtmosphereLightDirection[%i]"), LightIndex));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkyAtmosphereLightDiskLuminance
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSkyAtmosphereLightDiskLuminance::UMaterialExpressionSkyAtmosphereLightDiskLuminance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Sky;
		FConstructorStatics()
			: NAME_Sky(LOCTEXT("Sky", "Sky"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Sky);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSkyAtmosphereLightDiskLuminance::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->SkyAtmosphereLightDiskLuminance(LightIndex);
}

void UMaterialExpressionSkyAtmosphereLightDiskLuminance::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString::Printf(TEXT("SkyAtmosphereLightDiskLuminance[%i]"), LightIndex));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkyAtmosphereViewLuminance
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSkyAtmosphereViewLuminance::UMaterialExpressionSkyAtmosphereViewLuminance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Sky;
		FConstructorStatics()
			: NAME_Sky(LOCTEXT("Sky", "Sky"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Sky);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSkyAtmosphereViewLuminance::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->SkyAtmosphereViewLuminance();
}

void UMaterialExpressionSkyAtmosphereViewLuminance::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SkyAtmosphereViewLuminance"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkyAtmosphereAerialPerpsective
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSkyAtmosphereAerialPerspective::UMaterialExpressionSkyAtmosphereAerialPerspective(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Sky;
		FConstructorStatics()
			: NAME_Sky(LOCTEXT("Sky", "Sky"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Sky);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSkyAtmosphereAerialPerspective::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 WorldPositionInput;
	if (WorldPosition.GetTracedInput().Expression)
	{
		WorldPositionInput = WorldPosition.Compile(Compiler);
	}
	else
	{
		WorldPositionInput = Compiler->WorldPosition(WPT_Default);
	}
	return Compiler->SkyAtmosphereAerialPerspective(WorldPositionInput);
}

void UMaterialExpressionSkyAtmosphereAerialPerspective::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SkyAtmosphereAerialPerspective"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkyAtmosphereAerialPerpsective
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSkyAtmosphereDistantLightScatteredLuminance::UMaterialExpressionSkyAtmosphereDistantLightScatteredLuminance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Sky;
		FConstructorStatics()
			: NAME_Sky(LOCTEXT("Sky", "Sky"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Sky);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionSkyAtmosphereDistantLightScatteredLuminance::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->SkyAtmosphereDistantLightScatteredLuminance();
}

void UMaterialExpressionSkyAtmosphereDistantLightScatteredLuminance::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("SkyAtmosphereDistantLightScatteredLuminance"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSkinningVertexOffsets
///////////////////////////////////////////////////////////////////////////////
PRAGMA_DISABLE_DEPRECATION_WARNINGS
UMaterialExpressionSkinningVertexOffsets::UMaterialExpressionSkinningVertexOffsets(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT("Vectors", "Vectors"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Pre Skin Offset"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Post Skin Offset"), 1, 1, 1, 1, 0));
	bShaderInputData = true;
	bShowOutputNameOnPin = true;
#endif
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
int32 UMaterialExpressionSkinningVertexOffsets::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Compiler->GetCurrentShaderFrequency() != SF_Vertex)
	{
		return Compiler->Errorf(
			TEXT("%s only available in the vertex shader, pass through custom interpolators if needed."), 
			(OutputIndex == 0) ? TEXT("Pre Skin Offset") : TEXT("Post Skin Offset")
			);
	}

	switch (OutputIndex)
	{
	default:
		return Compiler->Constant3(0, 0, 0);

	case 0:
		return Compiler->PreSkinVertexOffset();

	case 1:
		return Compiler->PostSkinVertexOffset();
	}
}

void UMaterialExpressionSkinningVertexOffsets::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Skin Vertex Offset"));
}

void UMaterialExpressionSkinningVertexOffsets::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Returns pre or post skinned offset applied to each vertex for a skeletal mesh, usable in vertex shader only."
		"Returns <0, 0, 0> for non-skeletal meshes or when disabled."), 40, OutToolTip);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPreSkinnedPosition
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPreSkinnedPosition::UMaterialExpressionPreSkinnedPosition(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPreSkinnedPosition::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Compiler->GetCurrentShaderFrequency() != SF_Vertex)
	{
		return Compiler->Errorf(TEXT("Pre-skinned position is only available in the vertex shader, pass through custom interpolators if needed."));
	}

	return Compiler->PreSkinnedPosition();
}

void UMaterialExpressionPreSkinnedPosition::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Pre-Skinned Local Position"));
}

void UMaterialExpressionPreSkinnedPosition::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns pre-skinned local position for skeletal meshes, usable in vertex shader only."
		"Returns the local position for non-skeletal meshes. Incompatible with GPU skin cache feature."), 40, OutToolTip);
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionPreSkinnedNormal
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionPreSkinnedNormal::UMaterialExpressionPreSkinnedNormal(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Constants;
		FConstructorStatics()
			: NAME_Constants(LOCTEXT( "Vectors", "Vectors" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Constants);

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	bShaderInputData = true;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionPreSkinnedNormal::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	return Compiler->PreSkinnedNormal();
}

void UMaterialExpressionPreSkinnedNormal::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Pre-Skinned Local Normal"));
}

void UMaterialExpressionPreSkinnedNormal::GetExpressionToolTip(TArray<FString>& OutToolTip) 
{
	ConvertToMultilineToolTip(TEXT("Returns pre-skinned local normal for skeletal meshes, usable in vertex shader only."
		"Returns the local normal for non-skeletal meshes. Incompatible with GPU skin cache feature."), 40, OutToolTip);
}
#endif // WITH_EDITOR

//
//  UMaterialExpressionCurveAtlasRowParameter
//
UMaterialExpressionCurveAtlasRowParameter::UMaterialExpressionCurveAtlasRowParameter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bCollapsed = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 0, 0, 0, 1));
#endif // WITH_EDITORONLY_DATA

}

UObject* UMaterialExpressionCurveAtlasRowParameter::GetReferencedTexture() const
{
	return Atlas;
};

#if WITH_EDITOR

void UMaterialExpressionCurveAtlasRowParameter::GetTexturesForceMaterialRecompile(TArray<UTexture *> &Textures) const
{	
	if (Atlas)
	{
		Textures.AddUnique(Atlas);
	}
}


int32 UMaterialExpressionCurveAtlasRowParameter::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (Atlas && Curve)
	{
		// Retrieve the curve index directly from the atlas rather than relying on the scalar parameter defaults
		int32 CurveIndex = 0;
		
		if (Atlas->GetCurveIndex(Curve, CurveIndex))
		{
			DefaultValue = (float)CurveIndex;
			int32 Slot = Compiler->ScalarParameter(ParameterName, DefaultValue);

			// Get Atlas texture object and texture size
			int32 AtlasRef = INDEX_NONE;
			int32 AtlasCode = Compiler->Texture(Atlas, AtlasRef, SAMPLERTYPE_LinearColor, SSM_Clamp_WorldGroupSettings, TMVM_None);
			if (AtlasCode != INDEX_NONE)
			{
				int32 AtlasHeight = Compiler->ComponentMask(Compiler->TextureProperty(AtlasCode, TMTM_TextureSize), false, true, false, false);
				
				// Calculate UVs from height and slot
				// if the input is hooked up, use it, otherwise use the internal constant
				int32 Arg1 = InputTime.GetTracedInput().Expression ? InputTime.Compile(Compiler) : Compiler->Constant(0);
				int32 Arg2 = Compiler->Div(Compiler->Add(Slot, Compiler->Constant(0.5)), AtlasHeight);

				int32 UV = Compiler->AppendVector(Arg1, Arg2);

				// Sample texture
				return Compiler->TextureSample(AtlasCode, UV, SAMPLERTYPE_LinearColor, INDEX_NONE, INDEX_NONE, TMVM_None, SSM_Clamp_WorldGroupSettings, AtlasRef, false);
			}
			else
			{
				return CompilerError(Compiler, TEXT("There was an error when compiling the texture."));
			}
		}
		else
		{
			return CompilerError(Compiler, TEXT("The curve is not contained within the atlas."));
		}
	}
	else if (Atlas)
	{
		return CompilerError(Compiler, TEXT("The curve is not currently set."));
	}
	else if (Curve)
	{
		return CompilerError(Compiler, TEXT("The atlas is not currently set."));
	}

	return CompilerError(Compiler, TEXT("The curve and atlas are not currently set."));
}

void UMaterialExpressionCurveAtlasRowParameter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property 
		&& (PropertyChangedEvent.Property->GetName() == TEXT("Atlas") || PropertyChangedEvent.Property->GetName() == TEXT("Curve")))
	{
		int32 SlotIndex = INDEX_NONE;
		if (Atlas && Curve)
		{
			SlotIndex = Atlas->GradientCurves.Find(Curve);
		}
		if (SlotIndex != INDEX_NONE)
		{
			float NewValue = (float)SlotIndex;
			SetParameterValue(ParameterName, NewValue);
		}
		else
		{
			SetParameterValue(ParameterName, 0.0f);
		}
	}

	// Need to update expression properties before super call (which triggers recompile)
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif


//
// Hair attributes
//

UMaterialExpressionHairAttributes::UMaterialExpressionHairAttributes(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Hair Attributes", "Hair Attributes"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	MenuCategories.Add(ConstructorStatics.NAME_Utility);

#endif

	bUseTangentSpace = true;

#if WITH_EDITORONLY_DATA
	bShowOutputNameOnPin = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("U"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("V"), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Length"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Radius"), 1, 0, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Seed"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Tangent"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Root UV"), 1, 1, 1, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("BaseColor"), 1, 1, 1, 1, 0));
	Outputs.Add(FExpressionOutput(TEXT("Roughness"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Depth"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("Coverage"), 1, 1, 0, 0, 0));
	Outputs.Add(FExpressionOutput(TEXT("AuxilaryData"), 1, 1, 1, 1, 1));
	Outputs.Add(FExpressionOutput(TEXT("AtlasUVs"), 1, 1, 1, 0, 0));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionHairAttributes::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex == 0 || OutputIndex == 1)
	{
		return Compiler->GetHairUV();
	}
	else if (OutputIndex == 2 || OutputIndex == 3)
	{
		return Compiler->GetHairDimensions();
	}
	else if (OutputIndex == 4)
	{
		return Compiler->GetHairSeed();
	}
	else if (OutputIndex == 5)
	{
		return Compiler->GetHairTangent(bUseTangentSpace);
	}
	else if (OutputIndex == 6)
	{
		return Compiler->GetHairRootUV();
	}
	else if (OutputIndex == 7)
	{
		return Compiler->GetHairBaseColor();
	}
	else if (OutputIndex == 8)
	{
		return Compiler->GetHairRoughness();
	}
	else if (OutputIndex == 9)
	{
		return Compiler->GetHairDepth();
	}
	else if (OutputIndex == 10)
	{
		return Compiler->GetHairCoverage();
	}
	else if (OutputIndex == 11)
	{
		return Compiler->GetHairAuxilaryData();
	}
	else if (OutputIndex == 12)
	{
		return Compiler->GetHairAtlasUVs();
	}

	return Compiler->Errorf(TEXT("Invalid input parameter"));
}

void UMaterialExpressionHairAttributes::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Hair Attributes"));
}
#endif // WITH_EDITOR

//
// Hair Color
//

UMaterialExpressionHairColor::UMaterialExpressionHairColor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Hair Color", "Hair Color"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	MenuCategories.Add(ConstructorStatics.NAME_Utility);

#endif

#if WITH_EDITORONLY_DATA
	bShowOutputNameOnPin = true;
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Color"), 1, 1, 1, 1, 0));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionHairColor::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 MelaninInput = Melanin.GetTracedInput().Expression ? Melanin.Compile(Compiler) : Compiler->Constant(0.5f);
	int32 RednessInput = Redness.GetTracedInput().Expression ? Redness.Compile(Compiler) : Compiler->Constant(0.0f);
	int32 DyeColorInput = DyeColor.GetTracedInput().Expression ? DyeColor.Compile(Compiler) : Compiler->Constant3(1.f,1.f, 1.f);

	return Compiler->GetHairColorFromMelanin(
		MelaninInput,
		RednessInput,
		DyeColorInput);
}

void UMaterialExpressionHairColor::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Hair Color"));
}
#endif // WITH_EDITOR

//
//  UMaterialExpressionARPassthroughCameraUVs
//

UMaterialExpressionMapARPassthroughCameraUV::UMaterialExpressionMapARPassthroughCameraUV(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Coordinates;
		FConstructorStatics()
			: NAME_Coordinates(LOCTEXT("Coordinates", "Coordinates"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	MenuCategories.Add(ConstructorStatics.NAME_Coordinates);
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
int32 UMaterialExpressionMapARPassthroughCameraUV::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Coordinates.GetTracedInput().Expression)
	{
		return CompilerError(Compiler, TEXT("UV input missing"));
	}
	else
	{
		int32 Index = Coordinates.Compile(Compiler);
		return Compiler->MapARPassthroughCameraUV(Index);
	}
}

void UMaterialExpressionMapARPassthroughCameraUV::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Map AR Passthrough Camera UVs"));
}
#endif // WITH_EDITOR
//
//	UMaterialExpressionShadingModel
//
UMaterialExpressionShadingModel::UMaterialExpressionShadingModel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA

	MenuCategories.Add(LOCTEXT("Shading Model", "Shading Model"));

	// bShaderInputData = true;
	bShowOutputNameOnPin = true;
	
	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionShadingModel::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	// Maybe add the shading model to the material here. Don't forget to clear the material shading model list before compilation though
	return Compiler->ShadingModel(ShadingModel);
}

void UMaterialExpressionShadingModel::GetCaption(TArray<FString>& OutCaptions) const
{
	const UEnum* ShadingModelEnum = FindObject<UEnum>(nullptr, TEXT("Engine.EMaterialShadingModel"));
	check(ShadingModelEnum);

	const FString ShadingModelDisplayName = ShadingModelEnum->GetDisplayNameTextByValue(ShadingModel).ToString();

	// Add as a stack, last caption to be added will be the main (bold) caption
	OutCaptions.Add(ShadingModelDisplayName);
	OutCaptions.Add(TEXT("Shading Model"));
}

uint32 UMaterialExpressionShadingModel::GetOutputType(int32 OutputIndex)
{
	return MCT_ShadingModel;
}
#endif // WITH_EDITOR

UMaterialExpressionSingleLayerWaterMaterialOutput::UMaterialExpressionSingleLayerWaterMaterialOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Water;
		FConstructorStatics()
			: NAME_Water(LOCTEXT("Water", "Water"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Water);
#endif

#if WITH_EDITOR
	Outputs.Reset();
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionSingleLayerWaterMaterialOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 CodeInput = INDEX_NONE;

	if (!ScatteringCoefficients.IsConnected() && !AbsorptionCoefficients.IsConnected() && !PhaseG.IsConnected())
	{
		Compiler->Error(TEXT("No inputs to Single Layer Water Material."));
	}

	// Generates function names GetSingleLayerWaterMaterialOutput{index} used in BasePixelShader.usf.
	if (OutputIndex == 0)
	{
		CodeInput = ScatteringCoefficients.IsConnected() ? ScatteringCoefficients.Compile(Compiler) : Compiler->Constant3(0.f, 0.f, 0.f);
	}
	else if (OutputIndex == 1)
	{
		CodeInput = AbsorptionCoefficients.IsConnected() ? AbsorptionCoefficients.Compile(Compiler) : Compiler->Constant3(0.f, 0.f, 0.f);
	}
	else if (OutputIndex == 2)
	{
		CodeInput = PhaseG.IsConnected() ? PhaseG.Compile(Compiler) : Compiler->Constant(0.f);
	}
	else if (OutputIndex == 3)
	{
		CodeInput = ColorScaleBehindWater.IsConnected() ? ColorScaleBehindWater.Compile(Compiler) : Compiler->Constant(1.f);
	}

	return Compiler->CustomOutput(this, OutputIndex, CodeInput);
}

void UMaterialExpressionSingleLayerWaterMaterialOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Single Layer Water Material")));
}

#endif // WITH_EDITOR

int32 UMaterialExpressionSingleLayerWaterMaterialOutput::GetNumOutputs() const
{
	return 4;
}

FString UMaterialExpressionSingleLayerWaterMaterialOutput::GetFunctionName() const
{
	return TEXT("GetSingleLayerWaterMaterialOutput");
}

FString UMaterialExpressionSingleLayerWaterMaterialOutput::GetDisplayName() const
{
	return TEXT("Single Layer Water Material");
}


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionVolumetricAdvancedMaterialOutput
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionVolumetricAdvancedMaterialOutput::UMaterialExpressionVolumetricAdvancedMaterialOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VolumetricAdvancedOutput;
		FConstructorStatics()
			: NAME_VolumetricAdvancedOutput(LOCTEXT("Volume", "Volume"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	ConstPhaseG = 0.0f;
	ConstPhaseG2 = 0.0f;
	ConstPhaseBlend = 0.0f;
	PerSamplePhaseEvaluation = false;

	MultiScatteringApproximationOctaveCount = 0;
	ConstMultiScatteringContribution = 0.5f;
	ConstMultiScatteringOcclusion = 0.5f;
	ConstMultiScatteringEccentricity = 0.5f;

	bGroundContribution = false;
	bGrayScaleMaterial = false;
	bRayMarchVolumeShadow = true;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_VolumetricAdvancedOutput);
#endif

#if WITH_EDITOR
	Outputs.Reset();
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionVolumetricAdvancedMaterialOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 CodeInput = INDEX_NONE;

	// Generates function names GetVolumetricAdvanceMaterialOutput{index} used in BasePixelShader.usf.
	if (OutputIndex == 0)
	{
		CodeInput = PhaseG.IsConnected() ? PhaseG.Compile(Compiler) : Compiler->Constant(ConstPhaseG);
	}
	else if (OutputIndex == 1)
	{
		CodeInput = PhaseG2.IsConnected() ? PhaseG2.Compile(Compiler) : Compiler->Constant(ConstPhaseG2);
	}
	else if (OutputIndex == 2)
	{
		CodeInput = PhaseBlend.IsConnected() ? PhaseBlend.Compile(Compiler) : Compiler->Constant(ConstPhaseBlend);
	}
	else if (OutputIndex == 3)
	{
		CodeInput = MultiScatteringContribution.IsConnected() ? MultiScatteringContribution.Compile(Compiler) : Compiler->Constant(ConstMultiScatteringContribution);
	}
	else if (OutputIndex == 4)
	{
		CodeInput = MultiScatteringOcclusion.IsConnected() ? MultiScatteringOcclusion.Compile(Compiler) : Compiler->Constant(ConstMultiScatteringOcclusion);
	}
	else if (OutputIndex == 5)
	{
		CodeInput = MultiScatteringEccentricity.IsConnected() ? MultiScatteringEccentricity.Compile(Compiler) : Compiler->Constant(ConstMultiScatteringEccentricity);
	}
	else if (OutputIndex == 6)
	{
		CodeInput = ConservativeDensity.IsConnected() ? ConservativeDensity.Compile(Compiler) : Compiler->Constant3(1.0f, 1.0f, 1.0f);
	}

	return Compiler->CustomOutput(this, OutputIndex, CodeInput);
}

void UMaterialExpressionVolumetricAdvancedMaterialOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Volumetric Advanced Output")));
}

bool UMaterialExpressionVolumetricAdvancedMaterialOutput::GetEvaluatePhaseOncePerSample() const
{
	return PerSamplePhaseEvaluation && (PhaseG.IsConnected() || PhaseG2.IsConnected() || PhaseBlend.IsConnected());
}

uint32 UMaterialExpressionVolumetricAdvancedMaterialOutput::GetMultiScatteringApproximationOctaveCount() const
{
	return MultiScatteringApproximationOctaveCount;
}

#endif // WITH_EDITOR

int32 UMaterialExpressionVolumetricAdvancedMaterialOutput::GetNumOutputs() const
{
	return 7;
}

FString UMaterialExpressionVolumetricAdvancedMaterialOutput::GetFunctionName() const
{
	return TEXT("GetVolumetricAdvancedMaterialOutput");
}

FString UMaterialExpressionVolumetricAdvancedMaterialOutput::GetDisplayName() const
{
	return TEXT("Volumetric Advanced Ouput");
}


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionVolumetricAdvancedMaterialInput
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionVolumetricAdvancedMaterialInput::UMaterialExpressionVolumetricAdvancedMaterialInput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_VolumetricAdvancedInput;
		FConstructorStatics()
			: NAME_VolumetricAdvancedInput(LOCTEXT("Volume", "Volume"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	MenuCategories.Add(ConstructorStatics.NAME_VolumetricAdvancedInput);

	bShowOutputNameOnPin = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("ConservativeDensity")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionVolumetricAdvancedMaterialInput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex == 0)
	{
		return Compiler->GetVolumeSampleConservativeDensity();
	}

	return Compiler->Errorf(TEXT("Invalid input parameter"));
}

void UMaterialExpressionVolumetricAdvancedMaterialInput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Volumetric Advanced Input"));
}
#endif // WITH_EDITOR

///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionReflectionCapturePassSwitch
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionReflectionCapturePassSwitch::UMaterialExpressionReflectionCapturePassSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_Utility(LOCTEXT("Utility", "Utility"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionReflectionCapturePassSwitch::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (!Default.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input Default"));
	}
	else if (!Reflection.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing input Reflection"));
	}
	else
	{
		const int32 Arg1 = Default.Compile(Compiler);
		const int32 Arg2 = Reflection.Compile(Compiler);

		return Compiler->ReflectionCapturePassSwitch(Arg1, Arg2);
	}
}

void UMaterialExpressionReflectionCapturePassSwitch::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Reflection Capture Pass Switch"));
}

void UMaterialExpressionReflectionCapturePassSwitch::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Allows material to define specialized behavior when being rendered into reflection capture views."), 40, OutToolTip);
}
#endif // WITH_EDITOR


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionCloudSampleAttribute
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionCloudSampleAttribute::UMaterialExpressionCloudSampleAttribute(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_CloudSampleAttributes;
		FConstructorStatics()
			: NAME_CloudSampleAttributes(LOCTEXT("Volume", "Volume"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	MenuCategories.Add(ConstructorStatics.NAME_CloudSampleAttributes);

	bShowOutputNameOnPin = true;

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT("Altitude")));
	Outputs.Add(FExpressionOutput(TEXT("AltitudeInLayer")));
	Outputs.Add(FExpressionOutput(TEXT("NormAltitudeInLayer")));
	Outputs.Add(FExpressionOutput(TEXT("ShadowSampleDistance")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCloudSampleAttribute::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if (OutputIndex == 0)
	{
		return Compiler->GetCloudSampleAltitude();
	}
	else if (OutputIndex == 1)
	{
		return Compiler->GetCloudSampleAltitudeInLayer();
	}
	else if (OutputIndex == 2)
	{
		return Compiler->GetCloudSampleNormAltitudeInLayer();
	}
	else if (OutputIndex == 3)
	{
		return Compiler->GetCloudSampleShadowSampleDistance();
	}

	return Compiler->Errorf(TEXT("Invalid input parameter"));
}

void UMaterialExpressionCloudSampleAttribute::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Cloud Sample Attributes"));
}

void UMaterialExpressionCloudSampleAttribute::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("Cloud sample attributes.\n- Altitude is the sample atlitude relative to the planet ground (centimeters).\n- AltitudeInLayer is the sample atlitude relative to the cloud layer bottom altitude (centimeters).\n- NormAltitudeInLayer is the normalised sample altitude within the cloud layer (0=bottom, 1=top).\n- ShadowSampleDistance is 0.0 if the sample is used to trace the cloud in view (primary view ray sample). If it is used to trace volumetric shadows, then it is greater than 0.0 and it represents the shadow sample distance in centimeter from the primary view ray sample (during secondary ray marching or Beer shadow map generation): This can help tweaking the shadow strength, skip some code using dynamic branching or sample texture lower mipmaps."), 80, OutToolTip);
}

#endif // WITH_EDITOR


/////////////////////////// THIN TRANSLUCENT


UMaterialExpressionThinTranslucentMaterialOutput::UMaterialExpressionThinTranslucentMaterialOutput(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_ThinTranslucent;
		FConstructorStatics()
			: NAME_ThinTranslucent(LOCTEXT("ThinTranslucent", "ThinTranslucent"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_ThinTranslucent);
#endif

#if WITH_EDITOR
	Outputs.Reset();
#endif
}

#if WITH_EDITOR

int32 UMaterialExpressionThinTranslucentMaterialOutput::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 CodeInput = INDEX_NONE;

	// Generates function names GetSingleLayerWaterMaterialOutput{index} used in BasePixelShader.usf.
	if (OutputIndex == 0)
	{
		CodeInput = TransmittanceColor.IsConnected() ? TransmittanceColor.Compile(Compiler) : Compiler->Constant3(0.5f, 0.5f, 0.5f);
	}

	return Compiler->CustomOutput(this, OutputIndex, CodeInput);
}

void UMaterialExpressionThinTranslucentMaterialOutput::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("Thin Translucent Material")));
}

#endif // WITH_EDITOR

int32 UMaterialExpressionThinTranslucentMaterialOutput::GetNumOutputs() const
{
	return 1;
}

FString UMaterialExpressionThinTranslucentMaterialOutput::GetFunctionName() const
{
	return TEXT("GetThinTranslucentMaterialOutput");
}

FString UMaterialExpressionThinTranslucentMaterialOutput::GetDisplayName() const
{
	return TEXT("Thin Translucent Material");
}


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionSceneDepthWithoutWater
///////////////////////////////////////////////////////////////////////////////
UMaterialExpressionSceneDepthWithoutWater::UMaterialExpressionSceneDepthWithoutWater()
{
#if WITH_EDITORONLY_DATA
	MenuCategories.Add(LOCTEXT("Water", "Water"));

	Outputs.Reset();
	Outputs.Add(FExpressionOutput(TEXT(""), 1, 1, 0, 0, 0));
	bShaderInputData = true;
#endif // WITH_EDITORONLY_DATA

	ConstInput = FVector2D(0.f, 0.f);
}

#if WITH_EDITOR
int32 UMaterialExpressionSceneDepthWithoutWater::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	int32 OffsetIndex = INDEX_NONE;
	int32 CoordinateIndex = INDEX_NONE;
	bool bUseOffset = false;

	if (InputMode == EMaterialSceneAttributeInputMode::OffsetFraction)
	{
		if (Input.GetTracedInput().Expression)
		{
			OffsetIndex = Input.Compile(Compiler);
		}
		else
		{
			OffsetIndex = Compiler->Constant2(ConstInput.X, ConstInput.Y);
		}
		bUseOffset = true;
	}
	else if (InputMode == EMaterialSceneAttributeInputMode::Coordinates)
	{
		if (Input.GetTracedInput().Expression)
		{
			CoordinateIndex = Input.Compile(Compiler);
		}
	}

	int32 Result = Compiler->SceneDepthWithoutWater(OffsetIndex, CoordinateIndex, bUseOffset, FallbackDepth);
	return Result;
}

void UMaterialExpressionSceneDepthWithoutWater::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Scene Depth Without Water"));
}

FName UMaterialExpressionSceneDepthWithoutWater::GetInputName(int32 InputIndex) const
{
	if (InputIndex == 0)
	{
		// Display the current InputMode enum's display name.
		FByteProperty* InputModeProperty = FindFProperty<FByteProperty>(UMaterialExpressionSceneDepthWithoutWater::StaticClass(), "InputMode");
		// Can't use GetNameByValue as GetNameStringByValue does name mangling that GetNameByValue does not
		return *InputModeProperty->Enum->GetNameStringByValue((int64)InputMode.GetValue());
	}
	return NAME_None;
}

#endif // WITH_EDITOR


#undef LOCTEXT_NAMESPACE
