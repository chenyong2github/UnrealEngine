// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialInstance.h"
#include "Stats/StatsMisc.h"
#include "EngineGlobals.h"
#include "EngineModule.h"
#include "BatchedElements.h"
#include "Engine/Font.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/LinkerLoad.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealEngine.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialUniformExpressions.h"
#include "Materials/MaterialInstanceSupport.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/SubsurfaceProfile.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Components.h"
#include "HAL/LowLevelMemTracker.h"
#include "ShaderCodeLibrary.h"
#include "Materials/MaterialExpressionCurveAtlasRowParameter.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "HAL/ThreadHeartBeat.h"
#include "Misc/ScopedSlowTask.h"
#include "ShaderPlatformQualitySettings.h"
#include "MaterialShaderQualitySettings.h"

DECLARE_CYCLE_STAT(TEXT("MaterialInstance CopyMatInstParams"), STAT_MaterialInstance_CopyMatInstParams, STATGROUP_Shaders);
DECLARE_CYCLE_STAT(TEXT("MaterialInstance Serialize"), STAT_MaterialInstance_Serialize, STATGROUP_Shaders);
DECLARE_CYCLE_STAT(TEXT("MaterialInstance CopyUniformParamsInternal"), STAT_MaterialInstance_CopyUniformParamsInternal, STATGROUP_Shaders);

/**
 * Cache uniform expressions for the given material.
 * @param MaterialInstance - The material instance for which to cache uniform expressions.
 */
void CacheMaterialInstanceUniformExpressions(const UMaterialInstance* MaterialInstance, bool bRecreateUniformBuffer)
{
	if (MaterialInstance->Resource)
	{
		MaterialInstance->Resource->CacheUniformExpressions_GameThread(bRecreateUniformBuffer);
	}
}

#if WITH_EDITOR
/**
 * Recaches uniform expressions for all material instances with a given parent.
 * WARNING: This function is a noop outside of the Editor!
 * @param ParentMaterial - The parent material to look for.
 */
void RecacheMaterialInstanceUniformExpressions(const UMaterialInterface* ParentMaterial, bool bRecreateUniformBuffer)
{
	if (GIsEditor && FApp::CanEverRender())
	{
		UE_LOG(LogMaterial,Verbose,TEXT("Recaching MI Uniform Expressions for parent %s"), *ParentMaterial->GetFullName());
		TArray<FMICReentranceGuard> ReentranceGuards;
		for (TObjectIterator<UMaterialInstance> It; It; ++It)
		{
			UMaterialInstance* MaterialInstance = *It;
			do 
			{
				if (MaterialInstance->Parent == ParentMaterial)
				{
					UE_LOG(LogMaterial,Verbose,TEXT("--> %s"), *MaterialInstance->GetFullName());
					CacheMaterialInstanceUniformExpressions(*It, bRecreateUniformBuffer);
					break;
				}
				new (ReentranceGuards) FMICReentranceGuard(MaterialInstance);
				MaterialInstance = Cast<UMaterialInstance>(MaterialInstance->Parent);
			} while (MaterialInstance && !MaterialInstance->GetReentrantFlag());
			ReentranceGuards.Reset();
		}
	}
}
#endif // #if WITH_EDITOR

FFontParameterValue::ValueType FFontParameterValue::GetValue(const FFontParameterValue& Parameter)
{
	ValueType Value = NULL;
	if (Parameter.FontValue && Parameter.FontValue->Textures.IsValidIndex(Parameter.FontPage))
	{
		// get the texture for the font page
		Value = Parameter.FontValue->Textures[Parameter.FontPage];
	}
	return Value;
}

FMaterialInstanceResource::FMaterialInstanceResource(UMaterialInstance* InOwner)
	: Parent(NULL)
	, Owner(InOwner)
	, GameThreadParent(NULL)
{
}

#if 0
const FMaterial& FMaterialInstanceResource::GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const
{
	checkSlow(IsInParallelRenderingThread());

	if (Parent)
	{
		if (Owner->bHasStaticPermutationResource)
		{
			const EMaterialQualityLevel::Type ActiveQualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
			FMaterialResource* StaticPermutationResource = FindMaterialResource(Owner->StaticPermutationMaterialResources, InFeatureLevel, ActiveQualityLevel, true);
			if (StaticPermutationResource)
			{
				if (StaticPermutationResource->IsRenderingThreadShaderMapComplete())
				{
					// Verify that compilation has been finalized, the rendering thread shouldn't be touching it otherwise
					checkSlow(StaticPermutationResource->GetRenderingThreadShaderMap()->IsCompilationFinalized());
					// The shader map reference should have been NULL'ed if it did not compile successfully
					checkSlow(StaticPermutationResource->GetRenderingThreadShaderMap()->CompiledSuccessfully());
					return *StaticPermutationResource;
				}
				else
				{
					EMaterialDomain Domain = (EMaterialDomain)StaticPermutationResource->GetMaterialDomain();
					UMaterial* FallbackMaterial = UMaterial::GetDefaultMaterial(Domain);
					//there was an error, use the default material's resource
					OutFallbackMaterialRenderProxy = FallbackMaterial->GetRenderProxy();
					return OutFallbackMaterialRenderProxy->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
				}
			}
		}
		else
		{
			//use the parent's material resource
			return Parent->GetRenderProxy()->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
		}
	}

	// No Parent, or no StaticPermutationResource. This seems to happen if the parent is in the process of using the default material since it's being recompiled or failed to do so.
	UMaterial* FallbackMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	OutFallbackMaterialRenderProxy = FallbackMaterial->GetRenderProxy();
	return OutFallbackMaterialRenderProxy->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
}
#endif // 0

const FMaterialRenderProxy* FMaterialInstanceResource::GetFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (Parent)
	{
		if (Owner->bHasStaticPermutationResource)
		{
			EMaterialQualityLevel::Type ActiveQualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
			FMaterialResource* StaticPermutationResource = FindMaterialResource(Owner->StaticPermutationMaterialResources, InFeatureLevel, ActiveQualityLevel, true);
			if (StaticPermutationResource)
			{
				EMaterialDomain Domain = (EMaterialDomain)StaticPermutationResource->GetMaterialDomain();
				UMaterial* FallbackMaterial = UMaterial::GetDefaultMaterial(Domain);
				//there was an error, use the default material's resource
				return FallbackMaterial->GetRenderProxy();
			}
		}
		else
		{
			//use the parent's material resource
			return Parent->GetRenderProxy()->GetFallback(InFeatureLevel);
		}
	}

	// No Parent, or no StaticPermutationResource. This seems to happen if the parent is in the process of using the default material since it's being recompiled or failed to do so.
	UMaterial* FallbackMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	return FallbackMaterial->GetRenderProxy();
}

const FMaterial* FMaterialInstanceResource::GetMaterialNoFallback(ERHIFeatureLevel::Type InFeatureLevel) const
{
	checkSlow(IsInParallelRenderingThread());

	if (Parent)
	{
		if (Owner->bHasStaticPermutationResource)
		{
			EMaterialQualityLevel::Type ActiveQualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
			const FMaterialResource* StaticPermutationResource = FindMaterialResource(Owner->StaticPermutationMaterialResources, InFeatureLevel, ActiveQualityLevel, true);
			if (StaticPermutationResource && StaticPermutationResource->GetRenderingThreadShaderMap())
			{
				return StaticPermutationResource;
			}
		}
		else
		{
			const FMaterialRenderProxy* ParentProxy = Parent->GetRenderProxy();
			if (ParentProxy)
			{
				return ParentProxy->GetMaterialNoFallback(InFeatureLevel);
			}
		}
	}
	return nullptr;
}

UMaterialInterface* FMaterialInstanceResource::GetMaterialInterface() const
{
	return Owner;
}

bool FMaterialInstanceResource::GetScalarValue(
	const FHashedMaterialParameterInfo& ParameterInfo,
	float* OutValue,
	const FMaterialRenderContext& Context
	) const
{
	checkSlow(IsInParallelRenderingThread());

	static FName NameSubsurfaceProfile(TEXT("__SubsurfaceProfile"));
	if (ParameterInfo.Name == NameSubsurfaceProfile)
	{
		check(ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter);
		const USubsurfaceProfile* MySubsurfaceProfileRT = GetSubsurfaceProfileRT();

		int32 AllocationId = 0;
		if (MySubsurfaceProfileRT)
		{
			// can be optimized (cached)
			AllocationId = GSubsurfaceProfileTextureObject.FindAllocationId(MySubsurfaceProfileRT);
		}
		else
		{
			// no profile specified means we use the default one stored at [0] which is human skin
			AllocationId = 0;
		}
		*OutValue = AllocationId / 255.0f;

		return true;
	}

	const float* Value = RenderThread_FindParameterByName<float>(ParameterInfo);
	if(Value)
	{
		*OutValue = *Value;
		return true;
	}
	else if (Parent)
	{
		return Parent->GetRenderProxy()->GetScalarValue(ParameterInfo, OutValue, Context);
	}
	else
	{
		return false;
	}
}

bool FMaterialInstanceResource::GetVectorValue(
	const FHashedMaterialParameterInfo& ParameterInfo,
	FLinearColor* OutValue,
	const FMaterialRenderContext& Context
	) const
{
	checkSlow(IsInParallelRenderingThread());
	const FLinearColor* Value = RenderThread_FindParameterByName<FLinearColor>(ParameterInfo);
	if(Value)
	{
		*OutValue = *Value;
		return true;
	}
	else if(Parent)
	{
		return Parent->GetRenderProxy()->GetVectorValue(ParameterInfo, OutValue, Context);
	}
	else
	{
		return false;
	}
}

bool FMaterialInstanceResource::GetTextureValue(
	const FHashedMaterialParameterInfo& ParameterInfo,
	const UTexture** OutValue,
	const FMaterialRenderContext& Context
	) const
{
	checkSlow(IsInParallelRenderingThread());
	const UTexture* const * Value = RenderThread_FindParameterByName<const UTexture*>(ParameterInfo);
	if(Value && *Value)
	{
		*OutValue = *Value;
		return true;
	}
	else if(Parent)
	{
		return Parent->GetRenderProxy()->GetTextureValue(ParameterInfo, OutValue, Context);
	}
	else
	{
		return false;
	}
}

bool FMaterialInstanceResource::GetTextureValue(
	const FHashedMaterialParameterInfo& ParameterInfo,
	const URuntimeVirtualTexture** OutValue,
	const FMaterialRenderContext& Context
) const
{
	checkSlow(IsInParallelRenderingThread());
	const URuntimeVirtualTexture* const * Value = RenderThread_FindParameterByName<const URuntimeVirtualTexture*>(ParameterInfo);
	if (Value && *Value)
	{
		*OutValue = *Value;
		return true;
	}
	else if (Parent)
	{
		return Parent->GetRenderProxy()->GetTextureValue(ParameterInfo, OutValue, Context);
	}
	else
	{
		return false;
	}
}

void UMaterialInstance::PropagateDataToMaterialProxy()
{
	if (Resource)
	{
		UpdateMaterialRenderProxy(*Resource);
	}
}

void FMaterialInstanceResource::GameThread_SetParent(UMaterialInterface* ParentMaterialInterface)
{
	// @todo loadtimes: this is no longer valid because of the ParallelFor calling AddPrimitive in UnrealEngine.cpp
	// check(IsInGameThread() || IsAsyncLoading());

	if (GameThreadParent != ParentMaterialInterface)
	{
		// Set the game thread accessible parent.
		UMaterialInterface* OldParent = GameThreadParent;
		GameThreadParent = ParentMaterialInterface;

		// Set the rendering thread's parent and instance pointers.
		check(ParentMaterialInterface != NULL);
		FMaterialInstanceResource* Resource = this;
		ENQUEUE_RENDER_COMMAND(InitMaterialInstanceResource)(
			[Resource, ParentMaterialInterface](FRHICommandListImmediate& RHICmdList)
			{
				Resource->Parent = ParentMaterialInterface;
				Resource->InvalidateUniformExpressionCache(true);
			});

		if (OldParent)
		{
			// make sure that the old parent sticks around until we've set the new parent on FMaterialInstanceResource
			OldParent->ParentRefFence.BeginFence();
		}
	}
}

void FMaterialInstanceResource::InitMIParameters(FMaterialInstanceParameterSet& ParameterSet)
{
	InvalidateUniformExpressionCache(false);
	Swap(ScalarParameterArray, ParameterSet.ScalarParameters);
	Swap(VectorParameterArray, ParameterSet.VectorParameters);
	Swap(TextureParameterArray, ParameterSet.TextureParameters);
	Swap(RuntimeVirtualTextureParameterArray, ParameterSet.RuntimeVirtualTextureParameters);
}

/**
* Updates a parameter on the material instance from the game thread.
*/
template <typename ParameterType>
void GameThread_UpdateMIParameter(const UMaterialInstance* Instance, const ParameterType& Parameter)
{
	if (FApp::CanEverRender())
	{
		FMaterialInstanceResource* Resource = Instance->Resource;
		const FMaterialParameterInfo& ParameterInfo = Parameter.ParameterInfo;
		typename ParameterType::ValueType Value = ParameterType::GetValue(Parameter);
		ENQUEUE_RENDER_COMMAND(SetMIParameterValue)(
			[Resource, ParameterInfo, Value](FRHICommandListImmediate& RHICmdList)
			{
				Resource->RenderThread_UpdateParameter(ParameterInfo, Value);
				Resource->CacheUniformExpressions(false);
			});
	}
}

#if WITH_EDITOR
template<typename ParameterType>
static void RemapLayerParameterIndicesArray(TArray<ParameterType>& Parameters, const TArray<int32>& RemapLayerIndices)
{
	int32 ParameterIndex = 0;
	while (ParameterIndex < Parameters.Num())
	{
		ParameterType& Parameter = Parameters[ParameterIndex];
		bool bRemovedParameter = false;
		if (Parameter.ParameterInfo.Association == LayerParameter)
		{
			const int32 NewIndex = RemapLayerIndices[Parameter.ParameterInfo.Index];
			if (NewIndex != INDEX_NONE)
			{
				Parameter.ParameterInfo.Index = NewIndex;
			}
			else
			{
				bRemovedParameter = true;
			}
		}
		else if (Parameter.ParameterInfo.Association == BlendParameter)
		{
			const int32 NewIndex = RemapLayerIndices[Parameter.ParameterInfo.Index + 1];
			if (NewIndex != INDEX_NONE)
			{
				Parameter.ParameterInfo.Index = NewIndex - 1;
			}
			else
			{
				bRemovedParameter = true;
			}
		}
		if (bRemovedParameter)
		{
			Parameters.RemoveAt(ParameterIndex);
		}
		else
		{
			++ParameterIndex;
		}
	}
}

template<typename ParameterType>
static void SwapLayerParameterIndicesArray(TArray<ParameterType>& Parameters, int32 OriginalIndex, int32 NewIndex)
{
	check(OriginalIndex > 0);
	check(NewIndex > 0);

	for (ParameterType& Parameter : Parameters)
	{
		if (Parameter.ParameterInfo.Association == LayerParameter)
		{
			if(Parameter.ParameterInfo.Index == OriginalIndex) Parameter.ParameterInfo.Index = NewIndex;
			else if (Parameter.ParameterInfo.Index == NewIndex) Parameter.ParameterInfo.Index = OriginalIndex;
		}
		else if (Parameter.ParameterInfo.Association == BlendParameter)
		{
			if (Parameter.ParameterInfo.Index == OriginalIndex - 1) Parameter.ParameterInfo.Index = NewIndex - 1;
			else if (Parameter.ParameterInfo.Index == NewIndex - 1) Parameter.ParameterInfo.Index = OriginalIndex - 1;
		}
	}
}

template<typename ParameterType>
static void RemoveLayerParameterIndicesArray(TArray<ParameterType>& Parameters, int32 RemoveIndex)
{
	int32 ParameterIndex = 0;
	while (ParameterIndex < Parameters.Num())
	{
		ParameterType& Parameter = Parameters[ParameterIndex];
		bool bRemovedParameter = false;
		if (Parameter.ParameterInfo.Association == LayerParameter)
		{
			const int32 Index = Parameter.ParameterInfo.Index;
			if (Index == RemoveIndex)
			{
				bRemovedParameter = true;
			}
			else if (Index > RemoveIndex)
			{
				Parameter.ParameterInfo.Index--;
			}
		}
		else if (Parameter.ParameterInfo.Association == BlendParameter)
		{
			const int32 Index = Parameter.ParameterInfo.Index + 1;
			if (Index == RemoveIndex)
			{
				bRemovedParameter = true;
			}
			else if (Index > RemoveIndex)
			{
				Parameter.ParameterInfo.Index--;
			}
		}
		if (bRemovedParameter)
		{
			Parameters.RemoveAt(ParameterIndex);
		}
		else
		{
			++ParameterIndex;
		}
	}
}

void UMaterialInstance::SwapLayerParameterIndices(int32 OriginalIndex, int32 NewIndex)
{
	if (OriginalIndex != NewIndex)
	{
		SwapLayerParameterIndicesArray(ScalarParameterValues, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(VectorParameterValues, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(TextureParameterValues, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(RuntimeVirtualTextureParameterValues, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(FontParameterValues, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(StaticParameters.StaticSwitchParameters, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(StaticParameters.StaticComponentMaskParameters, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(StaticParameters.TerrainLayerWeightParameters, OriginalIndex, NewIndex);
		SwapLayerParameterIndicesArray(StaticParameters.MaterialLayersParameters, OriginalIndex, NewIndex);
	}
}

void UMaterialInstance::RemoveLayerParameterIndex(int32 Index)
{
	RemoveLayerParameterIndicesArray(ScalarParameterValues, Index);
	RemoveLayerParameterIndicesArray(VectorParameterValues, Index);
	RemoveLayerParameterIndicesArray(TextureParameterValues, Index);
	RemoveLayerParameterIndicesArray(RuntimeVirtualTextureParameterValues, Index);
	RemoveLayerParameterIndicesArray(FontParameterValues, Index);
	RemoveLayerParameterIndicesArray(StaticParameters.StaticSwitchParameters, Index);
	RemoveLayerParameterIndicesArray(StaticParameters.StaticComponentMaskParameters, Index);
	RemoveLayerParameterIndicesArray(StaticParameters.TerrainLayerWeightParameters, Index);
	RemoveLayerParameterIndicesArray(StaticParameters.MaterialLayersParameters, Index);
}
#endif // WITH_EDITOR

bool UMaterialInstance::UpdateParameters()
{
	bool bDirty = false;

#if WITH_EDITOR
	if(IsTemplate(RF_ClassDefaultObject)==false)
	{
		// Get a pointer to the parent material.
		UMaterial* ParentMaterial = NULL;
		UMaterialInstance* ParentInst = this;
		while(ParentInst && ParentInst->Parent)
		{
			if(ParentInst->Parent->IsA(UMaterial::StaticClass()))
			{
				ParentMaterial = Cast<UMaterial>(ParentInst->Parent);
				break;
			}
			else
			{
				ParentInst = Cast<UMaterialInstance>(ParentInst->Parent);
			}
		}

		if(ParentMaterial)
		{
			// Scalar parameters
			bDirty = UpdateParameterSet<FScalarParameterValue, UMaterialExpressionScalarParameter>(ScalarParameterValues, ParentMaterial) || bDirty;

			// Vector parameters	
			bDirty = UpdateParameterSet<FVectorParameterValue, UMaterialExpressionVectorParameter>(VectorParameterValues, ParentMaterial) || bDirty;

			// Texture parameters
			bDirty = UpdateParameterSet<FTextureParameterValue, UMaterialExpressionTextureSampleParameter>(TextureParameterValues, ParentMaterial) || bDirty;

			// Runtime Virtual Texture parameters
			bDirty = UpdateParameterSet<FRuntimeVirtualTextureParameterValue, UMaterialExpressionRuntimeVirtualTextureSampleParameter>(RuntimeVirtualTextureParameterValues, ParentMaterial) || bDirty;

			// Font parameters
			bDirty = UpdateParameterSet<FFontParameterValue, UMaterialExpressionFontSampleParameter>(FontParameterValues, ParentMaterial) || bDirty;

			// Static switch parameters
			bDirty = UpdateParameterSet<FStaticSwitchParameter, UMaterialExpressionStaticBoolParameter>(StaticParameters.StaticSwitchParameters, ParentMaterial) || bDirty;

			// Static component mask parameters
			bDirty = UpdateParameterSet<FStaticComponentMaskParameter, UMaterialExpressionStaticComponentMaskParameter>(StaticParameters.StaticComponentMaskParameters, ParentMaterial) || bDirty;

			// Material layers parameters
			bDirty = UpdateParameterSet<FStaticMaterialLayersParameter, UMaterialExpressionMaterialAttributeLayers>(StaticParameters.MaterialLayersParameters, ParentMaterial) || bDirty;

			// Custom parameters
			for (const auto& CustomParameterSetUpdater : CustomParameterSetUpdaters)
			{
				bDirty |= CustomParameterSetUpdater.Execute(StaticParameters, ParentMaterial);
			}
		}

		if (Parent)
		{
			for (FStaticMaterialLayersParameter& LayersParam : StaticParameters.MaterialLayersParameters)
			{
				FMaterialLayersFunctions ParentLayers;
				FGuid ParentGuid;
				if (Parent->GetMaterialLayersParameterValue(LayersParam.ParameterInfo, ParentLayers, ParentGuid))
				{
					TArray<int32> RemapLayerIndices;
					if (LayersParam.Value.ResolveParent(ParentLayers, RemapLayerIndices))
					{
						RemapLayerParameterIndicesArray(ScalarParameterValues, RemapLayerIndices);
						RemapLayerParameterIndicesArray(VectorParameterValues, RemapLayerIndices);
						RemapLayerParameterIndicesArray(TextureParameterValues, RemapLayerIndices);
						RemapLayerParameterIndicesArray(RuntimeVirtualTextureParameterValues, RemapLayerIndices);
						RemapLayerParameterIndicesArray(FontParameterValues, RemapLayerIndices);
						RemapLayerParameterIndicesArray(StaticParameters.StaticSwitchParameters, RemapLayerIndices);
						RemapLayerParameterIndicesArray(StaticParameters.StaticComponentMaskParameters, RemapLayerIndices);
						RemapLayerParameterIndicesArray(StaticParameters.TerrainLayerWeightParameters, RemapLayerIndices);
						RemapLayerParameterIndicesArray(StaticParameters.MaterialLayersParameters, RemapLayerIndices);
						bDirty = true;
					}
				}
			}
		}
	}
#endif // WITH_EDITOR

	return bDirty;
}

UMaterialInstance::UMaterialInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ReleasedByRT(true)
{
	bHasStaticPermutationResource = false;
#if WITH_EDITOR
	ReentrantFlag[0] = false;
	ReentrantFlag[1] = false;
#endif
	ShadingModels = MSM_Unlit;

	PhysMaterial = nullptr;
	for (UPhysicalMaterial*& PhysMat : PhysicalMaterialMap)
	{
		PhysMat = nullptr;
	}
}

void UMaterialInstance::PostInitProperties()	
{
	LLM_SCOPE(ELLMTag::MaterialInstance);
	Super::PostInitProperties();

	if(!HasAnyFlags(RF_ClassDefaultObject))
	{
		Resource = new FMaterialInstanceResource(this);
	}
}

/**
 * Initializes MI parameters from the game thread.
 */
void GameThread_InitMIParameters(const UMaterialInstance& Instance)
{
	if (Instance.HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	FMaterialInstanceResource* Resource = Instance.Resource;
	FMaterialInstanceParameterSet ParameterSet;

	// Scalar parameters
	ParameterSet.ScalarParameters.Reserve(Instance.ScalarParameterValues.Num());
	for (const FScalarParameterValue& Parameter : Instance.ScalarParameterValues)
	{
		auto& ParamRef = ParameterSet.ScalarParameters.AddDefaulted_GetRef();
		ParamRef.Info = Parameter.ParameterInfo;
		ParamRef.Value = FScalarParameterValue::GetValue(Parameter);
	}

	// Vector parameters
	ParameterSet.VectorParameters.Reserve(Instance.VectorParameterValues.Num());
	for (const FVectorParameterValue& Parameter : Instance.VectorParameterValues)
	{
		auto& ParamRef = ParameterSet.VectorParameters.AddDefaulted_GetRef();
		ParamRef.Info = Parameter.ParameterInfo;
		ParamRef.Value = FVectorParameterValue::GetValue(Parameter);
	}

	// Texture + Fonts parameters
	ParameterSet.TextureParameters.Reserve(Instance.TextureParameterValues.Num() + Instance.FontParameterValues.Num());
	for (const FTextureParameterValue& Parameter : Instance.TextureParameterValues)
	{
		auto& ParamRef = ParameterSet.TextureParameters.AddDefaulted_GetRef();
		ParamRef.Info = Parameter.ParameterInfo;
		ParamRef.Value = FTextureParameterValue::GetValue(Parameter);
	}
	for (const FFontParameterValue& Parameter : Instance.FontParameterValues)
	{
		auto& ParamRef = ParameterSet.TextureParameters.AddDefaulted_GetRef();
		ParamRef.Info = Parameter.ParameterInfo;
		ParamRef.Value = FFontParameterValue::GetValue(Parameter);
	}

	// RuntimeVirtualTexture parameters
	ParameterSet.RuntimeVirtualTextureParameters.Reserve(Instance.RuntimeVirtualTextureParameterValues.Num());
	for (const FRuntimeVirtualTextureParameterValue& Parameter : Instance.RuntimeVirtualTextureParameterValues)
	{
		auto& ParamRef = ParameterSet.RuntimeVirtualTextureParameters.AddDefaulted_GetRef();
		ParamRef.Info = Parameter.ParameterInfo;
		ParamRef.Value = FRuntimeVirtualTextureParameterValue::GetValue(Parameter);
	}
	
	ENQUEUE_RENDER_COMMAND(InitMIParameters)(
		[Resource, Parameters = MoveTemp(ParameterSet)](FRHICommandListImmediate& RHICmdList) mutable
		{
			Resource->InitMIParameters(Parameters);
		});
}

void UMaterialInstance::InitResources()
{	
	// Find the instance's parent.
	UMaterialInterface* SafeParent = NULL;
	if (Parent)
	{
		SafeParent = Parent;
	}

	// Don't use the instance's parent if it has a circular dependency on the instance.
	if (SafeParent && SafeParent->IsDependent_Concurrent(this))
	{
		SafeParent = NULL;
	}

	// Don't allow MIDs as parents for material instances.
	if (SafeParent && SafeParent->IsA(UMaterialInstanceDynamic::StaticClass()))
	{
		SafeParent = NULL;
	}

	// If the instance doesn't have a valid parent, use the default material as the parent.
	if (!SafeParent)
	{
		SafeParent = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	checkf(SafeParent, TEXT("Invalid parent on %s"), *GetFullName());

	// Set the material instance's parent on its resources.
	if (Resource != nullptr)
	{
		Resource->GameThread_SetParent(SafeParent);
	}

	GameThread_InitMIParameters(*this);
	PropagateDataToMaterialProxy();

	CacheMaterialInstanceUniformExpressions(this);
}

const UMaterial* UMaterialInstance::GetMaterial() const
{
	check(IsInGameThread() || IsAsyncLoading());
	if(GetReentrantFlag())
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	FMICReentranceGuard	Guard(this);
	if(Parent)
	{
		return Parent->GetMaterial();
	}
	else
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}
}

const UMaterial* UMaterialInstance::GetMaterial_Concurrent(TMicRecursionGuard RecursionGuard) const
{
	if(!Parent || RecursionGuard.Contains(this))
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	RecursionGuard.Set(this);
	return Parent->GetMaterial_Concurrent(RecursionGuard);
}

UMaterial* UMaterialInstance::GetMaterial()
{
	if(GetReentrantFlag())
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	FMICReentranceGuard	Guard(this);
	if(Parent)
	{
		return Parent->GetMaterial();
	}
	else
	{
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}
}

#if WITH_EDITOR
bool UMaterialInstance::GetScalarParameterSliderMinMax(const FHashedMaterialParameterInfo& ParameterInfo, float& OutSliderMin, float& OutSliderMax) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Scalar, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutSliderMin = CachedLayerParameters.ScalarMinMaxValues[ParameterIndex].X;
			OutSliderMax = CachedLayerParameters.ScalarMinMaxValues[ParameterIndex].Y;
			return true;
		}
	}

	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetScalarParameterSliderMinMax(ParameterInfo, OutSliderMin, OutSliderMax);
	}

	return false;
}
#endif // WITH_EDITOR

bool UMaterialInstance::GetScalarParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance override
	const FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(ScalarParameterValues, ParameterInfo);
	if (ParameterValue)
	{
		OutValue = ParameterValue->ParameterValue;
		return true;
	}
	
	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Scalar, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.ScalarValues[ParameterIndex];
			return true;
		}
	}
	
	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetScalarParameterValue(ParameterInfo, OutValue, bOveriddenOnly);
	}
	
	return false;
}

#if WITH_EDITOR
bool UMaterialInstance::IsScalarParameterUsedAsAtlasPosition(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, TSoftObjectPtr<UCurveLinearColor>& OutCurve, TSoftObjectPtr<UCurveLinearColorAtlas>& OutAtlas) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance override
	const FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(ScalarParameterValues, ParameterInfo);
#if WITH_EDITOR
	if (ParameterValue && ParameterValue->AtlasData.Curve.Get() != nullptr && ParameterValue->AtlasData.Atlas.Get() != nullptr)
	{
		OutValue = ParameterValue->AtlasData.bIsUsedAsAtlasPosition;
		OutCurve = ParameterValue->AtlasData.Curve;
		OutAtlas = ParameterValue->AtlasData.Atlas;
		return true;
	}
#endif

	// Instance-included default
	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Scalar, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			UCurveLinearColor* Curve = CachedLayerParameters.ScalarCurveValues[ParameterIndex];
			UCurveLinearColorAtlas* Atlas = CachedLayerParameters.ScalarCurveAtlasValues[ParameterIndex];
			if (Curve && Atlas)
			{
				OutCurve = TSoftObjectPtr<UCurveLinearColor>(FSoftObjectPath(Curve->GetPathName()));
				OutAtlas = TSoftObjectPtr<UCurveLinearColorAtlas>(FSoftObjectPath(Atlas->GetPathName()));
				OutValue = true;
			}
			else
			{
				OutValue = false;
			}
			return true;
		}
	}

	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->IsScalarParameterUsedAsAtlasPosition(ParameterInfo, OutValue, OutCurve, OutAtlas);
	}

	return false;
}
#endif // WITH_EDITOR

bool UMaterialInstance::GetVectorParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance override
	const FVectorParameterValue* ParameterValue = GameThread_FindParameterByName(VectorParameterValues, ParameterInfo);
	if (ParameterValue)
	{
		OutValue = ParameterValue->ParameterValue;
		return true;
	}
	
	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Vector, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.VectorValues[ParameterIndex];
			return true;
		}
	}
	
	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetVectorParameterValue(ParameterInfo, OutValue, bOveriddenOnly);
	}
	
	return false;
}

#if WITH_EDITOR
bool UMaterialInstance::IsVectorParameterUsedAsChannelMask(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		return false;
	}
	
	// Instance-included default
	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Vector, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.VectorUsedAsChannelMaskValues[ParameterIndex];
			return true;
		}
	}
	
	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->IsVectorParameterUsedAsChannelMask(ParameterInfo, OutValue);
	}
	
	return false;
}

bool  UMaterialInstance::GetVectorParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance-included default
	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Vector, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.VectorChannelNameValues[ParameterIndex];
			return true;
		}
	}

	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetVectorParameterChannelNames(ParameterInfo, OutValue);
	}

	return false;
};
#endif // WITH_EDITOR

bool UMaterialInstance::GetTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue, bool bOveriddenOnly) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		OutValue = nullptr;
		return false;
	}

	// Instance override
	const FTextureParameterValue* ParameterValue = GameThread_FindParameterByName(TextureParameterValues, ParameterInfo);
	if (ParameterValue)
	{
		OutValue = ParameterValue->ParameterValue;
		return true;
	}
	
	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Texture, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.TextureValues[ParameterIndex];
			return true;
		}
	}
	
	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetTextureParameterValue(ParameterInfo,OutValue,bOveriddenOnly);
	}
	
	OutValue = nullptr;
	return false;
}

bool UMaterialInstance::GetRuntimeVirtualTextureParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue, bool bOveriddenOnly) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		OutValue = nullptr;
		return false;
	}

	// Instance override
	const FRuntimeVirtualTextureParameterValue* ParameterValue = GameThread_FindParameterByName(RuntimeVirtualTextureParameterValues, ParameterInfo);
	if (ParameterValue)
	{
		OutValue = ParameterValue->ParameterValue;
		return true;
	}

	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::RuntimeVirtualTexture, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.RuntimeVirtualTextureValues[ParameterIndex];
			return true;
		}
	}

	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetRuntimeVirtualTextureParameterValue(ParameterInfo, OutValue, bOveriddenOnly);
	}

	OutValue = nullptr;
	return false;
}


#if WITH_EDITOR
bool  UMaterialInstance::GetTextureParameterChannelNames(const FHashedMaterialParameterInfo& ParameterInfo, FParameterChannelNames& OutValue) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance-included default
	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Texture, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.TextureChannelNameValues[ParameterIndex];
			return true;
		}
	}

	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetTextureParameterChannelNames(ParameterInfo, OutValue);
	}

	return false;
};
#endif // WITH_EDITOR

bool UMaterialInstance::GetFontParameterValue(const FHashedMaterialParameterInfo& ParameterInfo,class UFont*& OutFontValue, int32& OutFontPage, bool bOveriddenOnly) const
{
	bool bFoundAValue = false;

	if (GetReentrantFlag())
	{
		OutFontValue = nullptr;
		OutFontPage = INDEX_NONE;
		return false;
	}

	// Instance override
	const FFontParameterValue* ParameterValue = GameThread_FindParameterByName(FontParameterValues, ParameterInfo);
	if (ParameterValue)
	{
		OutFontValue = ParameterValue->FontValue;
		OutFontPage = ParameterValue->FontPage;
		return true;
	}
	
	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Font, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutFontValue = CachedLayerParameters.FontValues[ParameterIndex];
			OutFontPage = CachedLayerParameters.FontPageValues[ParameterIndex];
			return true;
		}
	}
	
	// Next material in hierarchy
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetFontParameterValue(ParameterInfo, OutFontValue, OutFontPage, bOveriddenOnly);
	}
	
	OutFontValue = nullptr;
	OutFontPage = INDEX_NONE;
	return false;
}

bool UMaterialInstance::GetRefractionSettings(float& OutBiasValue) const
{
	bool bFoundAValue = false;

	FMaterialParameterInfo ParamInfo;
	if( GetLinkerUE4Version() >= VER_UE4_REFRACTION_BIAS_TO_REFRACTION_DEPTH_BIAS )
	{
		static FName NAME_RefractionDepthBias(TEXT("RefractionDepthBias"));
		ParamInfo.Name = NAME_RefractionDepthBias;
	}
	else
	{
		static FName NAME_RefractionBias(TEXT("RefractionBias"));
		ParamInfo.Name = NAME_RefractionBias;
	}

	const FScalarParameterValue* BiasParameterValue = GameThread_FindParameterByName(ScalarParameterValues, ParamInfo);
	if (BiasParameterValue)
	{
		OutBiasValue = BiasParameterValue->ParameterValue;
		return true;
	}
	else if(Parent)
	{
		return Parent->GetRefractionSettings(OutBiasValue);
	}
	else
	{
		return false;
	}
}

int32 UMaterialInstance::GetLayerParameterIndex(EMaterialParameterAssociation Association, UMaterialFunctionInterface* LayerFunction) const
{
	check(Association != GlobalParameter);

	int32 Index = INDEX_NONE;
	for (const FStaticMaterialLayersParameter& LayersParam : GetStaticParameters().MaterialLayersParameters)
	{
		if (LayersParam.bOverride)
		{
			if (Association == BlendParameter) Index = LayersParam.Value.Blends.Find(LayerFunction);
			else if (Association == LayerParameter) Index = LayersParam.Value.Layers.Find(LayerFunction);
		}
	}
	if (Index == INDEX_NONE && Parent)
	{
		Index = Parent->GetLayerParameterIndex(Association, LayerFunction);
	}
	return Index;
}

void UMaterialInstance::GetTextureExpressionValues(const FMaterialResource* MaterialResource, TArray<UTexture*>& OutTextures, TArray< TArray<int32> >* OutIndices) const
{
	check(MaterialResource);
	const FUniformExpressionSet& UniformExpressions = MaterialResource->GetUniformExpressions();

	if (OutIndices) // Try to prevent resizing since this would be expensive.
	{
		uint32 NumTextures = 0u;
		for (uint32 TypeIndex = 0u; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
		{
			NumTextures += UniformExpressions.GetNumTextures((EMaterialTextureParameterType)TypeIndex);
		}
		OutIndices->Empty(NumTextures);
	}

	for(int32 TypeIndex = 0;TypeIndex < NumMaterialTextureParameterTypes;TypeIndex++)
	{
		// Iterate over each of the material's texture expressions.
		for(int32 TextureIndex = 0; TextureIndex < UniformExpressions.GetNumTextures((EMaterialTextureParameterType)TypeIndex); TextureIndex++)
		{
			// Evaluate the expression in terms of this material instance.
			UTexture* Texture = NULL;
			UniformExpressions.GetGameThreadTextureValue((EMaterialTextureParameterType)TypeIndex, TextureIndex, this,*MaterialResource,Texture, true);
			
			if (Texture)
			{
				const int32 InsertIndex = OutTextures.AddUnique(Texture);
				if (OutIndices)
				{
					const FMaterialTextureParameterInfo& Parameter = UniformExpressions.GetTextureParameter((EMaterialTextureParameterType)TypeIndex, TextureIndex);
					if (InsertIndex >= OutIndices->Num())
					{
						OutIndices->AddDefaulted(InsertIndex - OutIndices->Num() + 1);
					}
					(*OutIndices)[InsertIndex].Add(Parameter.TextureIndex);
				}
			}
		}
	}
}

void UMaterialInstance::GetUsedTextures(TArray<UTexture*>& OutTextures, EMaterialQualityLevel::Type QualityLevel, bool bAllQualityLevels, ERHIFeatureLevel::Type FeatureLevel, bool bAllFeatureLevels) const
{
	OutTextures.Empty();

	// Do not care if we're running dedicated server
	if (!FPlatformProperties::IsServerOnly())
	{
		FInt32Range QualityLevelRange(0, EMaterialQualityLevel::Num - 1);
		if (!bAllQualityLevels)
		{
			if (QualityLevel == EMaterialQualityLevel::Num)
			{
				QualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
			}
			QualityLevelRange = FInt32Range(QualityLevel, QualityLevel);
		}

		FInt32Range FeatureLevelRange(0, ERHIFeatureLevel::Num - 1);
		if (!bAllFeatureLevels)
		{
			if (FeatureLevel == ERHIFeatureLevel::Num)
			{
				FeatureLevel = GMaxRHIFeatureLevel;
			}
			FeatureLevelRange = FInt32Range(FeatureLevel, FeatureLevel);
		}

		const UMaterial* BaseMaterial = GetMaterial();
		const UMaterialInstance* MaterialInstanceToUse = this;

		if (BaseMaterial && !BaseMaterial->IsDefaultMaterial())
		{
			// Walk up the material instance chain to the first parent that has static parameters
			while (MaterialInstanceToUse && !MaterialInstanceToUse->bHasStaticPermutationResource)
			{
				MaterialInstanceToUse = Cast<const UMaterialInstance>(MaterialInstanceToUse->Parent);
			}

			// Use the uniform expressions from the lowest material instance with static parameters in the chain, if one exists
			const UMaterialInterface* MaterialToUse = (MaterialInstanceToUse && MaterialInstanceToUse->bHasStaticPermutationResource) ? (const UMaterialInterface*)MaterialInstanceToUse : (const UMaterialInterface*)BaseMaterial;

			TArray<const FMaterialResource*, TInlineAllocator<4>> MatchedResources;
			// Parse all relevant quality and feature levels.
			for (int32 QualityLevelIndex = QualityLevelRange.GetLowerBoundValue(); QualityLevelIndex <= QualityLevelRange.GetUpperBoundValue(); ++QualityLevelIndex)
			{
				for (int32 FeatureLevelIndex = FeatureLevelRange.GetLowerBoundValue(); FeatureLevelIndex <= FeatureLevelRange.GetUpperBoundValue(); ++FeatureLevelIndex)
				{
					const FMaterialResource* MaterialResource = MaterialToUse->GetMaterialResource((ERHIFeatureLevel::Type)FeatureLevelIndex, (EMaterialQualityLevel::Type)QualityLevelIndex);
					if (MaterialResource)
					{
						MatchedResources.AddUnique(MaterialResource);
					}
				}
			}

			for (const FMaterialResource* MaterialResource : MatchedResources)
			{
				GetTextureExpressionValues(MaterialResource, OutTextures);
			}
		}
		else
		{
			// If the material instance has no material, use the default material.
			UMaterial::GetDefaultMaterial(MD_Surface)->GetUsedTextures(OutTextures, QualityLevel, bAllQualityLevels, FeatureLevel, bAllFeatureLevels);
		}
	}
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
void UMaterialInstance::LogMaterialsAndTextures(FOutputDevice& Ar, int32 Indent) const
{
	auto World = GetWorld();
	const EMaterialQualityLevel::Type QualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
	const ERHIFeatureLevel::Type FeatureLevel = World ? World->FeatureLevel.GetValue() : GMaxRHIFeatureLevel;

	Ar.Logf(TEXT("%sMaterialInstance: %s"), FCString::Tab(Indent), *GetName());

	if (FPlatformProperties::IsServerOnly())
	{
		Ar.Logf(TEXT("%sNo Textures: IsServerOnly"), FCString::Tab(Indent + 1));
	}
	else
	{
		const UMaterialInstance* MaterialInstanceToUse = nullptr;
		const UMaterial* MaterialToUse = nullptr;

		const UMaterialInterface* CurrentMaterialInterface = this;
		{
			TSet<const UMaterialInterface*> MaterialParents;

			// Walk up the parent chain to the materials to use.
			while (CurrentMaterialInterface && !MaterialParents.Contains(CurrentMaterialInterface))
			{
				MaterialParents.Add(CurrentMaterialInterface);

				const UMaterialInstance* CurrentMaterialInstance = Cast<const UMaterialInstance>(CurrentMaterialInterface);
				const UMaterial* CurrentMaterial = Cast<const UMaterial>(CurrentMaterialInterface);

				// The parent material is the first parent of this class.
				if (!MaterialToUse && CurrentMaterial)
				{
					MaterialToUse = CurrentMaterial;
				}

				if (!MaterialInstanceToUse && CurrentMaterialInstance && CurrentMaterialInstance->bHasStaticPermutationResource)
				{
					MaterialInstanceToUse = CurrentMaterialInstance;
				}

				CurrentMaterialInterface = CurrentMaterialInstance ? CurrentMaterialInstance->Parent : nullptr;
			}
		}

		if (CurrentMaterialInterface)
		{
			Ar.Logf(TEXT("%sNo Textures : Cycling Parent Loop"), FCString::Tab(Indent + 1));
		}
		else if (MaterialInstanceToUse)
		{
			const FMaterialResource* MaterialResource = FindMaterialResource(MaterialInstanceToUse->StaticPermutationMaterialResources, FeatureLevel, QualityLevel, true);
			if (MaterialResource)
			{
				if (MaterialResource->HasValidGameThreadShaderMap())
				{
					TArray<UTexture*> Textures;
					GetTextureExpressionValues(MaterialResource, Textures);
					for (UTexture* Texture : Textures)
					{
						if (Texture)
						{
							Ar.Logf(TEXT("%s%s"), FCString::Tab(Indent + 1), *Texture->GetName());
						}
					}
				}
				else
				{
					Ar.Logf(TEXT("%sNo Textures : Invalid GameThread ShaderMap"), FCString::Tab(Indent + 1));
				}
			}
			else
			{
				Ar.Logf(TEXT("%sNo Textures : Invalid MaterialResource"), FCString::Tab(Indent + 1));
			}
		}
		else if (MaterialToUse)
		{
			MaterialToUse->LogMaterialsAndTextures(Ar, Indent + 1);
		}
		else
		{
			Ar.Logf(TEXT("%sNo Textures : No Material Found"), FCString::Tab(Indent + 1));
		}
	}
}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void UMaterialInstance::ValidateTextureOverrides(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (!(IsInGameThread() || IsAsyncLoading()))
	{
		// Fatal to call getmaterial in a non-game thread or async loading
		return;
	}

	const UMaterial* Material = GetMaterial();
	const FMaterialResource* CurrentResource = Material->GetMaterialResource(InFeatureLevel);

	if (!CurrentResource)
	{
		return;
	}

	FString MaterialName;
	GetName(MaterialName);

	for (uint32 TypeIndex = 0; TypeIndex < NumMaterialTextureParameterTypes; ++TypeIndex)
	{
		const EMaterialTextureParameterType ParameterType = (EMaterialTextureParameterType)TypeIndex;
		for (const FMaterialTextureParameterInfo& TextureInfo : CurrentResource->GetUniformTextureExpressions(ParameterType))
		{
			UTexture* Texture = nullptr;
			TextureInfo.GetGameThreadTextureValue(this, *CurrentResource, Texture);
			if (Texture)
			{
				const EMaterialValueType TextureType = Texture->GetMaterialType();
				switch (ParameterType)
				{
				case EMaterialTextureParameterType::Standard2D:
					if (!(TextureType & (MCT_Texture2D | MCT_TextureExternal | MCT_TextureVirtual)))
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" has invalid type, required 2D texture"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					else if (TextureType & MCT_TextureVirtual)
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" requires non-virtual texture"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					break;
				case EMaterialTextureParameterType::Cube:
					if (!(TextureType & MCT_TextureCube))
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" has invalid type, required Cube texture"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					break;
				case EMaterialTextureParameterType::Array2D:
					if (!(TextureType & MCT_Texture2DArray))
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" has invalid type, required texture array"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					break;
				case EMaterialTextureParameterType::Volume:
					if (!(TextureType & MCT_VolumeTexture))
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" has invalid type, required Volume texture"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					break;
				case EMaterialTextureParameterType::Virtual:
					if (!(TextureType & (MCT_Texture2D | MCT_TextureExternal | MCT_TextureVirtual)))
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" has invalid type, required 2D texture"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					else if (!(TextureType & MCT_TextureVirtual))
					{
						UE_LOG(LogMaterial, Error, TEXT("MaterialInstance \"%s\" parameter '%s' assigned texture \"%s\" requires virtual texture"), *MaterialName, *TextureInfo.GetParameterName().ToString(), *Texture->GetName());
					}
					break;
				default:
					checkNoEntry();
					break;
				}
			}
		}
	}
}

void UMaterialInstance::GetUsedTexturesAndIndices(TArray<UTexture*>& OutTextures, TArray< TArray<int32> >& OutIndices, EMaterialQualityLevel::Type QualityLevel, ERHIFeatureLevel::Type FeatureLevel) const
{
	OutTextures.Empty();
	OutIndices.Empty();

	if (!FPlatformProperties::IsServerOnly())
	{
		const UMaterialInstance* MaterialInstanceToUse = this;
		// Walk up the material instance chain to the first parent that has static parameters
		while (MaterialInstanceToUse && !MaterialInstanceToUse->bHasStaticPermutationResource)
		{
			MaterialInstanceToUse = Cast<const UMaterialInstance>(MaterialInstanceToUse->Parent);
		}

		if (MaterialInstanceToUse && MaterialInstanceToUse->bHasStaticPermutationResource)
		{
			const FMaterialResource* CurrentResource = FindMaterialResource(MaterialInstanceToUse->StaticPermutationMaterialResources, FeatureLevel, QualityLevel, true);
			if (CurrentResource)
			{
				GetTextureExpressionValues(CurrentResource, OutTextures, &OutIndices);
			}
		}
		else // Use the uniform expressions from the base material
		{ 
			const UMaterial* Material = GetMaterial();
			if (Material)
			{
				const FMaterialResource* MaterialResource = Material->GetMaterialResource(FeatureLevel, QualityLevel);
				if( MaterialResource )
				{
					GetTextureExpressionValues(MaterialResource, OutTextures, &OutIndices);
				}
			}
			else // If the material instance has no material, use the default material.
			{
				UMaterial::GetDefaultMaterial(MD_Surface)->GetUsedTexturesAndIndices(OutTextures, OutIndices, QualityLevel, FeatureLevel);
			}
		}
	}
}

void UMaterialInstance::OverrideTexture(const UTexture* InTextureToOverride, UTexture* OverrideTexture, ERHIFeatureLevel::Type InFeatureLevel)
{
#if WITH_EDITOR
	FMaterialResource* SourceMaterialResource = nullptr;
	if (bHasStaticPermutationResource)
	{
		SourceMaterialResource = GetMaterialResource(InFeatureLevel);
	}
	else
	{
		//@todo - this isn't handling chained MIC's correctly, where a parent in the chain has static parameters
		UMaterial* Material = GetMaterial();
		SourceMaterialResource = Material->GetMaterialResource(InFeatureLevel);
	}

	if (SourceMaterialResource)
	{
		bool bShouldRecacheMaterialExpressions = false;
		for (int32 TypeIndex = 0; TypeIndex < NumMaterialTextureParameterTypes; TypeIndex++)
		{
			const TArrayView<const FMaterialTextureParameterInfo> Parameters = SourceMaterialResource->GetUniformTextureExpressions((EMaterialTextureParameterType)TypeIndex);
			// Iterate over each of the material's texture expressions.
			for (int32 i = 0; i < Parameters.Num(); ++i)
			{
				const FMaterialTextureParameterInfo& Parameter = Parameters[i];

				// Evaluate the expression in terms of this material instance.
				UTexture* Texture = NULL;
				Parameter.GetGameThreadTextureValue(this, *SourceMaterialResource, Texture);
				if (Texture != NULL && Texture == InTextureToOverride)
				{
					// Override this texture!
					SourceMaterialResource->TransientOverrides.SetTextureOverride((EMaterialTextureParameterType)TypeIndex, Parameter.ParameterInfo, OverrideTexture);
					bShouldRecacheMaterialExpressions = true;
				}
			}
		}

		if (bShouldRecacheMaterialExpressions)
		{
			RecacheUniformExpressions(false);
		}
	}
#endif // #if WITH_EDITOR
}

void UMaterialInstance::OverrideVectorParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, const FLinearColor& Value, bool bOverride, ERHIFeatureLevel::Type InFeatureLevel)
{
#if WITH_EDITOR
	bool bShouldRecacheMaterialExpressions = false;
	if (bHasStaticPermutationResource)
	{
		FMaterialResource* SourceMaterialResource = GetMaterialResource(InFeatureLevel);
		if (SourceMaterialResource)
		{
			SourceMaterialResource->TransientOverrides.SetVectorOverride(ParameterInfo, Value, bOverride);

			const TArrayView<const FMaterialVectorParameterInfo> Parameters = SourceMaterialResource->GetUniformVectorParameterExpressions();
			for (int32 i = 0; i < Parameters.Num(); ++i)
			{
				const FMaterialVectorParameterInfo& Parameter = Parameters[i];
				if (Parameter.ParameterInfo == ParameterInfo)
				{
					bShouldRecacheMaterialExpressions = true;
				}
			}
		}
	}

	if (bShouldRecacheMaterialExpressions)
	{
		RecacheUniformExpressions(false);
	}
#endif // #if WITH_EDITOR
}

void UMaterialInstance::OverrideScalarParameterDefault(const FHashedMaterialParameterInfo& ParameterInfo, float Value, bool bOverride, ERHIFeatureLevel::Type InFeatureLevel)
{
#if WITH_EDITOR
	bool bShouldRecacheMaterialExpressions = false;
	if (bHasStaticPermutationResource)
	{
		FMaterialResource* SourceMaterialResource = GetMaterialResource(InFeatureLevel);
		if (SourceMaterialResource)
		{
			SourceMaterialResource->TransientOverrides.SetScalarOverride(ParameterInfo, Value, bOverride);

			const TArrayView<const FMaterialScalarParameterInfo> Parameters = SourceMaterialResource->GetUniformScalarParameterExpressions();
			for (int32 i = 0; i < Parameters.Num(); ++i)
			{
				const FMaterialScalarParameterInfo& Parameter = Parameters[i];
				if (Parameter.ParameterInfo == ParameterInfo)
				{
					bShouldRecacheMaterialExpressions = true;
				}
			}
		}
	}

	if (bShouldRecacheMaterialExpressions)
	{
		RecacheUniformExpressions(false);
	}
#endif // #if WITH_EDITOR
}

bool UMaterialInstance::CheckMaterialUsage(const EMaterialUsage Usage)
{
	check(IsInGameThread());
	UMaterial* Material = GetMaterial();
	if(Material)
	{
		bool bNeedsRecompile = false;
		bool bUsageSetSuccessfully = Material->SetMaterialUsage(bNeedsRecompile, Usage);
		if (bNeedsRecompile)
		{
			CacheResourceShadersForRendering(EMaterialShaderPrecompileMode::None);
			MarkPackageDirty();
		}
		return bUsageSetSuccessfully;
	}
	else
	{
		return false;
	}
}

bool UMaterialInstance::CheckMaterialUsage_Concurrent(const EMaterialUsage Usage) const
{
	UMaterial const* Material = GetMaterial_Concurrent();
	if(Material)
	{
		bool bUsageSetSuccessfully = false;
		if (Material->NeedsSetMaterialUsage_Concurrent(bUsageSetSuccessfully, Usage))
		{
			if (IsInGameThread())
			{
				bUsageSetSuccessfully = const_cast<UMaterialInstance*>(this)->CheckMaterialUsage(Usage);
			}
			else
			{
				struct FCallSMU
				{
					UMaterialInstance* Material;
					EMaterialUsage Usage;

					FCallSMU(UMaterialInstance* InMaterial, EMaterialUsage InUsage)
						: Material(InMaterial)
						, Usage(InUsage)
					{
					}

					void Task()
					{
						Material->CheckMaterialUsage(Usage);
					}
				};
				UE_LOG(LogMaterial, Log, TEXT("Had to pass SMU back to game thread. Please ensure correct material usage flags."));

				TSharedRef<FCallSMU, ESPMode::ThreadSafe> CallSMU = MakeShareable(new FCallSMU(const_cast<UMaterialInstance*>(this), Usage));
				bUsageSetSuccessfully = false;

				DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.CheckMaterialUsage"),
					STAT_FSimpleDelegateGraphTask_CheckMaterialUsage,
					STATGROUP_TaskGraphTasks);

				FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
					FSimpleDelegateGraphTask::FDelegate::CreateThreadSafeSP(CallSMU, &FCallSMU::Task),
					GET_STATID(STAT_FSimpleDelegateGraphTask_CheckMaterialUsage), NULL, ENamedThreads::GameThread_Local
				);
			}
		}
		return bUsageSetSuccessfully;
	}
	else
	{
		return false;
	}
}

bool UMaterialInstance::IsDependent(UMaterialInterface* TestDependency)
{
	if(TestDependency == this)
	{
		return true;
	}
	else if(Parent)
	{
		if(GetReentrantFlag())
		{
			return true;
		}

		FMICReentranceGuard	Guard(this);
		return Parent->IsDependent(TestDependency);
	}
	else
	{
		return false;
	}
}

bool UMaterialInstance::IsDependent_Concurrent(UMaterialInterface* TestDependency, TMicRecursionGuard RecursionGuard)
{
	if (TestDependency == this)
	{
		return true;
	}
	else if (Parent)
	{
		if (RecursionGuard.Contains(this))
		{
			return true;
		}

		RecursionGuard.Set(this);
		return Parent->IsDependent_Concurrent(TestDependency, RecursionGuard);
	}
	else
	{
		return false;
	}
}

void UMaterialInstanceDynamic::CopyScalarAndVectorParameters(const UMaterialInterface& SourceMaterialToCopyFrom, ERHIFeatureLevel::Type FeatureLevel)
{
	check(IsInGameThread());

	// We get the parameter list form the input material, this might be different from the base material
	// because static (bool) parameters can cause some parameters to be hidden
	FMaterialResource* MaterialResource = GetMaterialResource(FeatureLevel);

	if(MaterialResource)
	{
		// first, clear out all the parameter values
		ClearParameterValuesInternal(false);

		// scalar
		{
			TArrayView<const FMaterialScalarParameterInfo> Array = MaterialResource->GetUniformScalarParameterExpressions();

			for (int32 i = 0, Count = Array.Num(); i < Count; ++i)
			{
				const FMaterialScalarParameterInfo& Parameter = Array[i];

				float Value;
				Parameter.GetGameThreadNumberValue(&SourceMaterialToCopyFrom, Value);

				FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(ScalarParameterValues, Parameter.ParameterInfo);
				if (!ParameterValue)
				{
					ParameterValue = new(ScalarParameterValues)FScalarParameterValue;
					ParameterValue->ParameterInfo = FMaterialParameterInfo(Parameter.ParameterInfo);
				}

				ParameterValue->ParameterValue = Value;
			}
		}

		// vector
		{
			TArrayView<const FMaterialVectorParameterInfo> Array = MaterialResource->GetUniformVectorParameterExpressions();

			for (int32 i = 0, Count = Array.Num(); i < Count; ++i)
			{
				const FMaterialVectorParameterInfo& Parameter = Array[i];

				FLinearColor Value;
				Parameter.GetGameThreadNumberValue(&SourceMaterialToCopyFrom, Value);

				FVectorParameterValue* ParameterValue = GameThread_FindParameterByName(VectorParameterValues, Parameter.ParameterInfo);
				if (!ParameterValue)
				{
					ParameterValue = new(VectorParameterValues)FVectorParameterValue;
					ParameterValue->ParameterInfo = FMaterialParameterInfo(Parameter.ParameterInfo);
				}

				ParameterValue->ParameterValue = Value;
			}
		}

		// now, init the resources
		InitResources();
	}
}

float UMaterialInstanceDynamic::GetOpacityMaskClipValue() const
{
	return Parent ? Parent->GetOpacityMaskClipValue() : 0.0f;
}

bool UMaterialInstanceDynamic::GetCastDynamicShadowAsMasked() const
{
	return Parent ? Parent->GetCastDynamicShadowAsMasked() : false;
}

EBlendMode UMaterialInstanceDynamic::GetBlendMode() const
{
	return Parent ? Parent->GetBlendMode() : BLEND_Opaque;
}

bool UMaterialInstanceDynamic::IsTwoSided() const
{
	return Parent ? Parent->IsTwoSided() : false;
}

bool UMaterialInstanceDynamic::IsDitheredLODTransition() const
{
	return Parent ? Parent->IsDitheredLODTransition() : false;
}

bool UMaterialInstanceDynamic::IsMasked() const
{
	return Parent ? Parent->IsMasked() : false;
}

FMaterialShadingModelField UMaterialInstanceDynamic::GetShadingModels() const
{
	return Parent ? Parent->GetShadingModels() : MSM_DefaultLit;
}

bool UMaterialInstanceDynamic::IsShadingModelFromMaterialExpression() const
{
	return Parent ? Parent->IsShadingModelFromMaterialExpression() : false;
}

void UMaterialInstance::CopyMaterialInstanceParameters(UMaterialInterface* Source)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);
	SCOPE_CYCLE_COUNTER(STAT_MaterialInstance_CopyMatInstParams);

	if ((Source != nullptr) && (Source != this))
	{
		// First, clear out all the parameter values
		ClearParameterValuesInternal();

		//setup some arrays to use
		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;
		
		// Handle all the fonts
		GetAllFontParameterInfo(OutParameterInfo, Guids);
		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			UFont* FontValue = nullptr;
			int32 FontPage;
			if (Source->GetFontParameterValue(ParameterInfo, FontValue, FontPage))
			{
				FFontParameterValue* ParameterValue = new(FontParameterValues) FFontParameterValue;
				ParameterValue->ParameterInfo = ParameterInfo;
				ParameterValue->ExpressionGUID.Invalidate();
				ParameterValue->FontValue = FontValue;
				ParameterValue->FontPage = FontPage;
			}
		}

		// Now do the scalar params
		OutParameterInfo.Reset();
		Guids.Reset();
		GetAllScalarParameterInfo(OutParameterInfo, Guids);
		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			float ScalarValue = 1.0f;
			if (Source->GetScalarParameterValue(ParameterInfo, ScalarValue))
			{
				FScalarParameterValue* ParameterValue = new(ScalarParameterValues) FScalarParameterValue;
				ParameterValue->ParameterInfo = ParameterInfo;
				ParameterValue->ExpressionGUID.Invalidate();
				ParameterValue->ParameterValue = ScalarValue;
#if WITH_EDITOR
				IsScalarParameterUsedAsAtlasPosition(ParameterValue->ParameterInfo, ParameterValue->AtlasData.bIsUsedAsAtlasPosition, ParameterValue->AtlasData.Curve, ParameterValue->AtlasData.Atlas);
#endif
			}
		}

		// Now do the vector params
		OutParameterInfo.Reset();
		Guids.Reset();
		GetAllVectorParameterInfo(OutParameterInfo, Guids);
		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			FLinearColor VectorValue;
			if (Source->GetVectorParameterValue(ParameterInfo, VectorValue))
			{
				FVectorParameterValue* ParameterValue = new(VectorParameterValues) FVectorParameterValue;
				ParameterValue->ParameterInfo = ParameterInfo;
				ParameterValue->ExpressionGUID.Invalidate();
				ParameterValue->ParameterValue = VectorValue;
			}
		}

		// Now do the texture params
		OutParameterInfo.Reset();
		Guids.Reset();
		GetAllTextureParameterInfo(OutParameterInfo, Guids);
		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			UTexture* TextureValue = nullptr;
			if (Source->GetTextureParameterValue(ParameterInfo, TextureValue))
			{
				FTextureParameterValue* ParameterValue = new(TextureParameterValues) FTextureParameterValue;
				ParameterValue->ParameterInfo = ParameterInfo;
				ParameterValue->ExpressionGUID.Invalidate();
				ParameterValue->ParameterValue = TextureValue;
			}
		}

		// Now do the runtime virtual texture params
		OutParameterInfo.Reset();
		Guids.Reset();
		GetAllRuntimeVirtualTextureParameterInfo(OutParameterInfo, Guids);
		for (const FMaterialParameterInfo& ParameterInfo : OutParameterInfo)
		{
			URuntimeVirtualTexture* Value = nullptr;
			if (Source->GetRuntimeVirtualTextureParameterValue(ParameterInfo, Value))
			{
				FRuntimeVirtualTextureParameterValue* ParameterValue = new(RuntimeVirtualTextureParameterValues) FRuntimeVirtualTextureParameterValue;
				ParameterValue->ParameterInfo = ParameterInfo;
				ParameterValue->ExpressionGUID.Invalidate();
				ParameterValue->ParameterValue = Value;
			}
		}

		// Now, init the resources
		InitResources();
	}
}

FMaterialResource* UMaterialInstance::GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel)
{
	if (bHasStaticPermutationResource)
	{
		if (QualityLevel == EMaterialQualityLevel::Num)
		{
			QualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;
		}
		return FindMaterialResource(StaticPermutationMaterialResources, InFeatureLevel, QualityLevel, true);
	}

	//there was no static permutation resource
	return Parent ? Parent->GetMaterialResource(InFeatureLevel, QualityLevel) : nullptr;
}

const FMaterialResource* UMaterialInstance::GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel) const
{
	return const_cast<UMaterialInstance*>(this)->GetMaterialResource(InFeatureLevel, QualityLevel);
}

FMaterialRenderProxy* UMaterialInstance::GetRenderProxy() const
{
	return Resource;
}

UPhysicalMaterial* UMaterialInstance::GetPhysicalMaterial() const
{
	if(GetReentrantFlag())
	{
		return UMaterial::GetDefaultMaterial(MD_Surface)->GetPhysicalMaterial();
	}

	FMICReentranceGuard	Guard(const_cast<UMaterialInstance*>(this));  // should not need this to determine loop
	if(PhysMaterial)
	{
		return PhysMaterial;
	}
	else if(Parent)
	{
		// If no physical material has been associated with this instance, simply use the parent's physical material.
		return Parent->GetPhysicalMaterial();
	}
	else
	{
		// no material specified and no parent, fall back to default physical material
		check( GEngine->DefaultPhysMaterial != NULL );
		return GEngine->DefaultPhysMaterial;
	}
}

UPhysicalMaterialMask* UMaterialInstance::GetPhysicalMaterialMask() const
{
	return nullptr;
}

UPhysicalMaterial* UMaterialInstance::GetPhysicalMaterialFromMap(int32 Index) const
{
	if (Index < 0 || Index >= EPhysicalMaterialMaskColor::MAX)
	{
		return nullptr;
	}
	return PhysicalMaterialMap[Index];
}

#if WITH_EDITORONLY_DATA
void UMaterialInstance::GetStaticParameterValues(FStaticParameterSet& OutStaticParameters)
{
	check(IsInGameThread());

	if ((AllowCachingStaticParameterValuesCounter > 0) && CachedStaticParameterValues.IsSet())
	{
		OutStaticParameters = CachedStaticParameterValues.GetValue();
		return;
	}

	if (Parent)
	{
		UMaterial* ParentMaterial = Parent->GetMaterial();
		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;

		// Static Material Layers Parameters
		GetAllMaterialLayersParameterInfo(OutParameterInfo, Guids);
		OutStaticParameters.MaterialLayersParameters.AddZeroed(OutParameterInfo.Num());

		for (int32 ParameterIdx = 0; ParameterIdx < OutParameterInfo.Num(); ParameterIdx++)
		{
			FStaticMaterialLayersParameter& ParentParameter = OutStaticParameters.MaterialLayersParameters[ParameterIdx];
			FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];
			FGuid ExpressionId = Guids[ParameterIdx];

			ParentParameter.bOverride = true;
			ParentParameter.ParameterInfo = ParameterInfo;

			Parent->GetMaterialLayersParameterValue(ParameterInfo, ParentParameter.Value, ExpressionId);
			ParentParameter.Value.LinkAllLayersToParent(); // Set parent guids for layers from parent material

			ParentParameter.ExpressionGUID = ExpressionId;
			// If the SourceInstance is overriding this parameter, use its settings
			for (const FStaticMaterialLayersParameter& LayersParam : StaticParameters.MaterialLayersParameters)
			{
				if (ParameterInfo == LayersParam.ParameterInfo)
				{
					ParentParameter.bOverride = LayersParam.bOverride;
					if (LayersParam.bOverride)
					{
						ParentParameter.Value = LayersParam.Value;
					}
				}
			}
		}

		// Static Switch Parameters
		GetAllStaticSwitchParameterInfo(OutParameterInfo, Guids);
		OutStaticParameters.StaticSwitchParameters.AddZeroed(OutParameterInfo.Num());

		for(int32 ParameterIdx=0; ParameterIdx<OutParameterInfo.Num(); ParameterIdx++)
		{
			FStaticSwitchParameter& ParentParameter = OutStaticParameters.StaticSwitchParameters[ParameterIdx];
			FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];
			FGuid ExpressionId = Guids[ParameterIdx];

			ParentParameter.bOverride = false;
			ParentParameter.ParameterInfo = ParameterInfo;

			GetStaticSwitchParameterValue(ParameterInfo, ParentParameter.Value, ExpressionId);

			ParentParameter.ExpressionGUID = Guids[ParameterIdx];

			// If the SourceInstance is overriding this parameter, use its settings
			for(int32 SwitchParamIdx = 0; SwitchParamIdx < StaticParameters.StaticSwitchParameters.Num(); SwitchParamIdx++)
			{
				const FStaticSwitchParameter& StaticSwitchParam = StaticParameters.StaticSwitchParameters[SwitchParamIdx];

				if(ParameterInfo == StaticSwitchParam.ParameterInfo)
				{
					ParentParameter.bOverride = StaticSwitchParam.bOverride;
					if (StaticSwitchParam.bOverride)
					{
						ParentParameter.Value = StaticSwitchParam.Value;
					}
				}
			}
		}

		// Static Component Mask Parameters
		GetAllStaticComponentMaskParameterInfo(OutParameterInfo, Guids);
		OutStaticParameters.StaticComponentMaskParameters.AddZeroed(OutParameterInfo.Num());

		for(int32 ParameterIdx=0; ParameterIdx<OutParameterInfo.Num(); ParameterIdx++)
		{
			FStaticComponentMaskParameter& ParentParameter = OutStaticParameters.StaticComponentMaskParameters[ParameterIdx];
			FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];
			FGuid ExpressionId = Guids[ParameterIdx];

			ParentParameter.bOverride = false;
			ParentParameter.ParameterInfo = ParameterInfo;
			
			GetStaticComponentMaskParameterValue(ParameterInfo, ParentParameter.R, ParentParameter.G, ParentParameter.B, ParentParameter.A, ExpressionId);

			ParentParameter.ExpressionGUID = Guids[ParameterIdx];
			
			// If the SourceInstance is overriding this parameter, use its settings
			for(int32 MaskParamIdx = 0; MaskParamIdx < StaticParameters.StaticComponentMaskParameters.Num(); MaskParamIdx++)
			{
				const FStaticComponentMaskParameter &StaticComponentMaskParam = StaticParameters.StaticComponentMaskParameters[MaskParamIdx];

				if(ParameterInfo == StaticComponentMaskParam.ParameterInfo)
				{
					ParentParameter.bOverride = StaticComponentMaskParam.bOverride;
					if (StaticComponentMaskParam.bOverride)
					{
						ParentParameter.R = StaticComponentMaskParam.R;
						ParentParameter.G = StaticComponentMaskParam.G;
						ParentParameter.B = StaticComponentMaskParam.B;
						ParentParameter.A = StaticComponentMaskParam.A;
					}
				}
			}
		}
	}

	// Custom parameters.
	CustomStaticParametersGetters.Broadcast(OutStaticParameters, this);

	if (AllowCachingStaticParameterValuesCounter > 0)
	{
		CachedStaticParameterValues = OutStaticParameters;
	}
}
#endif // WITH_EDITORONLY_DATA

void UMaterialInstance::GetAllParametersOfType(EMaterialParameterType Type, TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	const UMaterial* Material = GetMaterial_Concurrent();
	int32 NumParameters = CachedLayerParameters.GetNumParameters(Type);
	if (Material)
	{
		NumParameters += Material->GetCachedExpressionData().Parameters.GetNumParameters(Type);
	}

	OutParameterInfo.Empty(NumParameters);
	OutParameterIds.Empty(NumParameters);
	CachedLayerParameters.GetAllParameterInfoOfType(Type, false, OutParameterInfo, OutParameterIds);
	if (Material)
	{
		Material->GetCachedExpressionData().Parameters.GetAllGlobalParameterInfoOfType(Type, false, OutParameterInfo, OutParameterIds);
	}
}

void UMaterialInstance::GetAllScalarParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::Scalar, OutParameterInfo, OutParameterIds);
}

void UMaterialInstance::GetAllVectorParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::Vector, OutParameterInfo, OutParameterIds);
}

void UMaterialInstance::GetAllTextureParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::Texture, OutParameterInfo, OutParameterIds);
}

void UMaterialInstance::GetAllRuntimeVirtualTextureParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::RuntimeVirtualTexture, OutParameterInfo, OutParameterIds);
}

void UMaterialInstance::GetAllFontParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::Font, OutParameterInfo, OutParameterIds);
}

#if WITH_EDITORONLY_DATA
void UMaterialInstance::GetAllMaterialLayersParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	OutParameterInfo.Empty();
	OutParameterIds.Empty();
	if (const UMaterial* Material = GetMaterial())
	{
		Material->GetAllParameterInfo<UMaterialExpressionMaterialAttributeLayers>(OutParameterInfo, OutParameterIds);
	}
}

void UMaterialInstance::GetAllStaticSwitchParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::StaticSwitch, OutParameterInfo, OutParameterIds);
}

void UMaterialInstance::GetAllStaticComponentMaskParameterInfo(TArray<FMaterialParameterInfo>& OutParameterInfo, TArray<FGuid>& OutParameterIds) const
{
	GetAllParametersOfType(EMaterialParameterType::StaticComponentMask, OutParameterInfo, OutParameterIds);
}

bool UMaterialInstance::IterateDependentFunctions(TFunctionRef<bool(UMaterialFunctionInterface*)> Predicate) const
{
	// Important that local function references are listed first so that traversing for a parameter
	// value we always hit the highest material in the hierarchy that can give us a valid value
	for (const FStaticMaterialLayersParameter& LayersParam : StaticParameters.MaterialLayersParameters)
	{
		if (LayersParam.bOverride)
		{
			for (UMaterialFunctionInterface* Layer : LayersParam.Value.Layers)
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

			for (UMaterialFunctionInterface* Blend : LayersParam.Value.Blends)
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
		}
	}

	return Parent ? Parent->IterateDependentFunctions(Predicate) : true;
}

void UMaterialInstance::GetDependentFunctions(TArray<UMaterialFunctionInterface*>& DependentFunctions) const
{
	IterateDependentFunctions([&DependentFunctions](UMaterialFunctionInterface* MaterialFunction) -> bool
		{
			DependentFunctions.AddUnique(MaterialFunction);
			return true;
		});
}
#endif // WITH_EDITORONLY_DATA

bool UMaterialInstance::GetScalarParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, float& OutValue, bool bOveriddenOnly, bool bCheckOwnedGlobalOverrides) const
{
#if WITH_EDITOR
	if (GetReentrantFlag())
	{
		return false;
	}
#endif

	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FScalarParameterValue& ScalarParam : ScalarParameterValues)
		{
			if (ScalarParam.ParameterInfo == ParameterInfo)
			{
				OutValue = ScalarParam.ParameterValue;
				return true;
			}
		}
	}

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Scalar, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.ScalarValues[ParameterIndex];
			return true;
		}
	}

	if (Parent)
	{
#if WITH_EDITOR
		FMICReentranceGuard	Guard(this);
#endif
		return Parent->GetScalarParameterDefaultValue(ParameterInfo, OutValue, bOveriddenOnly, true);
	}

	return false;
}

bool UMaterialInstance::GetVectorParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue, bool bOveriddenOnly, bool bCheckOwnedGlobalOverrides) const
{
#if WITH_EDITOR
	if (GetReentrantFlag())
	{
		return false;
	}
#endif

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FVectorParameterValue& VectorParam : VectorParameterValues)
		{
			if (VectorParam.ParameterInfo == ParameterInfo)
			{
				OutValue = VectorParam.ParameterValue;
				return true;
			}
		}
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Vector, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.VectorValues[ParameterIndex];
			return true;
		}
	}

	if (Parent)
	{
#if WITH_EDITOR
		FMICReentranceGuard	Guard(this);
#endif
		return Parent->GetVectorParameterDefaultValue(ParameterInfo, OutValue, bOveriddenOnly, true);
	}

	return false;
}

bool UMaterialInstance::GetTextureParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, UTexture*& OutValue, bool bCheckOwnedGlobalOverrides) const
{
#if WITH_EDITOR
	if (GetReentrantFlag())
	{
		return false;
	}
#endif

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FTextureParameterValue& TextureParam : TextureParameterValues)
		{
			if (TextureParam.ParameterInfo == ParameterInfo)
			{
				OutValue = TextureParam.ParameterValue;
				return true;
			}
		}
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Texture, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.TextureValues[ParameterIndex];
			return true;
		}
	}

	if (Parent)
	{
#if WITH_EDITOR
		FMICReentranceGuard	Guard(this);
#endif
		return Parent->GetTextureParameterDefaultValue(ParameterInfo, OutValue, true);
	}

	return false;
}

bool UMaterialInstance::GetRuntimeVirtualTextureParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue, bool bCheckOwnedGlobalOverrides) const
{
#if WITH_EDITOR
	if (GetReentrantFlag())
	{
		return false;
	}
#endif

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FRuntimeVirtualTextureParameterValue& RuntimeVirtualTextureParam : RuntimeVirtualTextureParameterValues)
		{
			if (RuntimeVirtualTextureParam.ParameterInfo == ParameterInfo)
			{
				OutValue = RuntimeVirtualTextureParam.ParameterValue;
				return true;
			}
		}
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::RuntimeVirtualTexture, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutValue = CachedLayerParameters.RuntimeVirtualTextureValues[ParameterIndex];
			return true;
		}
	}

	if (Parent)
	{
#if WITH_EDITOR
		FMICReentranceGuard	Guard(this);
#endif
		return Parent->GetRuntimeVirtualTextureParameterDefaultValue(ParameterInfo, OutValue, true);
	}

	return false;
}

bool UMaterialInstance::GetFontParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, UFont*& OutFontValue, int32& OutFontPage, bool bCheckOwnedGlobalOverrides) const
{
#if WITH_EDITOR
	if (GetReentrantFlag())
	{
		return false;
	}
#endif

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FFontParameterValue& FontParam : FontParameterValues)
		{
			if (FontParam.ParameterInfo == ParameterInfo)
			{
				OutFontValue = FontParam.FontValue;
				OutFontPage = FontParam.FontPage;
				return true;
			}
		}
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::Font, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutFontValue = CachedLayerParameters.FontValues[ParameterIndex];
			OutFontPage = CachedLayerParameters.FontPageValues[ParameterIndex];
			return true;
		}
	}

	if (Parent)
	{
#if WITH_EDITOR
		FMICReentranceGuard	Guard(this);
#endif
		return Parent->GetFontParameterDefaultValue(ParameterInfo, OutFontValue, OutFontPage, true);
	}

	return false;
}

#if WITH_EDITOR
bool UMaterialInstance::GetStaticSwitchParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid, bool bCheckOwnedGlobalOverrides) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FStaticSwitchParameter& SwitchParam : StaticParameters.StaticSwitchParameters)
		{
			if (SwitchParam.bOverride && SwitchParam.ParameterInfo == ParameterInfo)
			{
				OutValue = SwitchParam.Value;
				OutExpressionGuid = SwitchParam.ExpressionGUID;
				return true;
			}
		}
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::StaticSwitch, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutExpressionGuid = CachedLayerParameters.GetExpressionGuid(EMaterialParameterType::StaticSwitch, ParameterIndex);
			OutValue = CachedLayerParameters.StaticSwitchValues[ParameterIndex];
			return true;
		}
	}
	
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetStaticSwitchParameterDefaultValue(ParameterInfo, OutValue, OutExpressionGuid, true);
	}

	return false;
}

bool UMaterialInstance:: GetStaticComponentMaskParameterDefaultValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid& OutExpressionGuid, bool bCheckOwnedGlobalOverrides) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	// In the case of duplicate parameters with different values, this will return the
	// first matching expression found, not necessarily the one that's used for rendering
	if (bCheckOwnedGlobalOverrides)
	{
		// Parameters overridden by this instance
		for (const FStaticComponentMaskParameter& ComponentMaskParam : StaticParameters.StaticComponentMaskParameters)
		{
			if (ComponentMaskParam.bOverride && ComponentMaskParam.ParameterInfo == ParameterInfo)
			{
				OutR = ComponentMaskParam.R;
				OutG = ComponentMaskParam.G;
				OutB = ComponentMaskParam.B;
				OutA = ComponentMaskParam.A;
				OutExpressionGuid = ComponentMaskParam.ExpressionGUID;
				return true;
			}
		}
	}

	if (ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::StaticComponentMask, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutExpressionGuid = CachedLayerParameters.GetExpressionGuid(EMaterialParameterType::StaticComponentMask, ParameterIndex);
			OutR = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].R;
			OutG = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].G;
			OutB = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].B;
			OutA = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].A;
			return true;
		}
	}
	
	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetStaticComponentMaskParameterDefaultValue(ParameterInfo, OutR, OutG, OutB, OutA, OutExpressionGuid, true);
	}

	return false;
}

bool UMaterialInstance::GetGroupName(const FHashedMaterialParameterInfo& ParameterInfo, FName& OutGroup) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	// @TODO: Alter to match sort priority behavior?
	for (const FStaticMaterialLayersParameter& Param : StaticParameters.MaterialLayersParameters)
	{
		if (Param.bOverride)
		{
			if (ParameterInfo.Association == EMaterialParameterAssociation::LayerParameter)
			{
				if(Param.Value.Layers.IsValidIndex(ParameterInfo.Index))
				{
					UMaterialFunctionInterface* Layer = Param.Value.Layers[ParameterInfo.Index];

					if (Layer && Layer->GetParameterGroupName(ParameterInfo, OutGroup))
					{
						return true;
					}
				}
			}
			else if (ParameterInfo.Association == EMaterialParameterAssociation::BlendParameter)
			{
				if(Param.Value.Blends.IsValidIndex(ParameterInfo.Index))
				{
					UMaterialFunctionInterface* Blend = Param.Value.Blends[ParameterInfo.Index];

					if (Blend && Blend->GetParameterGroupName(ParameterInfo, OutGroup))
					{
						return true;
					}
				}
			}
		}
	}

	if (Parent)
	{
		FMICReentranceGuard	Guard(this);
		Parent->GetGroupName(ParameterInfo, OutGroup);
	}

	return false;
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UMaterialInstance::ForceRecompileForRendering()
{
	CacheResourceShadersForRendering();
}
#endif // WITH_EDITOR

void UMaterialInstance::InitStaticPermutation(EMaterialShaderPrecompileMode PrecompileMode)
{
	UpdateOverridableBaseProperties();

	// Update bHasStaticPermutationResource in case the parent was not found
	bHasStaticPermutationResource = (!StaticParameters.IsEmpty() || HasOverridenBaseProperties()) && Parent;

	FMaterialResourceDeferredDeletionArray ResourcesToFree;

	if ( FApp::CanEverRender() ) 
	{
		// Cache shaders for the current platform to be used for rendering
		CacheResourceShadersForRendering(PrecompileMode, ResourcesToFree);
	}

	FMaterial::DeferredDeleteArray(ResourcesToFree);
}

void UMaterialInstance::UpdateOverridableBaseProperties()
{
	//Parents base property overrides have to be cached by now.
	//This should be done on PostLoad()
	//Or via an FMaterialUpdateContext when editing.

	if (!Parent)
	{
		OpacityMaskClipValue = 0.0f;
		BlendMode = BLEND_Opaque;
		ShadingModels = MSM_DefaultLit;
		TwoSided = 0;
		DitheredLODTransition = 0;
		bIsShadingModelFromMaterialExpression = 0;
		return;
	}

	if (BasePropertyOverrides.bOverride_OpacityMaskClipValue)
	{
		OpacityMaskClipValue = BasePropertyOverrides.OpacityMaskClipValue;
	}
	else
	{
		OpacityMaskClipValue = Parent->GetOpacityMaskClipValue();
		BasePropertyOverrides.OpacityMaskClipValue = OpacityMaskClipValue;
	}

	if ( BasePropertyOverrides.bOverride_CastDynamicShadowAsMasked )
	{
		bCastDynamicShadowAsMasked = BasePropertyOverrides.bCastDynamicShadowAsMasked;
	}
	else
	{
		bCastDynamicShadowAsMasked = Parent->GetCastDynamicShadowAsMasked();
		BasePropertyOverrides.bCastDynamicShadowAsMasked = bCastDynamicShadowAsMasked;
	}

	if (BasePropertyOverrides.bOverride_BlendMode)
	{
		BlendMode = BasePropertyOverrides.BlendMode;
	}
	else
	{
		BlendMode = Parent->GetBlendMode();
		BasePropertyOverrides.BlendMode = BlendMode;
	}

	if (BasePropertyOverrides.bOverride_ShadingModel)
	{
		if (BasePropertyOverrides.ShadingModel == MSM_FromMaterialExpression)
		{
			// Can't override using MSM_FromMaterialExpression, simply fall back to parent
			ShadingModels = Parent->GetShadingModels();
			bIsShadingModelFromMaterialExpression = Parent->IsShadingModelFromMaterialExpression();
		}
		else
		{
			// It's only possible to override using a single shading model
			ShadingModels = FMaterialShadingModelField(BasePropertyOverrides.ShadingModel);
			bIsShadingModelFromMaterialExpression = 0;
		}
	}
	else
	{
		ShadingModels = Parent->GetShadingModels();
		bIsShadingModelFromMaterialExpression = Parent->IsShadingModelFromMaterialExpression();

		if (bIsShadingModelFromMaterialExpression)
		{
			BasePropertyOverrides.ShadingModel = MSM_FromMaterialExpression; 
		}
		else
		{
			ensure(ShadingModels.CountShadingModels() == 1);
			BasePropertyOverrides.ShadingModel = ShadingModels.GetFirstShadingModel(); 
		}
	}

	if (BasePropertyOverrides.bOverride_TwoSided)
	{
		TwoSided = BasePropertyOverrides.TwoSided != 0;
	}
	else
	{
		TwoSided = Parent->IsTwoSided();
		BasePropertyOverrides.TwoSided = TwoSided;
	}

	if (BasePropertyOverrides.bOverride_DitheredLODTransition)
	{
		DitheredLODTransition = BasePropertyOverrides.DitheredLODTransition != 0;
	}
	else
	{
		DitheredLODTransition = Parent->IsDitheredLODTransition();
		BasePropertyOverrides.DitheredLODTransition = DitheredLODTransition;
	}
}

void UMaterialInstance::GetAllShaderMaps(TArray<FMaterialShaderMap*>& OutShaderMaps)
{
	for (FMaterialResource* CurrentResource : StaticPermutationMaterialResources)
	{
		FMaterialShaderMap* ShaderMap = CurrentResource->GetGameThreadShaderMap();
		OutShaderMaps.Add(ShaderMap);
	}
}

FMaterialResource* UMaterialInstance::AllocatePermutationResource()
{
	return new FMaterialResource();
}

void UMaterialInstance::CacheResourceShadersForRendering(EMaterialShaderPrecompileMode PrecompileMode, FMaterialResourceDeferredDeletionArray& OutResourcesToFree)
{
	check(IsInGameThread() || IsAsyncLoading());

	UpdateOverridableBaseProperties();

#if STORE_ONLY_ACTIVE_SHADERMAPS
	OutResourcesToFree = MoveTemp(StaticPermutationMaterialResources);
	StaticPermutationMaterialResources.Reset();
#endif // STORE_ONLY_ACTIVE_SHADERMAPS

	if (bHasStaticPermutationResource && FApp::CanEverRender())
	{
		check(IsA(UMaterialInstanceConstant::StaticClass()));
		UMaterial* BaseMaterial = GetMaterial();

		uint32 FeatureLevelsToCompile = GetFeatureLevelsToCompileForRendering();
		const EMaterialQualityLevel::Type ActiveQualityLevel = GetCachedScalabilityCVars().MaterialQualityLevel;

		TArray<FMaterialResource*> ResourcesToCache;
		while (FeatureLevelsToCompile != 0)
		{
			const ERHIFeatureLevel::Type FeatureLevel = (ERHIFeatureLevel::Type)FBitSet::GetAndClearNextBit(FeatureLevelsToCompile);
			const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[FeatureLevel];

			// Only cache shaders for the quality level that will actually be used to render
			// In cooked build, there is no shader compilation but this is still needed to
			// register the loaded shadermap
			FMaterialResource* CurrentResource = FindOrCreateMaterialResource(StaticPermutationMaterialResources, BaseMaterial, this, FeatureLevel, ActiveQualityLevel);
			check(CurrentResource);

#if STORE_ONLY_ACTIVE_SHADERMAPS
			if (!CurrentResource->GetGameThreadShaderMap())
			{
				// Load the shader map for this resource, if needed
				FMaterialResource Tmp;
				FName PackageFileName = GetOutermost()->FileName;
				UE_CLOG(PackageFileName.IsNone(), LogMaterial, Warning,
					TEXT("UMaterialInstance::CacheResourceShadersForRendering - Can't reload material resource '%s'. File system based reload is unsupported in this build."),
					*GetFullName());
				if (!PackageFileName.IsNone() && ReloadMaterialResource(&Tmp, PackageFileName.ToString(), OffsetToFirstResource, FeatureLevel, ActiveQualityLevel))
				{
					CurrentResource->SetInlineShaderMap(Tmp.GetGameThreadShaderMap());
					CurrentResource->UpdateInlineShaderMapIsComplete();
				}
			}
#endif // STORE_ONLY_ACTIVE_SHADERMAPS

			ResourcesToCache.Reset();
			ResourcesToCache.Add(CurrentResource);
			CacheShadersForResources(ShaderPlatform, ResourcesToCache, PrecompileMode);
		}
	}

	RecacheUniformExpressions(true);

	InitResources();
}

void UMaterialInstance::CacheResourceShadersForRendering(EMaterialShaderPrecompileMode PrecompileMode)
{
	FMaterialResourceDeferredDeletionArray ResourcesToFree;
	CacheResourceShadersForRendering(PrecompileMode, ResourcesToFree);
	FMaterial::DeferredDeleteArray(ResourcesToFree);
}

void UMaterialInstance::CacheResourceShadersForCooking(EShaderPlatform ShaderPlatform, TArray<FMaterialResource*>& OutCachedMaterialResources, EMaterialShaderPrecompileMode PrecompileMode, const ITargetPlatform* TargetPlatform)
{
	if (bHasStaticPermutationResource)
	{
		UMaterial* BaseMaterial = GetMaterial();

		TArray<bool, TInlineAllocator<EMaterialQualityLevel::Num> > QualityLevelsUsed;
		BaseMaterial->GetQualityLevelUsageForCooking(QualityLevelsUsed, ShaderPlatform);

		const UShaderPlatformQualitySettings* MaterialQualitySettings = UMaterialShaderQualitySettings::Get()->GetShaderPlatformQualitySettings(ShaderPlatform);
		bool bNeedDefaultQuality = false;

		ERHIFeatureLevel::Type TargetFeatureLevel = GetMaxSupportedFeatureLevel(ShaderPlatform);

		TArray<FMaterialResource*> NewResourcesToCache;	// only new resources need to have CacheShaders() called on them, whereas OutCachedMaterialResources may already contain resources for another shader platform
		for (int32 QualityLevelIndex = 0; QualityLevelIndex < EMaterialQualityLevel::Num; QualityLevelIndex++)
		{
			// Cache all quality levels actually used
			if (QualityLevelsUsed[QualityLevelIndex])
			{
				FMaterialResource* NewResource = AllocatePermutationResource();
				NewResource->SetMaterial(BaseMaterial, this, (ERHIFeatureLevel::Type)TargetFeatureLevel, (EMaterialQualityLevel::Type)QualityLevelIndex);
				NewResourcesToCache.Add(NewResource);
			}
			else
			{
				const FMaterialQualityOverrides& QualityOverrides = MaterialQualitySettings->GetQualityOverrides((EMaterialQualityLevel::Type)QualityLevelIndex);
				if (!QualityOverrides.bDiscardQualityDuringCook)
				{
					// don't have an explicit resource for this quality level, but still need to support it, so make sure we include a default quality resource
					bNeedDefaultQuality = true;
				}
			}
		}

		if (bNeedDefaultQuality)
		{
			FMaterialResource* NewResource = AllocatePermutationResource();
			NewResource->SetMaterial(BaseMaterial, this, (ERHIFeatureLevel::Type)TargetFeatureLevel);
			NewResourcesToCache.Add(NewResource);
		}

		CacheShadersForResources(ShaderPlatform, NewResourcesToCache, PrecompileMode, TargetPlatform);

		OutCachedMaterialResources.Append(NewResourcesToCache);
	}
}

void UMaterialInstance::CacheShadersForResources(EShaderPlatform ShaderPlatform, const TArray<FMaterialResource*>& ResourcesToCache, EMaterialShaderPrecompileMode PrecompileMode, const ITargetPlatform* TargetPlatform)
{
	UMaterial* BaseMaterial = GetMaterial();
#if WITH_EDITOR
	check(!HasAnyFlags(RF_NeedPostLoad));
	check(!BaseMaterial->HasAnyFlags(RF_NeedPostLoad));
#endif

#if WITH_EDITOR
	UpdateCachedLayerParameters();
#endif

	for (int32 ResourceIndex = 0; ResourceIndex < ResourcesToCache.Num(); ResourceIndex++)
	{
		FMaterialResource* CurrentResource = ResourcesToCache[ResourceIndex];

		const bool bSuccess = CurrentResource->CacheShaders(ShaderPlatform, PrecompileMode, TargetPlatform);

		if (!bSuccess)
		{
			UE_ASSET_LOG(LogMaterial, Warning, this,
				TEXT("Failed to compile Material Instance with Base %s for platform %s, Default Material will be used in game."), 
				BaseMaterial ? *BaseMaterial->GetName() : TEXT("Null"), 
				*LegacyShaderPlatformToShaderFormat(ShaderPlatform).ToString()
				);

#if WITH_EDITOR
			const TArray<FString>& CompileErrors = CurrentResource->GetCompileErrors();
			for (int32 ErrorIndex = 0; ErrorIndex < CompileErrors.Num(); ErrorIndex++)
			{
				UE_LOG(LogMaterial, Display, TEXT("	%s"), *CompileErrors[ErrorIndex]);
			}
#endif // WITH_EDITOR
		}
	}
}

#if WITH_EDITORONLY_DATA
bool UMaterialInstance::GetStaticSwitchParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutValue, FGuid& OutExpressionGuid, bool bOveriddenOnly, bool bCheckParent) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance override
	for (const FStaticSwitchParameter& Param : StaticParameters.StaticSwitchParameters)
	{
		if (Param.ParameterInfo == ParameterInfo)
		{
			OutExpressionGuid = Param.ExpressionGUID;
			OutValue = Param.Value;
			return true;
		}
	}

	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::StaticSwitch, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutExpressionGuid = CachedLayerParameters.GetExpressionGuid(EMaterialParameterType::StaticSwitch, ParameterIndex);
			OutValue = CachedLayerParameters.StaticSwitchValues[ParameterIndex];
			return true;
		}
	}

	// Next material in hierarchy
	if (Parent && bCheckParent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetStaticSwitchParameterValue(ParameterInfo, OutValue, OutExpressionGuid, bOveriddenOnly, true);
	}

	return false;
}

bool UMaterialInstance::GetStaticComponentMaskParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, bool& R, bool& G, bool& B, bool& A, FGuid& OutExpressionGuid, bool bOveriddenOnly, bool bCheckParent) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	// Instance override
	for (const FStaticComponentMaskParameter& Param : StaticParameters.StaticComponentMaskParameters)
	{
		if (Param.ParameterInfo == ParameterInfo)
		{
			OutExpressionGuid = Param.ExpressionGUID;
			R = Param.R;
			G = Param.G;
			B = Param.B;
			A = Param.A;
			return true;
		}
	}

	// Instance-included default
	if (!bOveriddenOnly && ParameterInfo.Association != EMaterialParameterAssociation::GlobalParameter)
	{
		const int32 ParameterIndex = CachedLayerParameters.FindParameterIndex(EMaterialParameterType::StaticComponentMask, ParameterInfo);
		if (ParameterIndex != INDEX_NONE)
		{
			OutExpressionGuid = CachedLayerParameters.GetExpressionGuid(EMaterialParameterType::StaticComponentMask, ParameterIndex);
			R = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].R;
			G = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].G;
			B = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].B;
			A = CachedLayerParameters.StaticComponentMaskValues[ParameterIndex].A;
			return true;
		}
	}

	// Next material in hierarchy
	if (Parent && bCheckParent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetStaticComponentMaskParameterValue(ParameterInfo, R, G, B, A, OutExpressionGuid, bOveriddenOnly, true);
	}

	return false;
}

bool UMaterialInstance::GetMaterialLayersParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, FMaterialLayersFunctions& OutLayers, FGuid& OutExpressionGuid, bool bCheckParent /*= true*/) const
{
	if (GetReentrantFlag())
	{
		return false;
	}

	for (const FStaticMaterialLayersParameter& Param : StaticParameters.MaterialLayersParameters)
	{
		if (Param.bOverride && Param.ParameterInfo == ParameterInfo)
		{
			OutLayers = Param.Value;
			OutExpressionGuid = Param.ExpressionGUID;
			return true;
		}
	}

	if (Parent && bCheckParent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetMaterialLayersParameterValue(ParameterInfo, OutLayers, OutExpressionGuid);
	}
	else
	{
		return false;
	}
}
#endif // WITH_EDITORONLY_DATA

bool UMaterialInstance::GetTerrainLayerWeightParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutWeightmapIndex, FGuid &OutExpressionGuid) const
{
	if(GetReentrantFlag())
	{
		return false;
	}

	for (int32 ValueIndex = 0;ValueIndex < StaticParameters.TerrainLayerWeightParameters.Num();ValueIndex++)
	{
		const FStaticTerrainLayerWeightParameter& Param = StaticParameters.TerrainLayerWeightParameters[ValueIndex];
		if (Param.bOverride && Param.ParameterInfo == ParameterInfo)
		{
			OutWeightmapIndex = Param.WeightmapIndex;
			OutExpressionGuid = Param.ExpressionGUID;
			return true;
		}
	}

	if(Parent)
	{
		FMICReentranceGuard	Guard(this);
		return Parent->GetTerrainLayerWeightParameterValue(ParameterInfo, OutWeightmapIndex, OutExpressionGuid);
	}
	else
	{
		return false;
	}
}

bool UMaterialInstance::UpdateMaterialLayersParameterValue(const FHashedMaterialParameterInfo& ParameterInfo, const FMaterialLayersFunctions& LayersValue, const bool bOverridden, const FGuid& GUID)
{
	for (int32 ValueIndex = 0;ValueIndex < StaticParameters.MaterialLayersParameters.Num();ValueIndex++)
	{
		FStaticMaterialLayersParameter& Param = StaticParameters.MaterialLayersParameters[ValueIndex];
		if (Param.ParameterInfo == ParameterInfo)
		{
			if (Param.Value != LayersValue || Param.bOverride != bOverridden)
			{
				// @TODO: This should properly respect the override state
				Param.Value = LayersValue;
				Param.bOverride = true;//bOverridden;
				return true;
			}
#if WITH_EDITOR
			for(int32 LayerNameIndex = 0; LayerNameIndex < LayersValue.LayerNames.Num(); LayerNameIndex++) 
			{
				if (LayersValue.LayerNames[LayerNameIndex].ToString() != Param.Value.LayerNames[LayerNameIndex].ToString())
				{
					Param.Value = LayersValue;
					Param.bOverride = true;//bOverridden;
					return true;
				}
			}
#endif 			
			break;
		}
	}

	return false;
}

template <typename ParameterType>
void TrimToOverriddenOnly(TArray<ParameterType>& Parameters)
{
	for (int32 ParameterIndex = Parameters.Num() - 1; ParameterIndex >= 0; ParameterIndex--)
	{
		if (!Parameters[ParameterIndex].bOverride)
		{
			Parameters.RemoveAt(ParameterIndex);
		}
	}
}

#if WITH_EDITOR

void UMaterialInstance::BeginCacheForCookedPlatformData( const ITargetPlatform *TargetPlatform )
{
	TArray<FMaterialResource*> *CachedMaterialResourcesForPlatform = CachedMaterialResourcesForCooking.Find( TargetPlatform );

	if ( CachedMaterialResourcesForPlatform == NULL )
	{
		check( CachedMaterialResourcesForPlatform == NULL );

		CachedMaterialResourcesForCooking.Add( TargetPlatform );
		CachedMaterialResourcesForPlatform = CachedMaterialResourcesForCooking.Find( TargetPlatform );

		check( CachedMaterialResourcesForPlatform != NULL );

		TArray<FName> DesiredShaderFormats;
		TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

		// Cache shaders for each shader format, storing the results in CachedMaterialResourcesForCooking so they will be available during saving
		for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
		{
			const EShaderPlatform TargetShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);

			CacheResourceShadersForCooking(TargetShaderPlatform, *CachedMaterialResourcesForPlatform, EMaterialShaderPrecompileMode::Background, TargetPlatform);
		}
	}
}

bool UMaterialInstance::IsCachedCookedPlatformDataLoaded( const ITargetPlatform* TargetPlatform ) 
{
	const TArray<FMaterialResource*> *CachedMaterialResourcesForPlatform = CachedMaterialResourcesForCooking.Find( TargetPlatform );
	if ( CachedMaterialResourcesForPlatform != NULL )
	{
		for ( const auto& MaterialResource : *CachedMaterialResourcesForPlatform )
		{
			if ( MaterialResource->IsCompilationFinished() == false )
				return false;
		}

		return true;
	}
	return false; // this happens if we haven't started caching (begincache hasn't been called yet)
}


void UMaterialInstance::ClearCachedCookedPlatformData( const ITargetPlatform *TargetPlatform )
{
	// Make sure that all CacheShaders render thead commands are finished before we destroy FMaterialResources.
	// TODO - is this needed, since we're using DeferredDeleteArray now?
	FlushRenderingCommands();

	TArray<FMaterialResource*> *CachedMaterialResourcesForPlatform = CachedMaterialResourcesForCooking.Find( TargetPlatform );
	if ( CachedMaterialResourcesForPlatform != nullptr )
	{
		FMaterial::DeferredDeleteArray(*CachedMaterialResourcesForPlatform);
	}
	CachedMaterialResourcesForCooking.Remove( TargetPlatform );
}


void UMaterialInstance::ClearAllCachedCookedPlatformData()
{
	// Make sure that all CacheShaders render thead commands are finished before we destroy FMaterialResources.
	// TODO - is this needed, since we're using DeferredDeleteArray now?
	FlushRenderingCommands();

	for ( auto& It : CachedMaterialResourcesForCooking )
	{
		TArray<FMaterialResource*> &CachedMaterialResourcesForPlatform = It.Value;
		FMaterial::DeferredDeleteArray(CachedMaterialResourcesForPlatform);
	}

	CachedMaterialResourcesForCooking.Empty();
}

#endif

void UMaterialInstance::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);
	SCOPED_LOADTIMER(MaterialInstanceSerializeTime);
	SCOPE_CYCLE_COUNTER(STAT_MaterialInstance_Serialize);

	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Super::Serialize(Ar);
		
#if WITH_EDITOR
	if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MaterialAttributeLayerParameters)
	{
		// Material attribute layers parameter refactor fix-up
		for (FScalarParameterValue& Parameter : ScalarParameterValues)
		{
			Parameter.ParameterInfo.Name = Parameter.ParameterName_DEPRECATED;
		}
		for (FVectorParameterValue& Parameter : VectorParameterValues)
		{
			Parameter.ParameterInfo.Name = Parameter.ParameterName_DEPRECATED;
		}
		for (FTextureParameterValue& Parameter : TextureParameterValues)
		{
			Parameter.ParameterInfo.Name = Parameter.ParameterName_DEPRECATED;
		}
		for (FFontParameterValue& Parameter : FontParameterValues)
		{
			Parameter.ParameterInfo.Name = Parameter.ParameterName_DEPRECATED;
		}
	}
#endif // WITH_EDITOR

	// Only serialize the static permutation resource if one exists
	if (bHasStaticPermutationResource)
	{
		if (Ar.UE4Ver() >= VER_UE4_PURGED_FMATERIAL_COMPILE_OUTPUTS)
		{
			if (Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MaterialAttributeLayerParameters)
			{
				StaticParameters.Serialize(Ar);
			}

#if WITH_EDITOR
			static_assert(!STORE_ONLY_ACTIVE_SHADERMAPS, "Only discard unused SMs in cooked build");
			SerializeInlineShaderMaps(&CachedMaterialResourcesForCooking, Ar, LoadedMaterialResources);
#else
			SerializeInlineShaderMaps(
				NULL,
				Ar,
				LoadedMaterialResources
#if STORE_ONLY_ACTIVE_SHADERMAPS
				, &OffsetToFirstResource
#endif
			);
#endif
		}
#if WITH_EDITOR
		else
		{
			const bool bLoadedByCookedMaterial = FPlatformProperties::RequiresCookedData() || GetOutermost()->bIsCookedForEditor;

			FMaterialResource LegacyResource;
			LegacyResource.LegacySerialize(Ar);

			FMaterialShaderMapId LegacyId;
			LegacyId.Serialize(Ar, bLoadedByCookedMaterial);

			StaticParameters.StaticSwitchParameters = LegacyId.GetStaticSwitchParameters();
			StaticParameters.StaticComponentMaskParameters = LegacyId.GetStaticComponentMaskParameters();
			StaticParameters.TerrainLayerWeightParameters = LegacyId.GetTerrainLayerWeightParameters();

			TrimToOverriddenOnly(StaticParameters.StaticSwitchParameters);
			TrimToOverriddenOnly(StaticParameters.StaticComponentMaskParameters);
			TrimToOverriddenOnly(StaticParameters.TerrainLayerWeightParameters);
		}
#endif // WITH_EDITOR
	}

	if (Ar.UE4Ver() >= VER_UE4_MATERIAL_INSTANCE_BASE_PROPERTY_OVERRIDES )
	{
#if WITH_EDITORONLY_DATA
		if( Ar.UE4Ver() < VER_UE4_FIX_MATERIAL_PROPERTY_OVERRIDE_SERIALIZE )
		{
			// awful old native serialize of FMaterialInstanceBasePropertyOverrides UStruct
			Ar << bOverrideBaseProperties_DEPRECATED;
			bool bHasPropertyOverrides = false;
			Ar << bHasPropertyOverrides;
			if( bHasPropertyOverrides )
			{
				FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.bOverride_OpacityMaskClipValue);
				Ar << BasePropertyOverrides.OpacityMaskClipValue;

				if( Ar.UE4Ver() >= VER_UE4_MATERIAL_INSTANCE_BASE_PROPERTY_OVERRIDES_PHASE_2 )
				{
					FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.bOverride_BlendMode);
					Ar << BasePropertyOverrides.BlendMode;
					FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.bOverride_ShadingModel);
					Ar << BasePropertyOverrides.ShadingModel;
					FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.bOverride_TwoSided);
					FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.TwoSided);

					if( Ar.UE4Ver() >= VER_UE4_MATERIAL_INSTANCE_BASE_PROPERTY_OVERRIDES_DITHERED_LOD_TRANSITION )
					{
						FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.bOverride_DitheredLODTransition);
						FArchive_Serialize_BitfieldBool(Ar, BasePropertyOverrides.DitheredLODTransition);
					}
					// unrelated but closest change to bug
					if( Ar.UE4Ver() < VER_UE4_STATIC_SHADOW_DEPTH_MAPS )
					{
						// switched enum order
						switch( BasePropertyOverrides.ShadingModel )
						{
							case MSM_Unlit:			BasePropertyOverrides.ShadingModel = MSM_DefaultLit; break;
							case MSM_DefaultLit:	BasePropertyOverrides.ShadingModel = MSM_Unlit; break;
						}
					}
				}
			}
		}
#endif
	}
#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsCooking() && Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && FShaderLibraryCooker::NeedsShaderStableKeys(EShaderPlatform::SP_NumPlatforms))
	{
		SaveShaderStableKeys(Ar.CookingTarget());
	}
#endif

	if (Ar.IsSaving() && Ar.IsCooking())
	{
		ValidateTextureOverrides(GMaxRHIFeatureLevel);
	}
}

void UMaterialInstance::PostLoad()
{
	LLM_SCOPE(ELLMTag::MaterialInstance);
	SCOPED_LOADTIMER(MaterialInstancePostLoad);

	Super::PostLoad();

#if WITH_EDITOR
	//recalculate any scalar params based on a curve position in an atlas in case the atlas changed
	for (FScalarParameterValue& ScalarParam : ScalarParameterValues)
	{
		if (ScalarParam.AtlasData.bIsUsedAsAtlasPosition)
		{
			UCurveLinearColorAtlas* Atlas = Cast<UCurveLinearColorAtlas>(ScalarParam.AtlasData.Atlas.Get());
			UCurveLinearColor* Curve = Cast<UCurveLinearColor>(ScalarParam.AtlasData.Curve.Get());
			if (Curve && Atlas)
			{
				Curve->ConditionalPostLoad();
				Atlas->ConditionalPostLoad();
				int32 Index = Atlas->GradientCurves.Find(Curve);
				if (Index != INDEX_NONE)
				{
					ScalarParam.ParameterValue = (float)Index;
				}
			}
		}
	}
#endif // WITH_EDITOR

	if (FApp::CanEverRender())
	{
		// Resources can be processed / registered now that we're back on the main thread
		ProcessSerializedInlineShaderMaps(this, LoadedMaterialResources, StaticPermutationMaterialResources);
	}
	else
	{
		// Discard all loaded material resources
		for (FMaterialResource& LoadedResource : LoadedMaterialResources)
		{
			LoadedResource.DiscardShaderMap();
		}
	}
	// Empty the list of loaded resources, we don't need it anymore
	LoadedMaterialResources.Empty();

	AssertDefaultMaterialsPostLoaded();

	// Ensure that the instance's parent is PostLoaded before the instance.
	if (Parent)
	{
		if (GEventDrivenLoaderEnabled && EVENT_DRIVEN_ASYNC_LOAD_ACTIVE_AT_RUNTIME)
		{
			check(!Parent->HasAnyFlags(RF_NeedLoad));
		}
		Parent->ConditionalPostLoad();
	}

	// Add references to the expression object if we do not have one already, and fix up any names that were changed.
	UpdateParameters();

	// We have to make sure the resources are created for all used textures.
	for( int32 ValueIndex=0; ValueIndex<TextureParameterValues.Num(); ValueIndex++ )
	{
		// Make sure the texture is postloaded so the resource isn't null.
		UTexture* Texture = TextureParameterValues[ValueIndex].ParameterValue;
		if( Texture )
		{
			Texture->ConditionalPostLoad();
		}
	}

	// do the same for runtime virtual textures
	for (int32 ValueIndex = 0; ValueIndex < RuntimeVirtualTextureParameterValues.Num(); ValueIndex++)
	{
		// Make sure the texture is postloaded so the resource isn't null.
		URuntimeVirtualTexture* Value = RuntimeVirtualTextureParameterValues[ValueIndex].ParameterValue;
		if (Value)
		{
			Value->ConditionalPostLoad();
		}
	}

	// do the same for font textures
	for( int32 ValueIndex=0; ValueIndex < FontParameterValues.Num(); ValueIndex++ )
	{
		// Make sure the font is postloaded so the resource isn't null.
		UFont* Font = FontParameterValues[ValueIndex].FontValue;
		if( Font )
		{
			Font->ConditionalPostLoad();
		}
	}

	// And any material layers parameter's functions
	for (FStaticMaterialLayersParameter& LayersParam : StaticParameters.MaterialLayersParameters)
	{
		TArray<UMaterialFunctionInterface*> Dependencies;
		Dependencies.Append(LayersParam.Value.Layers);
		Dependencies.Append(LayersParam.Value.Blends);

		for (UMaterialFunctionInterface* Dependency : Dependencies)
		{
			if (Dependency)
			{
				Dependency->ConditionalPostLoad();
			}
		}
	}

#if WITH_EDITOR
	UpdateCachedLayerParameters();
#endif

	// called before we cache the uniform expression as a call to SubsurfaceProfileRT affects the dta in there
	PropagateDataToMaterialProxy();

	STAT(double MaterialLoadTime = 0);
	{
		SCOPE_SECONDS_COUNTER(MaterialLoadTime);

		// Make sure static parameters are up to date and shaders are cached for the current platform
		InitStaticPermutation();
#if WITH_EDITOR && 1
		// enable caching in postload for derived data cache commandlet and cook by the book
		ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
		if (TPM && (TPM->RestrictFormatsToRuntimeOnly() == false)) 
		{
			TArray<ITargetPlatform*> Platforms = TPM->GetActiveTargetPlatforms();
			// Cache for all the shader formats that the cooking target requires
			for (int32 FormatIndex = 0; FormatIndex < Platforms.Num(); FormatIndex++)
			{
				BeginCacheForCookedPlatformData(Platforms[FormatIndex]);
			}
		}
#endif
	}

	INC_FLOAT_STAT_BY(STAT_ShaderCompiling_MaterialLoading,(float)MaterialLoadTime);

	if (GIsEditor && GEngine != NULL && !IsTemplate() && Parent)
	{
		// Ensure that the ReferencedTextureGuids array is up to date.
		UpdateLightmassTextureTracking();
	}

	// Fixup for legacy instances which didn't recreate the lighting guid properly on duplication
	if (GetLinker() && GetLinker()->UE4Ver() < VER_UE4_BUMPED_MATERIAL_EXPORT_GUIDS)
	{
		extern TMap<FGuid, UMaterialInterface*> LightingGuidFixupMap;
		UMaterialInterface** ExistingMaterial = LightingGuidFixupMap.Find(GetLightingGuid());

		if (ExistingMaterial)
		{
			SetLightingGuid();
		}

		LightingGuidFixupMap.Add(GetLightingGuid(), this);
	}
	//DumpDebugInfo();
}

void UMaterialInstance::BeginDestroy()
{
	TArray<TRefCountPtr<FMaterialResource>> ResourcesToDestroy;
	for (FMaterialResource* CurrentResource : StaticPermutationMaterialResources)
	{
		CurrentResource->SetOwnerBeginDestroyed();
		if (CurrentResource->PrepareDestroy_GameThread())
		{
			ResourcesToDestroy.Add(CurrentResource);
		}
	}

	Super::BeginDestroy();

	if (Resource || ResourcesToDestroy.Num() > 0)
	{
		ReleasedByRT = false;

		FMaterialRenderProxy* LocalResource = Resource;
		FThreadSafeBool* Released = &ReleasedByRT;
		ENQUEUE_RENDER_COMMAND(BeginDestroyCommand)(
		[ResourcesToDestroy = MoveTemp(ResourcesToDestroy), LocalResource, Released](FRHICommandListImmediate& RHICmdList)
		{
			if (LocalResource)
			{
				LocalResource->MarkForGarbageCollection();
				LocalResource->ReleaseResource();
			}

			for (FMaterialResource* CurrentResource : ResourcesToDestroy)
			{
				CurrentResource->PrepareDestroy_RenderThread();
			}

			*Released = true;
		});		
	}
}

bool UMaterialInstance::IsReadyForFinishDestroy()
{
	bool bIsReady = Super::IsReadyForFinishDestroy();

	return bIsReady && ReleasedByRT;
}

void UMaterialInstance::FinishDestroy()
{
	if(!HasAnyFlags(RF_ClassDefaultObject))
	{
		Resource->GameThread_Destroy();
		Resource = nullptr;
	}

	for (FMaterialResource* CurrentResource : StaticPermutationMaterialResources)
	{
		delete CurrentResource;
	}
	StaticPermutationMaterialResources.Empty();
#if WITH_EDITOR
	if (!GExitPurge)
	{
		ClearAllCachedCookedPlatformData();
	}
#endif
	Super::FinishDestroy();
}

void UMaterialInstance::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UMaterialInstance* This = CastChecked<UMaterialInstance>(InThis);

	if (This->bHasStaticPermutationResource)
	{
		for (FMaterialResource* CurrentResource : This->StaticPermutationMaterialResources)
		{
			CurrentResource->AddReferencedObjects(Collector);
		}
	}

	Super::AddReferencedObjects(This, Collector);
}

void UMaterialInstance::SetParentInternal(UMaterialInterface* NewParent, bool RecacheShaders)
{
	if (!Parent || Parent != NewParent)
	{
		// Check if the new parent is already an existing child
		UMaterialInstance* ParentAsMaterialInstance = Cast<UMaterialInstance>(NewParent);
		bool bSetParent = false;

		if (ParentAsMaterialInstance != nullptr && ParentAsMaterialInstance->IsChildOf(this))
		{
			UE_LOG(LogMaterial, Warning, TEXT("%s is not a valid parent for %s as it is already a child of this material instance."),
				   *NewParent->GetFullName(),
				   *GetFullName());
		}
		else if (NewParent &&
				 !NewParent->IsA(UMaterial::StaticClass()) &&
				 !NewParent->IsA(UMaterialInstanceConstant::StaticClass()))
		{
			UE_LOG(LogMaterial, Warning, TEXT("%s is not a valid parent for %s. Only Materials and MaterialInstanceConstants are valid parents for a material instance. Outer is %s"),
				*NewParent->GetFullName(),
				*GetFullName(),
				*GetNameSafe(GetOuter()));
		}
		else
		{
			Parent = NewParent;
			bSetParent = true;

			if( Parent )
			{
				// It is possible to set a material's parent while post-loading. In
				// such a case it is also possible that the parent has not been
				// post-loaded, so call ConditionalPostLoad() just in case.
				Parent->ConditionalPostLoad();
			}
		}

		if (bSetParent && RecacheShaders)
		{
			// delete all the existing resources that may have previous parent as the owner
			if (StaticPermutationMaterialResources.Num() > 0)
			{
				FMaterialResourceDeferredDeletionArray ResourcesToFree;
				ResourcesToFree = MoveTemp(StaticPermutationMaterialResources);
				FMaterial::DeferredDeleteArray(ResourcesToFree);
				StaticPermutationMaterialResources.Reset();
			}
			InitStaticPermutation();
		}
		else
		{
			InitResources();
		}
#if WITH_EDITOR
		UpdateCachedLayerParameters();
#endif
	}
}

bool UMaterialInstance::SetVectorParameterByIndexInternal(int32 ParameterIndex, FLinearColor Value)
{
	FVectorParameterValue* ParameterValue = GameThread_FindParameterByIndex(VectorParameterValues, ParameterIndex);
	if (ParameterValue == nullptr)
	{
		return false;
	}

	if(ParameterValue->ParameterValue != Value)
	{
		ParameterValue->ParameterValue = Value;
		// Update the material instance data in the rendering thread.
		GameThread_UpdateMIParameter(this, *ParameterValue);
	}

	return true;
}

void UMaterialInstance::SetVectorParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, FLinearColor Value)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);

	FVectorParameterValue* ParameterValue = GameThread_FindParameterByName(VectorParameterValues, ParameterInfo);

	bool bForceUpdate = false;
	if(!ParameterValue)
	{
		// If there's no element for the named parameter in array yet, add one.
		ParameterValue = new(VectorParameterValues) FVectorParameterValue;
		ParameterValue->ParameterInfo = ParameterInfo;
		ParameterValue->ExpressionGUID.Invalidate();
		bForceUpdate = true;
	}

	// Don't enqueue an update if it isn't needed
	if (bForceUpdate || ParameterValue->ParameterValue != Value)
	{
		ParameterValue->ParameterValue = Value;
		// Update the material instance data in the rendering thread.
		GameThread_UpdateMIParameter(this, *ParameterValue);
	}
}

bool UMaterialInstance::SetScalarParameterByIndexInternal(int32 ParameterIndex, float Value)
{
	FScalarParameterValue* ParameterValue = GameThread_FindParameterByIndex(ScalarParameterValues, ParameterIndex);
	if (ParameterValue == nullptr)
	{
		return false;
	}
	
	if(ParameterValue->ParameterValue != Value)
	{
		ParameterValue->ParameterValue = Value;
		// Update the material instance data in the rendering thread.
		GameThread_UpdateMIParameter(this, *ParameterValue);
	}

	return true;
}

void UMaterialInstance::SetScalarParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, float Value)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);

	FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(ScalarParameterValues, ParameterInfo);

	bool bForceUpdate = false;
	if(!ParameterValue)
	{
		// If there's no element for the named parameter in array yet, add one.
		ParameterValue = new(ScalarParameterValues) FScalarParameterValue;
		ParameterValue->ParameterInfo = ParameterInfo;
		ParameterValue->ExpressionGUID.Invalidate();
		bForceUpdate = true;
	}

	// Don't enqueue an update if it isn't needed
	if (bForceUpdate || ParameterValue->ParameterValue != Value)
	{
		ParameterValue->ParameterValue = Value;
		// Update the material instance data in the rendering thread.
		GameThread_UpdateMIParameter(this, *ParameterValue);
	}
}

#if WITH_EDITOR
void UMaterialInstance::SetScalarParameterAtlasInternal(const FMaterialParameterInfo& ParameterInfo, FScalarParameterAtlasInstanceData AtlasData)
{
	FScalarParameterValue* ParameterValue = GameThread_FindParameterByName(ScalarParameterValues, ParameterInfo);

	if (ParameterValue)
	{
		ParameterValue->AtlasData = AtlasData;
		UCurveLinearColorAtlas* Atlas = Cast<UCurveLinearColorAtlas>(AtlasData.Atlas.Get());
		UCurveLinearColor* Curve = Cast<UCurveLinearColor>(AtlasData.Curve.Get());
		if (!Atlas || !Curve)
		{
			return;
		}
		int32 Index = Atlas->GradientCurves.Find(Curve);
		if (Index == INDEX_NONE)
		{
			return;
		}

		float NewValue = (float)Index;
		
		// Don't enqueue an update if it isn't needed
		if (ParameterValue->ParameterValue != NewValue)
		{
			ParameterValue->ParameterValue = NewValue;
			// Update the material instance data in the rendering thread.
			GameThread_UpdateMIParameter(this, *ParameterValue);
		}
	}
}
#endif

void UMaterialInstance::SetTextureParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, UTexture* Value)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);

	FTextureParameterValue* ParameterValue = GameThread_FindParameterByName(TextureParameterValues, ParameterInfo);

	bool bForceUpdate = false;
	if(!ParameterValue)
	{
		// If there's no element for the named parameter in array yet, add one.
		ParameterValue = new(TextureParameterValues) FTextureParameterValue;
		ParameterValue->ParameterInfo = ParameterInfo;
		ParameterValue->ExpressionGUID.Invalidate();
		bForceUpdate = true;
	}

	// Don't enqueue an update if it isn't needed
	if (bForceUpdate || ParameterValue->ParameterValue != Value)
	{
		// set as an ensure, because it is somehow possible to accidentally pass non-textures into here via blueprints...
		if (Value && ensureMsgf(Value->IsA(UTexture::StaticClass()), TEXT("Expecting a UTexture! Value='%s' class='%s'"), *Value->GetName(), *Value->GetClass()->GetName()))
		{
			ParameterValue->ParameterValue = Value;
			// Update the material instance data in the rendering thread.
			GameThread_UpdateMIParameter(this, *ParameterValue);
		}		
	}
}

void UMaterialInstance::SetRuntimeVirtualTextureParameterValueInternal(const FMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture* Value)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);

	FRuntimeVirtualTextureParameterValue* ParameterValue = GameThread_FindParameterByName(RuntimeVirtualTextureParameterValues, ParameterInfo);

	bool bForceUpdate = false;
	if (!ParameterValue)
	{
		// If there's no element for the named parameter in array yet, add one.
		ParameterValue = new(RuntimeVirtualTextureParameterValues) FRuntimeVirtualTextureParameterValue;
		ParameterValue->ParameterInfo = ParameterInfo;
		ParameterValue->ExpressionGUID.Invalidate();
		bForceUpdate = true;
	}

	// Don't enqueue an update if it isn't needed
	if (bForceUpdate || ParameterValue->ParameterValue != Value)
	{
		// set as an ensure, because it is somehow possible to accidentally pass non-textures into here via blueprints...
		if (Value && ensureMsgf(Value->IsA(URuntimeVirtualTexture::StaticClass()), TEXT("Expecting a URuntimeVirtualTexture! Value='%s' class='%s'"), *Value->GetName(), *Value->GetClass()->GetName()))
		{
			ParameterValue->ParameterValue = Value;
			// Update the material instance data in the rendering thread.
			GameThread_UpdateMIParameter(this, *ParameterValue);
		}
	}
}

void UMaterialInstance::SetFontParameterValueInternal(const FMaterialParameterInfo& ParameterInfo,class UFont* FontValue,int32 FontPage)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);

	FFontParameterValue* ParameterValue = GameThread_FindParameterByName(FontParameterValues, ParameterInfo);

	bool bForceUpdate = false;
	if(!ParameterValue)
	{
			// If there's no element for the named parameter in array yet, add one.
			ParameterValue = new(FontParameterValues) FFontParameterValue;
			ParameterValue->ParameterInfo = ParameterInfo;
			ParameterValue->ExpressionGUID.Invalidate();
			bForceUpdate = true;
	}

	// Don't enqueue an update if it isn't needed
	if (bForceUpdate ||
		ParameterValue->FontValue != FontValue ||
		ParameterValue->FontPage != FontPage)
	{
		ParameterValue->FontValue = FontValue;
		ParameterValue->FontPage = FontPage;
		// Update the material instance data in the rendering thread.
		GameThread_UpdateMIParameter(this, *ParameterValue);
	}
}

void UMaterialInstance::ClearParameterValuesInternal(const bool bAllParameters)
{
	ScalarParameterValues.Empty();
	VectorParameterValues.Empty();

	if(bAllParameters)
	{
		TextureParameterValues.Empty();
		RuntimeVirtualTextureParameterValues.Empty();
		FontParameterValues.Empty();
	}

	if (Resource)
	{
		FMaterialInstanceResource* InResource = Resource;
		ENQUEUE_RENDER_COMMAND(FClearMIParametersCommand)(
			[InResource](FRHICommandList& RHICmdList)
			{
				InResource->RenderThread_ClearParameters();
			});
	}

	InitResources();
}

#if WITH_EDITOR
void UMaterialInstance::UpdateStaticPermutation(const FStaticParameterSet& NewParameters, FMaterialInstanceBasePropertyOverrides& NewBasePropertyOverrides, const bool bForceStaticPermutationUpdate /*= false*/, FMaterialUpdateContext* MaterialUpdateContext)
{
	FStaticParameterSet CompareParameters = NewParameters;

	TrimToOverriddenOnly(CompareParameters.StaticSwitchParameters);
	TrimToOverriddenOnly(CompareParameters.StaticComponentMaskParameters);
	TrimToOverriddenOnly(CompareParameters.TerrainLayerWeightParameters);
	TrimToOverriddenOnly(CompareParameters.MaterialLayersParameters);

	const bool bParamsHaveChanged = StaticParameters != CompareParameters;
	const bool bBasePropertyOverridesHaveChanged = BasePropertyOverrides != NewBasePropertyOverrides;

	BasePropertyOverrides = NewBasePropertyOverrides;

	//Ensure our cached base property overrides are up to date.
	UpdateOverridableBaseProperties();

	const bool bHasBasePropertyOverrides = HasOverridenBaseProperties();

	const bool bWantsStaticPermutationResource = Parent && (!CompareParameters.IsEmpty() || bHasBasePropertyOverrides);

	if (bHasStaticPermutationResource != bWantsStaticPermutationResource || bParamsHaveChanged || (bBasePropertyOverridesHaveChanged && bWantsStaticPermutationResource) || bForceStaticPermutationUpdate)
	{
		// This will flush the rendering thread which is necessary before changing bHasStaticPermutationResource, since the RT is reading from that directly
		FlushRenderingCommands();

		bHasStaticPermutationResource = bWantsStaticPermutationResource;
		StaticParameters = CompareParameters;

		UpdateCachedLayerParameters();
		CacheResourceShadersForRendering(EMaterialShaderPrecompileMode::None);
		RecacheUniformExpressions(true);

		if (MaterialUpdateContext != nullptr)
		{
			MaterialUpdateContext->AddMaterialInstance(this);
		}
		else
		{
			// The update context will make sure any dependent MI's with static parameters get recompiled
			FMaterialUpdateContext LocalMaterialUpdateContext(FMaterialUpdateContext::EOptions::RecreateRenderStates);
			LocalMaterialUpdateContext.AddMaterialInstance(this);
		}
	}
}

void UMaterialInstance::GetReferencedTexturesAndOverrides(TSet<const UTexture*>& InOutTextures) const
{
	for (UObject* UsedObject : CachedReferencedTextures)
	{
		if (const UTexture* UsedTexture = Cast<UTexture>(UsedObject))
		{
			InOutTextures.Add(UsedTexture);
		}
	}

	// Loop on all override parameters, since child MICs might not override some parameters of parent MICs.
	const UMaterialInstance* MaterialInstance = this;
	while (MaterialInstance)
	{
		for (const FTextureParameterValue& TextureParam : TextureParameterValues)
		{
			if (TextureParam.ParameterValue)
			{
				InOutTextures.Add(TextureParam.ParameterValue);
			}
		}
		MaterialInstance = Cast<UMaterialInstance>(MaterialInstance->Parent);
	}
}

// From MaterialCachedData.cpp
extern void FMaterialCachedParameters_UpdateForLayerParameters(FMaterialCachedParameters& Parameters, const FMaterialCachedExpressionContext& Context, UMaterialInstance* ParentMaterialInstance, const FStaticMaterialLayersParameter& LayerParameters);

void UMaterialInstance::UpdateCachedLayerParameters()
{
	if (!GIsClient)
	{
		// Expressions may not be loaded on server, so only rebuild the data if we're the client
		return;
	}
	UMaterialInstance* ParentInstance = nullptr;
	FMaterialCachedExpressionData CachedExpressionData;
	CachedExpressionData.Reset();
	if (Parent)
	{
		CachedExpressionData.ReferencedTextures = Parent->GetReferencedTextures();
		ParentInstance = Cast<UMaterialInstance>(Parent);
	}
	
	bool bCachedDataValid = true;
	for (FStaticMaterialLayersParameter& LayerParameters : StaticParameters.MaterialLayersParameters)
	{
		FMaterialCachedExpressionContext Context;
		if (ParentInstance)
		{
			FMaterialCachedParameters_UpdateForLayerParameters(CachedExpressionData.Parameters, Context, ParentInstance, LayerParameters);
		}

		if (!CachedExpressionData.UpdateForLayerFunctions(Context, LayerParameters.Value))
		{
			bCachedDataValid = false;
		}
	}

	if (bCachedDataValid)
	{
		CachedLayerParameters = MoveTemp(CachedExpressionData.Parameters);
		CachedReferencedTextures = MoveTemp(CachedExpressionData.ReferencedTextures);
	}
}

void UMaterialInstance::UpdateStaticPermutation(const FStaticParameterSet& NewParameters, FMaterialUpdateContext* MaterialUpdateContext)
{
	UpdateStaticPermutation(NewParameters, BasePropertyOverrides, false, MaterialUpdateContext);
}

void UMaterialInstance::UpdateStaticPermutation(FMaterialUpdateContext* MaterialUpdateContext)
{
	UpdateStaticPermutation(StaticParameters, MaterialUpdateContext);
}

void UMaterialInstance::UpdateParameterNames()
{
	bool bDirty = UpdateParameters();

	// Atleast 1 parameter changed, initialize parameters
	if (bDirty)
	{
		InitResources();
	}
}

#endif // WITH_EDITOR

void UMaterialInstance::RecacheUniformExpressions(bool bRecreateUniformBuffer) const
{	
	CacheMaterialInstanceUniformExpressions(this, bRecreateUniformBuffer);
}

#if WITH_EDITOR
void UMaterialInstance::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Ensure that the ReferencedTextureGuids array is up to date.
	if (GIsEditor)
	{
		UpdateLightmassTextureTracking();
	}

	PropagateDataToMaterialProxy();

	InitResources();

	// Force UpdateStaticPermutation when change type is Redirected as this probably means a Material or MaterialInstance parent asset was deleted.
	const bool bForceStaticPermutationUpdate = PropertyChangedEvent.ChangeType == EPropertyChangeType::Redirected;
	UpdateStaticPermutation(StaticParameters, BasePropertyOverrides, bForceStaticPermutationUpdate);

	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet || PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayClear || PropertyChangedEvent.ChangeType == EPropertyChangeType::ArrayRemove || PropertyChangedEvent.ChangeType == EPropertyChangeType::Unspecified || PropertyChangedEvent.ChangeType == EPropertyChangeType::Duplicate)
	{
		RecacheMaterialInstanceUniformExpressions(this, false);
	}

	UpdateCachedLayerParameters();

	if (GIsEditor)
	{
		// Brute force all flush virtual textures if this material writes to any runtime virtual texture.
		const UMaterial* BaseMaterial = GetMaterial();
		if (BaseMaterial != nullptr && BaseMaterial->GetCachedExpressionData().bHasRuntimeVirtualTextureOutput)
		{
			ENQUEUE_RENDER_COMMAND(FlushVTCommand)([ResourcePtr = Resource](FRHICommandListImmediate& RHICmdList) 
			{
				GetRendererModule().FlushVirtualTextureCache();	
			});
		}
	}
}

#endif // WITH_EDITOR


bool UMaterialInstance::UpdateLightmassTextureTracking()
{
	bool bTexturesHaveChanged = false;
#if WITH_EDITOR
	TArray<UTexture*> UsedTextures;
	
	GetUsedTextures(UsedTextures, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);
	if (UsedTextures.Num() != ReferencedTextureGuids.Num())
	{
		bTexturesHaveChanged = true;
		// Just clear out all the guids and the code below will
		// fill them back in...
		ReferencedTextureGuids.Empty(UsedTextures.Num());
		ReferencedTextureGuids.AddZeroed(UsedTextures.Num());
	}
	
	for (int32 CheckIdx = 0; CheckIdx < UsedTextures.Num(); CheckIdx++)
	{
		UTexture* Texture = UsedTextures[CheckIdx];
		if (Texture)
		{
			if (ReferencedTextureGuids[CheckIdx] != Texture->GetLightingGuid())
			{
				ReferencedTextureGuids[CheckIdx] = Texture->GetLightingGuid();
				bTexturesHaveChanged = true;
			}
		}
		else
		{
			if (ReferencedTextureGuids[CheckIdx] != FGuid(0,0,0,0))
			{
				ReferencedTextureGuids[CheckIdx] = FGuid(0,0,0,0);
				bTexturesHaveChanged = true;
			}
		}
	}
#endif // WITH_EDITOR

	return bTexturesHaveChanged;
}


bool UMaterialInstance::GetCastShadowAsMasked() const
{
	if (LightmassSettings.bOverrideCastShadowAsMasked)
	{
		return LightmassSettings.bCastShadowAsMasked;
	}

	if (Parent)
	{
		return Parent->GetCastShadowAsMasked();
	}

	return false;
}

float UMaterialInstance::GetEmissiveBoost() const
{
	if (LightmassSettings.bOverrideEmissiveBoost)
	{
		return LightmassSettings.EmissiveBoost;
	}

	if (Parent)
	{
		return Parent->GetEmissiveBoost();
	}

	return 1.0f;
}

float UMaterialInstance::GetDiffuseBoost() const
{
	if (LightmassSettings.bOverrideDiffuseBoost)
	{
		return LightmassSettings.DiffuseBoost;
	}

	if (Parent)
	{
		return Parent->GetDiffuseBoost();
	}

	return 1.0f;
}

float UMaterialInstance::GetExportResolutionScale() const
{
	if (LightmassSettings.bOverrideExportResolutionScale)
	{
		return FMath::Clamp(LightmassSettings.ExportResolutionScale, .1f, 10.0f);
	}

	if (Parent)
	{
		return FMath::Clamp(Parent->GetExportResolutionScale(), .1f, 10.0f);
	}

	return 1.0f;
}

#if WITH_EDITOR
bool UMaterialInstance::GetParameterDesc(const FHashedMaterialParameterInfo& ParameterInfo, FString& OutDesc, const TArray<struct FStaticMaterialLayersParameter>*) const
{
	const UMaterial* BaseMaterial = GetMaterial();
	if (BaseMaterial && BaseMaterial->GetParameterDesc(ParameterInfo, OutDesc, &StaticParameters.MaterialLayersParameters))
	{
		return true;
	}

	return false;
}

bool UMaterialInstance::GetParameterSortPriority(const FHashedMaterialParameterInfo& ParameterInfo, int32& OutSortPriority, const TArray<struct FStaticMaterialLayersParameter>*) const
{
	const UMaterial* BaseMaterial = GetMaterial();
	if (BaseMaterial && BaseMaterial->GetParameterSortPriority(ParameterInfo, OutSortPriority, &StaticParameters.MaterialLayersParameters))
	{
		return true;
	}

	return false;
}

bool UMaterialInstance::GetGroupSortPriority(const FString& InGroupName, int32& OutSortPriority) const
{
	// @TODO: This needs to handle overridden functions, layers and blends
	const UMaterial* BaseMaterial = GetMaterial();
	if (BaseMaterial && BaseMaterial->GetGroupSortPriority(InGroupName, OutSortPriority))
	{
		return true;
	}

	return false;
}

bool UMaterialInstance::GetTexturesInPropertyChain(EMaterialProperty InProperty, TArray<UTexture*>& OutTextures,  
	TArray<FName>* OutTextureParamNames, struct FStaticParameterSet* InStaticParameterSet,
	ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type InQuality)
{
	if (Parent != NULL)
	{
		TArray<FName> LocalTextureParamNames;
		bool bResult = Parent->GetTexturesInPropertyChain(InProperty, OutTextures, &LocalTextureParamNames, InStaticParameterSet, InFeatureLevel, InQuality);
		if (LocalTextureParamNames.Num() > 0)
		{
			// Check textures set in parameters as well...
			for (int32 TPIdx = 0; TPIdx < LocalTextureParamNames.Num(); TPIdx++)
			{
				UTexture* ParamTexture = NULL;
				if (GetTextureParameterValue(LocalTextureParamNames[TPIdx], ParamTexture) == true)
				{
					if (ParamTexture != NULL)
					{
						OutTextures.AddUnique(ParamTexture);
					}
				}

				if (OutTextureParamNames != NULL)
				{
					OutTextureParamNames->AddUnique(LocalTextureParamNames[TPIdx]);
				}
			}
		}
		return bResult;
	}
	return false;
}
#endif // WITH_EDITOR

void UMaterialInstance::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (bHasStaticPermutationResource)
	{
		for (FMaterialResource* CurrentResource : StaticPermutationMaterialResources)
		{
			CurrentResource->GetResourceSizeEx(CumulativeResourceSize);
		}
	}

	if (Resource)
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(FMaterialInstanceResource));
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ScalarParameterValues.Num() * sizeof(FMaterialInstanceResource::TNamedParameter<float>));
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(VectorParameterValues.Num() * sizeof(FMaterialInstanceResource::TNamedParameter<FLinearColor>));
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(TextureParameterValues.Num() * sizeof(FMaterialInstanceResource::TNamedParameter<const UTexture*>));
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(RuntimeVirtualTextureParameterValues.Num() * sizeof(FMaterialInstanceResource::TNamedParameter<const URuntimeVirtualTexture*>));
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(FontParameterValues.Num() * sizeof(FMaterialInstanceResource::TNamedParameter<const UTexture*>));
	}
}

FPostProcessMaterialNode* IteratePostProcessMaterialNodes(const FFinalPostProcessSettings& Dest, const UMaterial* Material, FBlendableEntry*& Iterator)
{
	EBlendableLocation Location = Material->BlendableLocation;
	int32 Priority = Material->BlendablePriority;

	for (;;)
	{
		FPostProcessMaterialNode* DataPtr = Dest.BlendableManager.IterateBlendables<FPostProcessMaterialNode>(Iterator);

		if (!DataPtr)
		{
			// end reached
			return 0;
		}

		// Do not consider materials that are set as not blendable
		if (!DataPtr->GetIsBlendable())
		{
			return 0;
		}

		if(DataPtr->GetLocation() == Location && DataPtr->GetPriority() == Priority && DataPtr->GetMaterialInterface()->GetMaterial() == Material)
		{
			return DataPtr;
		}
	}
}

void UMaterialInstance::AllMaterialsCacheResourceShadersForRendering(bool bUpdateProgressDialog)
{
#if STORE_ONLY_ACTIVE_SHADERMAPS
	TArray<UMaterialInstance*> MaterialInstances;
	for (TObjectIterator<UMaterialInstance> It; It; ++It)
	{
		MaterialInstances.Add(*It);
	}
	MaterialInstances.Sort([](const UMaterialInstance& A, const UMaterialInstance& B) { return A.OffsetToFirstResource < B.OffsetToFirstResource; });
	for (UMaterialInstance* MaterialInstance : MaterialInstances)
	{
		MaterialInstance->CacheResourceShadersForRendering();
		FThreadHeartBeat::Get().HeartBeat();
	}
#else
#if WITH_EDITOR
	FScopedSlowTask SlowTask(100.f, NSLOCTEXT("Engine", "CacheMaterialInstanceShadersMessage", "Caching material instance shaders"), true);
	if (bUpdateProgressDialog)
	{
		SlowTask.Visibility = ESlowTaskVisibility::ForceVisible;
		SlowTask.MakeDialog();
	}
#endif // WITH_EDITOR

	TArray<UObject*> MaterialInstanceArray;
	GetObjectsOfClass(UMaterialInstance::StaticClass(), MaterialInstanceArray, true, RF_ClassDefaultObject, EInternalObjectFlags::None);
	float TaskIncrement = (float)100.0f / MaterialInstanceArray.Num();

	for (UObject* MaterialInstanceObj : MaterialInstanceArray)
	{
		UMaterialInstance* MaterialInstance = (UMaterialInstance*)MaterialInstanceObj;

		MaterialInstance->CacheResourceShadersForRendering();

#if WITH_EDITOR
		if (bUpdateProgressDialog)
		{
			SlowTask.EnterProgressFrame(TaskIncrement);
		}
#endif // WITH_EDITOR
	}
#endif // STORE_ONLY_ACTIVE_SHADERMAPS
}


bool UMaterialInstance::IsChildOf(const UMaterialInterface* ParentMaterialInterface) const
{
	const UMaterialInterface* Material = this;

	while (Material != ParentMaterialInterface && Material != nullptr)
	{
		const UMaterialInstance* MaterialInstance = Cast<const UMaterialInstance>(Material);
		Material = (MaterialInstance != nullptr) ? MaterialInstance->Parent : nullptr;
	}

	return (Material != nullptr);
}


/**
	Properties of the base material. Can now be overridden by instances.
*/

void UMaterialInstance::GetBasePropertyOverridesHash(FSHAHash& OutHash)const
{
	check(IsInGameThread());

	const UMaterial* Mat = GetMaterial();
	check(Mat);

	FSHA1 Hash;
	bool bHasOverrides = false;

	float UsedOpacityMaskClipValue = GetOpacityMaskClipValue();
	if (FMath::Abs(UsedOpacityMaskClipValue - Mat->GetOpacityMaskClipValue()) > SMALL_NUMBER)
	{
		const FString HashString = TEXT("bOverride_OpacityMaskClipValue");
		Hash.UpdateWithString(*HashString, HashString.Len());
		Hash.Update((const uint8*)&UsedOpacityMaskClipValue, sizeof(UsedOpacityMaskClipValue));
		bHasOverrides = true;
	}

	bool bUsedCastDynamicShadowAsMasked = GetCastDynamicShadowAsMasked();
	if ( bUsedCastDynamicShadowAsMasked != Mat->GetCastDynamicShadowAsMasked() )
	{
		const FString HashString = TEXT("bOverride_CastDynamicShadowAsMasked");
		Hash.UpdateWithString(*HashString, HashString.Len());
		Hash.Update((const uint8*)&bUsedCastDynamicShadowAsMasked, sizeof(bUsedCastDynamicShadowAsMasked));
		bHasOverrides = true;
	}

	EBlendMode UsedBlendMode = GetBlendMode();
	if (UsedBlendMode != Mat->GetBlendMode())
	{
		const FString HashString = TEXT("bOverride_BlendMode");
		Hash.UpdateWithString(*HashString, HashString.Len());
		Hash.Update((const uint8*)&UsedBlendMode, sizeof(UsedBlendMode));
		bHasOverrides = true;
	}
	
	FMaterialShadingModelField UsedShadingModels = GetShadingModels();
	if (UsedShadingModels != Mat->GetShadingModels())
	{
		const FString HashString = TEXT("bOverride_ShadingModel");
		Hash.UpdateWithString(*HashString, HashString.Len());
		Hash.Update((const uint8*)&UsedShadingModels, sizeof(UsedShadingModels));
		bHasOverrides = true;
	}

	bool bUsedIsTwoSided = IsTwoSided();
	if (bUsedIsTwoSided != Mat->IsTwoSided())
	{
		const FString HashString = TEXT("bOverride_TwoSided");
		Hash.UpdateWithString(*HashString, HashString.Len());
		Hash.Update((uint8*)&bUsedIsTwoSided, sizeof(bUsedIsTwoSided));
		bHasOverrides = true;
	}
	bool bUsedIsDitheredLODTransition = IsDitheredLODTransition();
	if (bUsedIsDitheredLODTransition != Mat->IsDitheredLODTransition())
	{
		const FString HashString = TEXT("bOverride_DitheredLODTransition");
		Hash.UpdateWithString(*HashString, HashString.Len());
		Hash.Update((uint8*)&bUsedIsDitheredLODTransition, sizeof(bUsedIsDitheredLODTransition));
		bHasOverrides = true;
	}

	if (bHasOverrides)
	{
		Hash.Final();
		Hash.GetHash(&OutHash.Hash[0]);
	}
}

bool UMaterialInstance::HasOverridenBaseProperties()const
{
	const UMaterial* Material = GetMaterial_Concurrent();
	if (Parent && Material && Material->bUsedAsSpecialEngineMaterial == false &&
		((FMath::Abs(GetOpacityMaskClipValue() - Parent->GetOpacityMaskClipValue()) > SMALL_NUMBER) ||
		(GetBlendMode() != Parent->GetBlendMode()) ||
		(GetShadingModels() != Parent->GetShadingModels()) ||
		(IsTwoSided() != Parent->IsTwoSided()) ||
		(IsDitheredLODTransition() != Parent->IsDitheredLODTransition()) ||
		(GetCastDynamicShadowAsMasked() != Parent->GetCastDynamicShadowAsMasked())
		))
	{
		return true;
	}

	return false;
}

float UMaterialInstance::GetOpacityMaskClipValue() const
{
	return OpacityMaskClipValue;
}

EBlendMode UMaterialInstance::GetBlendMode() const
{
	return BlendMode;
}

FMaterialShadingModelField UMaterialInstance::GetShadingModels() const
{
	return ShadingModels;
}

bool UMaterialInstance::IsShadingModelFromMaterialExpression() const
{
	return bIsShadingModelFromMaterialExpression;
}

bool UMaterialInstance::IsTwoSided() const
{
	return TwoSided;
}

bool UMaterialInstance::IsDitheredLODTransition() const
{
	return DitheredLODTransition;
}

bool UMaterialInstance::IsMasked() const
{
	return GetBlendMode() == EBlendMode::BLEND_Masked;
}

USubsurfaceProfile* UMaterialInstance::GetSubsurfaceProfile_Internal() const
{
	checkSlow(IsInGameThread());
	if (bOverrideSubsurfaceProfile)
	{
		return SubsurfaceProfile;
	}

	// go up the chain if possible
	return Parent ? Parent->GetSubsurfaceProfile_Internal() : 0;
}

bool UMaterialInstance::CastsRayTracedShadows() const
{
	//#dxr_todo: do per material instance override?
	return Parent ? Parent->CastsRayTracedShadows() : true;
}

/** Checks to see if an input property should be active, based on the state of the material */
bool UMaterialInstance::IsPropertyActive(EMaterialProperty InProperty) const
{
	const UMaterial* Material = GetMaterial();
	return Material ? Material->IsPropertyActiveInDerived(InProperty, this) : false;
}

#if WITH_EDITOR
int32 UMaterialInstance::CompilePropertyEx( class FMaterialCompiler* Compiler, const FGuid& AttributeID )
{
	return Parent ? Parent->CompilePropertyEx(Compiler, AttributeID) : INDEX_NONE;
}
#endif // WITH_EDITOR

const FStaticParameterSet& UMaterialInstance::GetStaticParameters() const
{
	return StaticParameters;
}

void UMaterialInstance::GetLightingGuidChain(bool bIncludeTextures, TArray<FGuid>& OutGuids) const
{
#if WITH_EDITOR
	if (bIncludeTextures)
	{
		OutGuids.Append(ReferencedTextureGuids);
	}
	if (Parent)
	{
		Parent->GetLightingGuidChain(bIncludeTextures, OutGuids);
	}
	Super::GetLightingGuidChain(bIncludeTextures, OutGuids);
#endif
}

void UMaterialInstance::PreSave(const class ITargetPlatform* TargetPlatform)
{
	// @TODO : Remove any duplicate data from parent? Aims at improving change propagation (if controlled by parent)
	Super::PreSave(TargetPlatform);
}

float UMaterialInstance::GetTextureDensity(FName TextureName, const struct FMeshUVChannelInfo& UVChannelData) const
{
	ensure(UVChannelData.bInitialized);

	const float Density = Super::GetTextureDensity(TextureName, UVChannelData);
	
	// If it is not handled by this instance, try the parent
	if (!Density && Parent)
	{
		return Parent->GetTextureDensity(TextureName, UVChannelData);
	}
	return Density;
}

bool UMaterialInstance::Equivalent(const UMaterialInstance* CompareTo) const
{
	if (Parent != CompareTo->Parent || 
		PhysMaterial != CompareTo->PhysMaterial ||
		bOverrideSubsurfaceProfile != CompareTo->bOverrideSubsurfaceProfile ||
		BasePropertyOverrides != CompareTo->BasePropertyOverrides
		)
	{
		return false;
	}

	if (!CompareValueArraysByExpressionGUID(TextureParameterValues, CompareTo->TextureParameterValues))
	{
		return false;
	}
	if (!CompareValueArraysByExpressionGUID(ScalarParameterValues, CompareTo->ScalarParameterValues))
	{
		return false;
	}
	if (!CompareValueArraysByExpressionGUID(VectorParameterValues, CompareTo->VectorParameterValues))
	{
		return false;
	}
	if (!CompareValueArraysByExpressionGUID(RuntimeVirtualTextureParameterValues, CompareTo->RuntimeVirtualTextureParameterValues))
	{
		return false;
	}
	if (!CompareValueArraysByExpressionGUID(FontParameterValues, CompareTo->FontParameterValues))
	{
		return false;
	}

	if (!StaticParameters.Equivalent(CompareTo->StaticParameters))
	{
		return false;
	}
	return true;
}

#if !UE_BUILD_SHIPPING

static void FindRedundantMICS(const TArray<FString>& Args)
{
	TArray<UObject*> MICs;
	GetObjectsOfClass(UMaterialInstance::StaticClass(), MICs);

	int32 NumRedundant = 0;
	for (int32 OuterIndex = 0; OuterIndex < MICs.Num(); OuterIndex++)
	{
		for (int32 InnerIndex = OuterIndex + 1; InnerIndex < MICs.Num(); InnerIndex++)
		{
			if (((UMaterialInstance*)MICs[OuterIndex])->Equivalent((UMaterialInstance*)MICs[InnerIndex]))
			{
				NumRedundant++;
				break;
			}
		}
	}
	UE_LOG(LogConsoleResponse, Display, TEXT("----------------------------- %d UMaterialInstance's %d redundant "), MICs.Num(), NumRedundant);
}

static FAutoConsoleCommand FindRedundantMICSCmd(
	TEXT("FindRedundantMICS"),
	TEXT("Looks at all loaded MICs and looks for redundant ones."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FindRedundantMICS)
);

#endif

void UMaterialInstance::DumpDebugInfo() const
{
	UE_LOG(LogConsoleResponse, Display, TEXT("----------------------------- %s"), *GetFullName());

	UE_LOG(LogConsoleResponse, Display, TEXT("  Parent %s"), Parent ? *Parent->GetFullName() : TEXT("null"));

	if (Parent)
	{
		const UMaterial* Base = GetMaterial();
		UE_LOG(LogConsoleResponse, Display, TEXT("  Base %s"), Base ? *Base->GetFullName() : TEXT("null"));

		if (Base)
		{
			static const UEnum* Enum = StaticEnum<EMaterialDomain>();
			check(Enum);
			UE_LOG(LogConsoleResponse, Display, TEXT("  MaterialDomain %s"), *Enum->GetNameStringByValue(int64(Base->MaterialDomain)));
		}
		if (bHasStaticPermutationResource)
		{
			for (FMaterialResource* CurrentResource : StaticPermutationMaterialResources)
			{
				CurrentResource->DumpDebugInfo();
			}
		}
		else
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("    This MIC does not have static permulations, and is therefore is just a version of the parent."));
		}
	}
}

void UMaterialInstance::SaveShaderStableKeys(const class ITargetPlatform* TP)
{
#if WITH_EDITOR
	FStableShaderKeyAndValue SaveKeyVal;
	SetCompactFullNameFromObject(SaveKeyVal.ClassNameAndObjectPath, this);
	UMaterial* Base = GetMaterial();
	if (Base)
	{
		SaveKeyVal.MaterialDomain = FName(*MaterialDomainString(Base->MaterialDomain));
	}
	SaveShaderStableKeysInner(TP, SaveKeyVal);
#endif
}

void UMaterialInstance::SaveShaderStableKeysInner(const class ITargetPlatform* TP, const FStableShaderKeyAndValue& InSaveKeyVal)
{
#if WITH_EDITOR
	if (bHasStaticPermutationResource)
	{
		FStableShaderKeyAndValue SaveKeyVal(InSaveKeyVal);
		TArray<FMaterialResource*>* MatRes = CachedMaterialResourcesForCooking.Find(TP);
		if (MatRes)
		{
			for (FMaterialResource* Mat : *MatRes)
			{
				if (Mat)
				{
					Mat->SaveShaderStableKeys(EShaderPlatform::SP_NumPlatforms, SaveKeyVal);
				}
			}
		}
	}
	else if (Parent)
	{
		Parent->SaveShaderStableKeysInner(TP, InSaveKeyVal);
	}
#endif
}

#if WITH_EDITOR
void UMaterialInstance::BeginAllowCachingStaticParameterValues()
{
	++AllowCachingStaticParameterValuesCounter;
}

void UMaterialInstance::EndAllowCachingStaticParameterValues()
{
	check(AllowCachingStaticParameterValuesCounter > 0);
	--AllowCachingStaticParameterValuesCounter;
	if (AllowCachingStaticParameterValuesCounter == 0)
	{
		CachedStaticParameterValues.Reset();
	}
}
#endif // WITH_EDITOR

void UMaterialInstance::CopyMaterialUniformParametersInternal(UMaterialInterface* Source)
{
	LLM_SCOPE(ELLMTag::MaterialInstance);
	SCOPE_CYCLE_COUNTER(STAT_MaterialInstance_CopyUniformParamsInternal)

	if ((Source == nullptr) || (Source == this))
	{
		return;
	}

	ClearParameterValuesInternal();

	if (!FPlatformProperties::IsServerOnly())
	{
		// Build the chain as we don't know which level in the hierarchy will override which parameter
		TArray<UMaterialInterface*> Hierarchy;
		UMaterialInterface* NextSource = Source;
		while (NextSource)
		{
			Hierarchy.Add(NextSource);
			if (UMaterialInstance* AsInstance = Cast<UMaterialInstance>(NextSource))
			{
				NextSource = AsInstance->Parent;
			}
			else
			{
				NextSource = nullptr;
			}
		}

		// Walk chain from material base overriding discovered values. Worst case
		// here is a long instance chain with every value overridden on every level
		for (int Index = Hierarchy.Num() - 1; Index >= 0; --Index)
		{
			UMaterialInterface* Interface = Hierarchy[Index];

			// For instances override existing data
			if (UMaterialInstance* AsInstance = Cast<UMaterialInstance>(Interface))
			{
				// Scalars
				for (FScalarParameterValue& Parameter : AsInstance->ScalarParameterValues)
				{
					// If the parameter already exists, override it
					bool bExisting = false;
					for (FScalarParameterValue& ExistingParameter : ScalarParameterValues)
					{
						if (ExistingParameter.ParameterInfo.Name == Parameter.ParameterInfo.Name)
						{
							ExistingParameter.ParameterValue = Parameter.ParameterValue;
							bExisting = true;
							break;
						}
					}

					// Instance has introduced a new parameter via static param set
					if (!bExisting)
					{
						ScalarParameterValues.Add(Parameter);
					}
				}

				// Vectors
				for (FVectorParameterValue& Parameter : AsInstance->VectorParameterValues)
				{
					// If the parameter already exists, override it
					bool bExisting = false;
					for (FVectorParameterValue& ExistingParameter : VectorParameterValues)
					{
						if (ExistingParameter.ParameterInfo.Name == Parameter.ParameterInfo.Name)
						{
							ExistingParameter.ParameterValue = Parameter.ParameterValue;
							bExisting = true;
							break;
						}
					}

					// Instance has introduced a new parameter via static param set
					if (!bExisting)
					{
						VectorParameterValues.Add(Parameter);
					}
				}

				// Textures
				for (FTextureParameterValue& Parameter : AsInstance->TextureParameterValues)
				{
					// If the parameter already exists, override it
					bool bExisting = false;
					for (FTextureParameterValue& ExistingParameter : TextureParameterValues)
					{
						if (ExistingParameter.ParameterInfo.Name == Parameter.ParameterInfo.Name)
						{
							ExistingParameter.ParameterValue = Parameter.ParameterValue;
							bExisting = true;
							break;
						}
					}

					// Instance has introduced a new parameter via static param set
					if (!bExisting)
					{
						TextureParameterValues.Add(Parameter);
					}
				}

				// Runtime Virtual Textures
				for (FRuntimeVirtualTextureParameterValue& Parameter : AsInstance->RuntimeVirtualTextureParameterValues)
				{
					// If the parameter already exists, override it
					bool bExisting = false;
					for (FRuntimeVirtualTextureParameterValue& ExistingParameter : RuntimeVirtualTextureParameterValues)
					{
						if (ExistingParameter.ParameterInfo.Name == Parameter.ParameterInfo.Name)
						{
							ExistingParameter.ParameterValue = Parameter.ParameterValue;
							bExisting = true;
							break;
						}
					}

					// Instance has introduced a new parameter via static param set
					if (!bExisting)
					{
						RuntimeVirtualTextureParameterValues.Add(Parameter);
					}
				}
			}
			else if (UMaterial* AsMaterial = Cast<UMaterial>(Interface))
			{
				// Material should be the base and only append new parameters
				checkSlow(ScalarParameterValues.Num() == 0);
				checkSlow(VectorParameterValues.Num() == 0);
				checkSlow(TextureParameterValues.Num() == 0);
				checkSlow(RuntimeVirtualTextureParameterValues.Num() == 0);

				const FMaterialResource* MaterialResource = nullptr;
				if (UWorld* World = AsMaterial->GetWorld())
				{
					MaterialResource = AsMaterial->GetMaterialResource(World->FeatureLevel);
				}

				if (!MaterialResource)
				{
					MaterialResource = AsMaterial->GetMaterialResource(GMaxRHIFeatureLevel);
				}

				if (MaterialResource)
				{
					// Scalars
					TArrayView<const FMaterialScalarParameterInfo> ScalarExpressions = MaterialResource->GetUniformScalarParameterExpressions();
					for (const FMaterialScalarParameterInfo& Parameter : ScalarExpressions)
					{
						FScalarParameterValue* ParameterValue = new(ScalarParameterValues) FScalarParameterValue;
						ParameterValue->ParameterInfo.Name = Parameter.ParameterInfo.GetName();
						Parameter.GetDefaultValue(ParameterValue->ParameterValue);
					}

					// Vectors
					TArrayView<const FMaterialVectorParameterInfo> VectorExpressions = MaterialResource->GetUniformVectorParameterExpressions();
					for (const FMaterialVectorParameterInfo& Parameter : VectorExpressions)
					{
						FVectorParameterValue* ParameterValue = new(VectorParameterValues) FVectorParameterValue;
						ParameterValue->ParameterInfo.Name = Parameter.ParameterInfo.GetName();
						Parameter.GetDefaultValue(ParameterValue->ParameterValue);
					}

					// Textures
					for (int32 TypeIndex = 0; TypeIndex < NumMaterialTextureParameterTypes; TypeIndex++)
					{
						for (const FMaterialTextureParameterInfo& Parameter : MaterialResource->GetUniformTextureExpressions((EMaterialTextureParameterType)TypeIndex))
						{
							if (!Parameter.ParameterInfo.Name.IsNone())
							{
								FTextureParameterValue* ParameterValue = new(TextureParameterValues) FTextureParameterValue;
								ParameterValue->ParameterInfo.Name = Parameter.ParameterInfo.GetName();
								Parameter.GetGameThreadTextureValue(AsMaterial, *MaterialResource, ParameterValue->ParameterValue);
							}
						}
					}
				}
			}
		}

		InitResources();
	}
}

#if WITH_EDITOR

void FindCollectionExpressionRecursive(TArray<FGuid>& OutGuidList, const TArray<UMaterialExpression*>& InMaterialExpression)
{
	for (int32 ExpressionIndex = 0; ExpressionIndex < InMaterialExpression.Num(); ExpressionIndex++)
	{
		const UMaterialExpression* ExpressionPtr = InMaterialExpression[ExpressionIndex];
		const UMaterialExpressionCollectionParameter* CollectionPtr = Cast<UMaterialExpressionCollectionParameter>(ExpressionPtr);
		const UMaterialExpressionMaterialFunctionCall* MaterialFunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(ExpressionPtr);
		const UMaterialExpressionMaterialAttributeLayers* MaterialLayers = Cast<UMaterialExpressionMaterialAttributeLayers>(ExpressionPtr);

		if (CollectionPtr)
		{
			if (CollectionPtr->Collection)
			{
				OutGuidList.Add(CollectionPtr->Collection->StateId);
			}
			return;
		}
		else if (MaterialFunctionCall && MaterialFunctionCall->MaterialFunction)
		{
			if (const TArray<UMaterialExpression*>* FunctionExpressions = MaterialFunctionCall->MaterialFunction->GetFunctionExpressions())
			{
				FindCollectionExpressionRecursive(OutGuidList, *FunctionExpressions);
			}
		}
		else if (MaterialLayers)
		{
			const TArray<UMaterialFunctionInterface*>& Layers = MaterialLayers->GetLayers();
			const TArray<UMaterialFunctionInterface*>& Blends = MaterialLayers->GetBlends();

			for (const UMaterialFunctionInterface* Layer : Layers)
			{
				if (Layer)
				{
					if (const TArray<UMaterialExpression*>* FunctionExpressions = Layer->GetFunctionExpressions())
					{
						FindCollectionExpressionRecursive(OutGuidList, *FunctionExpressions);
					}
				}
			}

			for (const UMaterialFunctionInterface* Blend : Blends)
			{
				if (Blend)
				{
					if (const TArray<UMaterialExpression*>* FunctionExpressions = Blend->GetFunctionExpressions())
					{
						FindCollectionExpressionRecursive(OutGuidList, *FunctionExpressions);
					}
				}
			}
		}
	}
}

void UMaterialInstance::AppendReferencedParameterCollectionIdsTo(TArray<FGuid>& OutIds) const
{
	const UMaterial* Material = GetMaterial();
	if (!Material)
	{
		return;
	}

	for (const FStaticMaterialLayersParameter& LayerParameter : StaticParameters.MaterialLayersParameters)
	{
		for (const UMaterialFunctionInterface* Layer : LayerParameter.Value.Layers)
		{
			if (Layer)
			{
				if (const TArray<UMaterialExpression*>* FunctionExpressions = Layer->GetFunctionExpressions())
				{
					FindCollectionExpressionRecursive(OutIds, *FunctionExpressions);
				}
			}
		}
		for (const UMaterialFunctionInterface* Blend : LayerParameter.Value.Blends)
		{
			if (Blend)
			{
				if (const TArray<UMaterialExpression*>* FunctionExpressions = Blend->GetFunctionExpressions())
				{
					FindCollectionExpressionRecursive(OutIds, *FunctionExpressions);
				}
			}
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
UMaterialInstance::FCustomStaticParametersGetterDelegate UMaterialInstance::CustomStaticParametersGetters;
TArray<UMaterialInstance::FCustomParameterSetUpdaterDelegate> UMaterialInstance::CustomParameterSetUpdaters;
#endif // WITH_EDITORONLY_DATA
