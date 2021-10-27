// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialCachedData.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionShadingModel.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionQualitySwitch.h"
#include "Materials/MaterialExpressionFeatureLevelSwitch.h"
#include "Materials/MaterialExpressionShadingPathSwitch.h"
#include "Materials/MaterialExpressionShaderStageSwitch.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionCurveAtlasRowParameter.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialParameterCollection.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Engine/Font.h"
#include "LandscapeGrassType.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveLinearColorAtlas.h"

const FMaterialCachedParameterEntry FMaterialCachedParameterEntry::EmptyData{};
const FMaterialCachedExpressionData FMaterialCachedExpressionData::EmptyData{};

void FMaterialCachedExpressionData::Reset()
{
	Parameters.Reset();
	ReferencedTextures.Reset();
	FunctionInfos.Reset();
	ParameterCollectionInfos.Reset();
	GrassTypes.Reset();
	DynamicParameterNames.Reset();
	QualityLevelsUsed.Reset();
	QualityLevelsUsed.AddDefaulted(EMaterialQualityLevel::Num);
	bHasMaterialLayers = false;
	bHasRuntimeVirtualTextureOutput = false;
	bHasSceneColor = false;
	bHasPerInstanceCustomData = false;
	bHasPerInstanceRandom = false;
	bHasVertexInterpolator = false;
	MaterialAttributesPropertyConnectedBitmask = 0;

	static_assert((uint32)(EMaterialProperty::MP_MAX)-1 <= (8 * sizeof(MaterialAttributesPropertyConnectedBitmask)), "MaterialAttributesPropertyConnectedBitmask cannot contain entire EMaterialProperty enumeration.");
}

void FMaterialCachedExpressionData::AddReferencedObjects(FReferenceCollector& Collector)
{
	Parameters.AddReferencedObjects(Collector);
	Collector.AddReferencedObjects(ReferencedTextures);
	Collector.AddReferencedObjects(MaterialLayers.Layers);
	Collector.AddReferencedObjects(MaterialLayers.Blends);
	Collector.AddReferencedObjects(GrassTypes);
	for (FMaterialFunctionInfo& FunctionInfo : FunctionInfos)
	{
		Collector.AddReferencedObject(FunctionInfo.Function);
	}
	for (FMaterialParameterCollectionInfo& ParameterCollectionInfo : ParameterCollectionInfos)
	{
		Collector.AddReferencedObject(ParameterCollectionInfo.ParameterCollection);
	}
}

void FMaterialInstanceCachedData::AddReferencedObjects(FReferenceCollector& Collector)
{
	LayerParameters.AddReferencedObjects(Collector);
	Collector.AddReferencedObjects(ReferencedTextures);
}

void FMaterialCachedParameters::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(TextureValues);
	Collector.AddReferencedObjects(RuntimeVirtualTextureValues);
	Collector.AddReferencedObjects(FontValues);
#if WITH_EDITORONLY_DATA
	Collector.AddReferencedObjects(ScalarCurveValues);
	Collector.AddReferencedObjects(ScalarCurveAtlasValues);
#endif
}

#if WITH_EDITOR
static int32 TryAddParameter(FMaterialCachedParameters& CachedParameters,
	EMaterialParameterType Type,
	const FMaterialParameterInfo& ParameterInfo,
	const FMaterialCachedParameterEditorInfo& InEditorInfo)
{
	FMaterialCachedParameterEntry& Entry = CachedParameters.GetParameterTypeEntry(Type);
	FSetElementId ElementId = Entry.ParameterInfoSet.FindId(ParameterInfo);
	int32 Index = INDEX_NONE;
	if (!ElementId.IsValidId())
	{
		ElementId = Entry.ParameterInfoSet.Add(ParameterInfo);
		Index = ElementId.AsInteger();
		Entry.EditorInfo.Insert(InEditorInfo, Index);
		// should be valid as long as we don't ever remove elements from ParameterInfoSet
		check(Entry.ParameterInfoSet.Num() == Entry.EditorInfo.Num());
		return Index;
	}

	// Update any editor values that haven't been set yet
	// TODO still need to do this??
	Index = ElementId.AsInteger();
	FMaterialCachedParameterEditorInfo& EditorInfo = Entry.EditorInfo[Index];
	if (!EditorInfo.ExpressionGuid.IsValid())
	{
		EditorInfo.ExpressionGuid = InEditorInfo.ExpressionGuid;
	}
	if (EditorInfo.Description.IsEmpty())
	{
		EditorInfo.Description = InEditorInfo.Description;
	}
	if (EditorInfo.Group.IsNone())
	{
		EditorInfo.Group = InEditorInfo.Group;
		EditorInfo.SortPriority = InEditorInfo.SortPriority;
	}
	
	// Still return INDEX_NONE, to signify this parameter was already added (don't want to add it again)
	return INDEX_NONE;
}

bool FMaterialCachedExpressionData::UpdateForFunction(const FMaterialCachedExpressionContext& Context, UMaterialFunctionInterface* Function, EMaterialParameterAssociation Association, int32 ParameterIndex)
{
	if (!Function)
	{
		return true;
	}

	bool bResult = true;

	// Update expressions for all dependent functions first, before processing the remaining expressions in this function
	// This is important so we add parameters in the proper order (parameter values are latched the first time a given parameter name is encountered)
	FMaterialCachedExpressionContext LocalContext(Context);
	LocalContext.CurrentFunction = Function;
	LocalContext.bUpdateFunctionExpressions = false; // we update functions explicitly
	
	FMaterialCachedExpressionData* Self = this;
	auto ProcessFunction = [Self, &LocalContext, Association, ParameterIndex, &bResult](UMaterialFunctionInterface* InFunction) -> bool
	{
		const TArray<TObjectPtr<UMaterialExpression>>* FunctionExpressions = InFunction->GetFunctionExpressions();
		if (FunctionExpressions)
		{
			if (!Self->UpdateForExpressions(LocalContext, *FunctionExpressions, Association, ParameterIndex))
			{
				bResult = false;
			}
		}

		FMaterialFunctionInfo NewFunctionInfo;
		NewFunctionInfo.Function = InFunction;
		NewFunctionInfo.StateId = InFunction->StateId;
		Self->FunctionInfos.Add(NewFunctionInfo);

		return true;
	};
	Function->IterateDependentFunctions(ProcessFunction);

	ProcessFunction(Function);

	return bResult;
}

bool FMaterialCachedExpressionData::UpdateForLayerFunctions(const FMaterialCachedExpressionContext& Context, const FMaterialLayersFunctions& LayerFunctions)
{
	bool bResult = true;
	for (int32 LayerIndex = 0; LayerIndex < LayerFunctions.Layers.Num(); ++LayerIndex)
	{
		if (!UpdateForFunction(Context, LayerFunctions.Layers[LayerIndex], LayerParameter, LayerIndex))
		{
			bResult = false;
		}
	}

	for (int32 BlendIndex = 0; BlendIndex < LayerFunctions.Blends.Num(); ++BlendIndex)
	{
		if (!UpdateForFunction(Context, LayerFunctions.Blends[BlendIndex], BlendParameter, BlendIndex))
		{
			bResult = false;
		}
	}

	return bResult;
}

bool FMaterialCachedExpressionData::UpdateForExpressions(const FMaterialCachedExpressionContext& Context, const TArray<TObjectPtr<UMaterialExpression>>& Expressions, EMaterialParameterAssociation Association, int32 ParameterIndex)
{
	bool bResult = true;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression)
		{
			bResult = false;
			continue;
		}

		UObject* ReferencedTexture = nullptr;

		FMaterialParameterMetadata ParameterMeta;
		if (Expression->GetParameterValue(ParameterMeta))
		{
			const FName ParameterName = Expression->GetParameterName();

			// If we're processing a function, give that a chance to override the parameter value
			if (Context.CurrentFunction)
			{
				FMaterialParameterMetadata OverrideParameterMeta;
				if (Context.CurrentFunction->GetParameterOverrideValue(ParameterMeta.Value.Type, ParameterName, OverrideParameterMeta))
				{
					ParameterMeta.Value = OverrideParameterMeta.Value;
					ParameterMeta.ExpressionGuid = OverrideParameterMeta.ExpressionGuid;
					ParameterMeta.bUsedAsAtlasPosition = OverrideParameterMeta.bUsedAsAtlasPosition;
					ParameterMeta.ScalarAtlas = OverrideParameterMeta.ScalarAtlas;
					ParameterMeta.ScalarCurve = OverrideParameterMeta.ScalarCurve;
				}
			}

			const FMaterialParameterInfo ParameterInfo(ParameterName, Association, ParameterIndex);
			const FMaterialCachedParameterEditorInfo EditorInfo(ParameterMeta.ExpressionGuid, ParameterMeta.Description, ParameterMeta.Group, ParameterMeta.SortPriority);
			const int32 Index = TryAddParameter(Parameters, ParameterMeta.Value.Type, ParameterInfo, EditorInfo);
			if (Index != INDEX_NONE)
			{
				switch (ParameterMeta.Value.Type)
				{
				case EMaterialParameterType::Scalar:
					Parameters.ScalarValues.Insert(ParameterMeta.Value.AsScalar(), Index);
					Parameters.ScalarMinMaxValues.Insert(FVector2D(ParameterMeta.ScalarMin, ParameterMeta.ScalarMax), Index);
					Parameters.ScalarPrimitiveDataIndexValues.Insert(ParameterMeta.PrimitiveDataIndex, Index);
					if (ParameterMeta.bUsedAsAtlasPosition)
					{
						Parameters.ScalarCurveValues.Insert(ParameterMeta.ScalarCurve.Get(), Index);
						Parameters.ScalarCurveAtlasValues.Insert(ParameterMeta.ScalarAtlas.Get(), Index);
						ReferencedTexture = ParameterMeta.ScalarAtlas.Get();
					}
					else
					{
						Parameters.ScalarCurveValues.Insert(nullptr, Index);
						Parameters.ScalarCurveAtlasValues.Insert(nullptr, Index);
					}
					break;
				case EMaterialParameterType::Vector:
					Parameters.VectorValues.Insert(ParameterMeta.Value.AsLinearColor(), Index);
					Parameters.VectorChannelNameValues.Insert(ParameterMeta.ChannelNames, Index);
					Parameters.VectorUsedAsChannelMaskValues.Insert(ParameterMeta.bUsedAsChannelMask, Index);
					Parameters.VectorPrimitiveDataIndexValues.Insert(ParameterMeta.PrimitiveDataIndex, Index);
					break;
				case EMaterialParameterType::DoubleVector:
					Parameters.DoubleVectorValues.Insert(ParameterMeta.Value.AsVector4d(), Index);
					break;
				case EMaterialParameterType::Texture:
					Parameters.TextureValues.Insert(ParameterMeta.Value.Texture, Index);
					Parameters.TextureChannelNameValues.Insert(ParameterMeta.ChannelNames, Index);
					ReferencedTexture = ParameterMeta.Value.Texture;
					break;
				case EMaterialParameterType::Font:
					Parameters.FontValues.Insert(ParameterMeta.Value.Font.Value, Index);
					Parameters.FontPageValues.Insert(ParameterMeta.Value.Font.Page, Index);
					if (ParameterMeta.Value.Font.Value->Textures.IsValidIndex(ParameterMeta.Value.Font.Page))
					{
						ReferencedTexture = ParameterMeta.Value.Font.Value->Textures[ParameterMeta.Value.Font.Page];
					}
					break;
				case EMaterialParameterType::RuntimeVirtualTexture:
					Parameters.RuntimeVirtualTextureValues.Insert(ParameterMeta.Value.RuntimeVirtualTexture, Index);
					ReferencedTexture = ParameterMeta.Value.RuntimeVirtualTexture;
					break;
				case EMaterialParameterType::StaticSwitch:
					Parameters.StaticSwitchValues.Insert(ParameterMeta.Value.AsStaticSwitch(), Index);
					break;
				case EMaterialParameterType::StaticComponentMask:
					Parameters.StaticComponentMaskValues.Insert(ParameterMeta.Value.AsStaticComponentMask(), Index);
					break;
				default:
					checkNoEntry();
					break;
				}
			}
		}

		// We first try to extract the referenced texture from the parameter value, that way we'll also get the proper texture in case value is overriden by a function instance
		const bool bCanReferenceTexture = Expression->CanReferenceTexture();
		if (!ReferencedTexture && bCanReferenceTexture)
		{
			ReferencedTexture = Expression->GetReferencedTexture();
		}

		if (ReferencedTexture)
		{
			checkf(bCanReferenceTexture, TEXT("CanReferenceTexture() returned false, but found a referenced texture"));
			ReferencedTextures.AddUnique(ReferencedTexture);
		}

		if (UMaterialExpressionCollectionParameter* ExpressionCollectionParameter = Cast<UMaterialExpressionCollectionParameter>(Expression))
		{
			UMaterialParameterCollection* Collection = ExpressionCollectionParameter->Collection;
			if (Collection)
			{
				FMaterialParameterCollectionInfo NewInfo;
				NewInfo.ParameterCollection = Collection;
				NewInfo.StateId = Collection->StateId;
				ParameterCollectionInfos.AddUnique(NewInfo);
			}
		}
		else if (UMaterialExpressionDynamicParameter* ExpressionDynamicParameter = Cast< UMaterialExpressionDynamicParameter>(Expression))
		{
			DynamicParameterNames.Empty(ExpressionDynamicParameter->ParamNames.Num());
			for (const FString& Name : ExpressionDynamicParameter->ParamNames)
			{
				DynamicParameterNames.Add(*Name);
			}
		}
		else if (UMaterialExpressionLandscapeGrassOutput* ExpressionGrassOutput = Cast<UMaterialExpressionLandscapeGrassOutput>(Expression))
		{
			for (const auto& Type : ExpressionGrassOutput->GrassTypes)
			{
				GrassTypes.AddUnique(Type.GrassType);
			}
		}
		else if (UMaterialExpressionQualitySwitch* QualitySwitchNode = Cast<UMaterialExpressionQualitySwitch>(Expression))
		{
			const FExpressionInput DefaultInput = QualitySwitchNode->Default.GetTracedInput();

			for (int32 InputIndex = 0; InputIndex < EMaterialQualityLevel::Num; InputIndex++)
			{
				if (QualitySwitchNode->Inputs[InputIndex].IsConnected())
				{
					// We can ignore quality levels that are defined the same way as 'Default'
					// This avoids compiling a separate explicit quality level resource, that will end up exactly the same as the default resource
					const FExpressionInput Input = QualitySwitchNode->Inputs[InputIndex].GetTracedInput();
					if (Input.Expression != DefaultInput.Expression ||
						Input.OutputIndex != DefaultInput.OutputIndex)
					{
						QualityLevelsUsed[InputIndex] = true;
					}
				}
			}
		}
		else if (Expression->IsA(UMaterialExpressionRuntimeVirtualTextureOutput::StaticClass()))
		{
			bHasRuntimeVirtualTextureOutput = true;
		}
		else if (Expression->IsA(UMaterialExpressionSceneColor::StaticClass()))
		{
			bHasSceneColor = true;
		}
		else if (Expression->IsA(UMaterialExpressionPerInstanceRandom::StaticClass()))
		{
			bHasPerInstanceRandom = true;
		}
		else if (Expression->IsA(UMaterialExpressionPerInstanceCustomData::StaticClass()))
		{
			bHasPerInstanceCustomData = true;
		}
		else if (Expression->IsA(UMaterialExpressionVertexInterpolator::StaticClass()))
		{
			bHasVertexInterpolator = true;
		}
		else if (UMaterialExpressionMaterialAttributeLayers* LayersExpression = Cast<UMaterialExpressionMaterialAttributeLayers>(Expression))
		{
			checkf(Association == GlobalParameter, TEXT("UMaterialExpressionMaterialAttributeLayers can't be nested"));
			// Only a single layers expression is allowed/expected...creating additional layer expression will cause a compile error
			if (!bHasMaterialLayers)
			{
				if (!UpdateForLayerFunctions(Context, LayersExpression->DefaultLayers))
				{
					bResult = false;
				}

				bHasMaterialLayers = true;
				MaterialLayers = LayersExpression->DefaultLayers;
				MaterialLayers.LinkAllLayersToParent(); // Initialize all the link states (there is no parent for base materials)
				LayersExpression->RebuildLayerGraph(false);
			}
		}
		else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			if (Context.bUpdateFunctionExpressions)
			{
				if (!UpdateForFunction(Context, FunctionCall->MaterialFunction, GlobalParameter, -1))
				{
					bResult = false;
				}

				// Update the function call node, so it can relink inputs and outputs as needed
				// Update even if MaterialFunctionNode->MaterialFunction is NULL, because we need to remove the invalid inputs in that case
				FunctionCall->UpdateFromFunctionResource();
			}
		}
		else if (UMaterialExpressionSetMaterialAttributes* SetMatAttributes = Cast<UMaterialExpressionSetMaterialAttributes>(Expression))
		{
			for (int PinIndex = 0; PinIndex < SetMatAttributes->AttributeSetTypes.Num(); ++PinIndex)
			{
				// For this material attribute pin do we have something connected?
				const FGuid& Guid = SetMatAttributes->AttributeSetTypes[PinIndex];
				const FExpressionInput& AttributeInput = SetMatAttributes->Inputs[PinIndex + 1];
				const EMaterialProperty MaterialProperty = FMaterialAttributeDefinitionMap::GetProperty(Guid);

				// Only set the material property if it hasn't been set yet.  We want to specifically avoid a Set Material Attributes node which doesn't have a 
				// attribute set from disabling the attribute from a different Set Material Attributes node which does have it enabled.
				if (!IsMaterialAttributePropertyConnected(MaterialProperty))
				{
					SetMaterialAttributePropertyConnected(MaterialProperty, AttributeInput.Expression ? true : false);
				}
			}
			}
			else if (UMaterialExpressionMakeMaterialAttributes* MakeMatAttributes = Cast<UMaterialExpressionMakeMaterialAttributes>(Expression))
			{
			// Only set the material property if it hasn't been set yet.  We want to specifically avoid a Set Material Attributes node which doesn't have a 
			// attribute set from disabling the attribute from a different Set Material Attributes node which does have it enabled.
			auto SetMatAttributeConditionally = [&](EMaterialProperty InMaterialProperty, bool InIsConnected)
			{
				if (!IsMaterialAttributePropertyConnected(InMaterialProperty))
				{
					SetMaterialAttributePropertyConnected(InMaterialProperty, InIsConnected);
				}
			};

			SetMatAttributeConditionally(EMaterialProperty::MP_BaseColor, MakeMatAttributes->BaseColor.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Metallic, MakeMatAttributes->Metallic.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Specular, MakeMatAttributes->Specular.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Roughness, MakeMatAttributes->Roughness.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Anisotropy, MakeMatAttributes->Anisotropy.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_EmissiveColor, MakeMatAttributes->EmissiveColor.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Opacity, MakeMatAttributes->Opacity.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_OpacityMask, MakeMatAttributes->OpacityMask.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Normal, MakeMatAttributes->Normal.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Tangent, MakeMatAttributes->Tangent.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_WorldPositionOffset, MakeMatAttributes->WorldPositionOffset.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_SubsurfaceColor, MakeMatAttributes->SubsurfaceColor.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomData0, MakeMatAttributes->ClearCoat.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomData1, MakeMatAttributes->ClearCoatRoughness.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_AmbientOcclusion, MakeMatAttributes->AmbientOcclusion.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_Refraction, MakeMatAttributes->Refraction.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs0, MakeMatAttributes->CustomizedUVs[0].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs1, MakeMatAttributes->CustomizedUVs[1].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs2, MakeMatAttributes->CustomizedUVs[2].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs3, MakeMatAttributes->CustomizedUVs[3].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs4, MakeMatAttributes->CustomizedUVs[4].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs5, MakeMatAttributes->CustomizedUVs[5].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs6, MakeMatAttributes->CustomizedUVs[6].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_CustomizedUVs7, MakeMatAttributes->CustomizedUVs[7].IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_PixelDepthOffset, MakeMatAttributes->PixelDepthOffset.IsConnected());
			SetMatAttributeConditionally(EMaterialProperty::MP_ShadingModel, MakeMatAttributes->ShadingModel.IsConnected());
		}
	}

	return bResult;
}
#endif // WITH_EDITOR

void FMaterialCachedParameterEntry::Reset()
{
	ParameterInfoSet.Reset();
#if WITH_EDITORONLY_DATA
	EditorInfo.Reset();
#endif
}

void FMaterialCachedParameters::Reset()
{
	for (int32 i = 0; i < NumMaterialRuntimeParameterTypes; ++i)
	{
		RuntimeEntries[i].Reset();
	}
#if WITH_EDITORONLY_DATA
	for (int32 i = 0; i < NumMaterialEditorOnlyParameterTypes; ++i)
	{
		EditorOnlyEntries[i].Reset();
	}
#endif

	ScalarPrimitiveDataIndexValues.Reset();
	VectorPrimitiveDataIndexValues.Reset();
	ScalarValues.Reset();
	VectorValues.Reset();
	DoubleVectorValues.Reset();
	TextureValues.Reset();
	FontValues.Reset();
	FontPageValues.Reset();
	RuntimeVirtualTextureValues.Reset();

#if WITH_EDITORONLY_DATA
	StaticSwitchValues.Reset();
	StaticComponentMaskValues.Reset();
	ScalarMinMaxValues.Reset();
	ScalarCurveValues.Reset();
	ScalarCurveAtlasValues.Reset();
	VectorChannelNameValues.Reset();
	VectorUsedAsChannelMaskValues.Reset();
	TextureChannelNameValues.Reset();
#endif // WITH_EDITORONLY_DATA
}

int32 FMaterialCachedParameters::FindParameterIndex(EMaterialParameterType Type, const FMemoryImageMaterialParameterInfo& ParameterInfo) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	const FSetElementId ElementId = Entry.ParameterInfoSet.FindId(FMaterialParameterInfo(ParameterInfo));
	return ElementId.AsInteger();
}

void FMaterialCachedParameters::GetParameterValueByIndex(EMaterialParameterType Type, int32 ParameterIndex, FMaterialParameterMetadata& OutResult) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	
#if WITH_EDITORONLY_DATA
	const bool bIsEditorOnlyDataStripped = Entry.EditorInfo.Num() == 0;
	if (!bIsEditorOnlyDataStripped)
	{
		const FMaterialCachedParameterEditorInfo& EditorInfo = Entry.EditorInfo[ParameterIndex];
		OutResult.ExpressionGuid = EditorInfo.ExpressionGuid;
		OutResult.Description = EditorInfo.Description;
		OutResult.Group = EditorInfo.Group;
		OutResult.SortPriority = EditorInfo.SortPriority;
	}
#endif

	switch (Type)
	{
	case EMaterialParameterType::Scalar:
		OutResult.Value = ScalarValues[ParameterIndex];
		OutResult.PrimitiveDataIndex = ScalarPrimitiveDataIndexValues[ParameterIndex];

#if WITH_EDITORONLY_DATA
		if (!bIsEditorOnlyDataStripped)
		{
			OutResult.ScalarMin = ScalarMinMaxValues[ParameterIndex].X;
			OutResult.ScalarMax = ScalarMinMaxValues[ParameterIndex].Y;
			{
				UCurveLinearColor* Curve = ScalarCurveValues[ParameterIndex];
				UCurveLinearColorAtlas* Atlas = ScalarCurveAtlasValues[ParameterIndex];
				if (Curve && Atlas)
				{
					OutResult.ScalarCurve = Curve;
					OutResult.ScalarAtlas = Atlas;
					OutResult.bUsedAsAtlasPosition = true;
				}
			}
		}
#endif // WITH_EDITORONLY_DATA
		break;
	case EMaterialParameterType::Vector:
		OutResult.Value = VectorValues[ParameterIndex];
		OutResult.PrimitiveDataIndex = VectorPrimitiveDataIndexValues[ParameterIndex];

#if  WITH_EDITORONLY_DATA
		if (!bIsEditorOnlyDataStripped)
		{
			OutResult.ChannelNames = VectorChannelNameValues[ParameterIndex];
			OutResult.bUsedAsChannelMask = VectorUsedAsChannelMaskValues[ParameterIndex];
		}
#endif // WITH_EDITORONLY_DATA
		break;
	case EMaterialParameterType::DoubleVector:
		OutResult.Value = DoubleVectorValues[ParameterIndex];
		break;
	case EMaterialParameterType::Texture:
		OutResult.Value = TextureValues[ParameterIndex];
#if WITH_EDITORONLY_DATA
		if (!bIsEditorOnlyDataStripped)
		{
			OutResult.ChannelNames = TextureChannelNameValues[ParameterIndex];
		}
#endif // WITH_EDITORONLY_DATA
		break;
	case EMaterialParameterType::RuntimeVirtualTexture:
		OutResult.Value = RuntimeVirtualTextureValues[ParameterIndex];
		break;
	case EMaterialParameterType::Font:
		OutResult.Value = FMaterialParameterValue(FontValues[ParameterIndex], FontPageValues[ParameterIndex]);
		break;
#if WITH_EDITORONLY_DATA
	case EMaterialParameterType::StaticSwitch:
		if (!bIsEditorOnlyDataStripped)
		{
			OutResult.Value = StaticSwitchValues[ParameterIndex];
		}
		break;
	case EMaterialParameterType::StaticComponentMask:
		if (!bIsEditorOnlyDataStripped)
		{
			OutResult.Value = StaticComponentMaskValues[ParameterIndex];
		}
		break;
#endif // WITH_EDITORONLY_DATA
	default:
		checkNoEntry();
		break;
	}
}

bool FMaterialCachedParameters::GetParameterValue(EMaterialParameterType Type, const FMemoryImageMaterialParameterInfo& ParameterInfo, FMaterialParameterMetadata& OutResult) const
{
	const int32 Index = FindParameterIndex(Type, ParameterInfo);
	if (Index != INDEX_NONE)
	{
		GetParameterValueByIndex(Type, Index, OutResult);
		return true;
	}

	return false;
}

#if WITH_EDITORONLY_DATA
const FGuid& FMaterialCachedParameters::GetExpressionGuid(EMaterialParameterType Type, int32 Index) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	return Entry.EditorInfo[Index].ExpressionGuid;
}
#endif // WITH_EDITORONLY_DATA

void FMaterialCachedParameters::GetAllParametersOfType(EMaterialParameterType Type, TMap<FMaterialParameterInfo, FMaterialParameterMetadata>& OutParameters) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	const int32 NumParameters = Entry.ParameterInfoSet.Num();
	OutParameters.Reserve(OutParameters.Num() + NumParameters);

	for (int32 ParameterIndex = 0; ParameterIndex < NumParameters; ++ParameterIndex)
	{
		const FMaterialParameterInfo& ParameterInfo = Entry.ParameterInfoSet[FSetElementId::FromInteger(ParameterIndex)];
		FMaterialParameterMetadata& Result = OutParameters.Emplace(ParameterInfo);
		GetParameterValueByIndex(Type, ParameterIndex, Result);
	}
}

void FMaterialCachedParameters::GetAllParameterInfoOfType(EMaterialParameterType Type, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	const int32 NumParameters = Entry.ParameterInfoSet.Num();
	OutParameterInfo.Reserve(OutParameterInfo.Num() + NumParameters);
	OutParameterIds.Reserve(OutParameterIds.Num() + NumParameters);

	for (TSet<FMaterialParameterInfo>::TConstIterator It(Entry.ParameterInfoSet); It; ++It)
	{
		OutParameterInfo.Add(*It);
#if WITH_EDITORONLY_DATA
		// cooked materials can strip out expression guids
		if (Entry.EditorInfo.Num() != 0)
		{
			OutParameterIds.Add(Entry.EditorInfo[It.GetId().AsInteger()].ExpressionGuid);
		}
		else
#endif
		{
			OutParameterIds.Add(FGuid());
		}
	}
}

void FMaterialCachedParameters::GetAllGlobalParametersOfType(EMaterialParameterType Type, TMap<FMaterialParameterInfo, FMaterialParameterMetadata>& OutParameters) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	const int32 NumParameters = Entry.ParameterInfoSet.Num();
	OutParameters.Reserve(OutParameters.Num() + NumParameters);

	for (int32 ParameterIndex = 0; ParameterIndex < NumParameters; ++ParameterIndex)
	{
		const FMaterialParameterInfo& ParameterInfo = Entry.ParameterInfoSet[FSetElementId::FromInteger(ParameterIndex)];
		if (ParameterInfo.Association == GlobalParameter)
		{
			FMaterialParameterMetadata& Meta = OutParameters.FindOrAdd(ParameterInfo);
			if (Meta.Value.Type == EMaterialParameterType::None)
			{
				GetParameterValueByIndex(Type, ParameterIndex, Meta);
			}
		}
	}
}

void FMaterialCachedParameters::GetAllGlobalParameterInfoOfType(EMaterialParameterType Type, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	const FMaterialCachedParameterEntry& Entry = GetParameterTypeEntry(Type);
	const int32 NumParameters = Entry.ParameterInfoSet.Num();
	OutParameterInfo.Reserve(OutParameterInfo.Num() + NumParameters);
	OutParameterIds.Reserve(OutParameterIds.Num() + NumParameters);

	for (TSet<FMaterialParameterInfo>::TConstIterator It(Entry.ParameterInfoSet); It; ++It)
	{
		const FMaterialParameterInfo& ParameterInfo = *It;
		if (ParameterInfo.Association == GlobalParameter)
		{
			OutParameterInfo.Add(ParameterInfo);
#if WITH_EDITORONLY_DATA
			// cooked materials can strip out expression guids
			if (Entry.EditorInfo.Num() != 0)
			{
				OutParameterIds.Add(Entry.EditorInfo[It.GetId().AsInteger()].ExpressionGuid);
			}
			else
#endif
			{
				OutParameterIds.Add(FGuid());
			}
		}
	}
}

#if WITH_EDITOR
void FMaterialInstanceCachedData::Initialize(FMaterialCachedExpressionData&& InCachedExpressionData, const FMaterialLayersFunctions* Layers, const FMaterialLayersFunctions* ParentLayers)
{
	LayerParameters = MoveTemp(InCachedExpressionData.Parameters);
	ReferencedTextures = MoveTemp(InCachedExpressionData.ReferencedTextures);

	const int32 NumLayers = Layers ? Layers->Layers.Num() : 0;
	ParentLayerIndexRemap.Empty(NumLayers);
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		int32 ParentLayerIndex = INDEX_NONE;
		if (Layers == ParentLayers)
		{
			// No overriden layers for this instance, just pass-thru
			ParentLayerIndex = LayerIndex;
		}
		else if (ParentLayers && Layers->LayerLinkStates[LayerIndex] == EMaterialLayerLinkState::LinkedToParent)
		{
			const FGuid& LayerGuid = Layers->LayerGuids[LayerIndex];
			ParentLayerIndex = ParentLayers->LayerGuids.Find(LayerGuid);
		}
		ParentLayerIndexRemap.Add(ParentLayerIndex);
	}

	NumParentLayers = ParentLayers ? ParentLayers->Layers.Num() : 0;
}
#endif // WITH_EDITOR
