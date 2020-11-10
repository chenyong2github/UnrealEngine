// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MaterialInstance.h: MaterialInstance definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RenderingThread.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstance.h"
#include "HAL/LowLevelMemTracker.h"

class UTexture;

/**
 * Cache uniform expressions for the given material instance.
 * @param MaterialInstance - The material instance for which to cache uniform expressions.
 */
void CacheMaterialInstanceUniformExpressions(const UMaterialInstance* MaterialInstance, bool bRecreateUniformBuffer = false);

/**
 * Recaches uniform expressions for all material instances with a given parent.
 * WARNING: This function is a noop outside of the Editor!
 * @param ParentMaterial - The parent material to look for.
 */
void RecacheMaterialInstanceUniformExpressions(const UMaterialInterface* ParentMaterial, bool bRecreateUniformBuffer);

/** Protects the members of a UMaterialInstanceConstant from re-entrance. */
class FMICReentranceGuard
{
public:
#if !WITH_EDITOR
	FMICReentranceGuard(const UMaterialInstance* InMaterial) {}
#else
	FMICReentranceGuard(const UMaterialInstance* InMaterial)
	{
		Material = const_cast<UMaterialInstance*>(InMaterial);

		if (Material->GetReentrantFlag() == true)
		{
			UE_LOG(LogMaterial, Warning, TEXT("InMaterial: %s GameThread: %d RenderThread: %d"), *InMaterial->GetFullName(), IsInGameThread(), IsInRenderingThread());
			check(!Material->GetReentrantFlag());
		}
		Material->SetReentrantFlag(true);
	}

	~FMICReentranceGuard()
	{
		Material->SetReentrantFlag(false);
	}

private:
	UMaterialInstance* Material;
#endif // WITH_EDITOR
};

/**
* The resource used to render a UMaterialInstance.
*/
class FMaterialInstanceResource: public FMaterialRenderProxy
{
public:

	/** Material instances store pairs of names and values in arrays to look up parameters at run time. */
	template <typename ValueType>
	struct TNamedParameter
	{
		FHashedMaterialParameterInfo Info;
		ValueType Value;
	};

	/** Initialization constructor. */
	FMaterialInstanceResource(UMaterialInstance* InOwner);

	/**
	 * Called from the game thread to destroy the material instance on the rendering thread.
	 */
	void GameThread_Destroy()
	{
		FMaterialInstanceResource* Resource = this;
		ENQUEUE_RENDER_COMMAND(FDestroyMaterialInstanceResourceCommand)(
			[Resource](FRHICommandList& RHICmdList)
			{
				delete Resource;
			});
	}

	// FRenderResource interface.
	virtual FString GetFriendlyName() const override { return Owner->GetName(); }

	// FMaterialRenderProxy interface.
	/** Get the FMaterial that should be used for rendering, but might not be in a valid state to actually use.  Can return NULL. */
	virtual const FMaterial* GetMaterialNoFallback(ERHIFeatureLevel::Type FeatureLevel) const override;
	virtual const FMaterialRenderProxy* GetFallback(ERHIFeatureLevel::Type InFeatureLevel) const override;
	virtual UMaterialInterface* GetMaterialInterface() const override;
	
	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override;
	virtual bool GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override;
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override;
	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const override;

	void GameThread_SetParent(UMaterialInterface* ParentMaterialInterface);

	void InitMIParameters(struct FMaterialInstanceParameterSet& ParameterSet);

	/**
	 * Clears all parameters set on this material instance.
	 */
	void RenderThread_ClearParameters()
	{
		VectorParameterArray.Empty();
		ScalarParameterArray.Empty();
		TextureParameterArray.Empty();
		RuntimeVirtualTextureParameterArray.Empty();
		InvalidateUniformExpressionCache(false);
	}

	/**
	 * Updates a named parameter on the render thread.
	 */
	template <typename ValueType>
	void RenderThread_UpdateParameter(const FHashedMaterialParameterInfo& ParameterInfo, const ValueType& Value )
	{
		LLM_SCOPE(ELLMTag::MaterialInstance);

		InvalidateUniformExpressionCache(false);
		TArray<TNamedParameter<ValueType> >& ValueArray = GetValueArray<ValueType>();
		const int32 ParameterCount = ValueArray.Num();
		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			TNamedParameter<ValueType>& Parameter = ValueArray[ParameterIndex];
			if (Parameter.Info == ParameterInfo)
			{
				Parameter.Value = Value;
				return;
			}
		}
		TNamedParameter<ValueType> NewParameter;
		NewParameter.Info = ParameterInfo;
		NewParameter.Value = Value;
		ValueArray.Add(NewParameter);
	}

	/**
	 * Retrieves a parameter by name.
	 */
	template <typename ValueType>
	const ValueType* RenderThread_FindParameterByName(const FHashedMaterialParameterInfo& ParameterInfo) const
	{
		const TArray<TNamedParameter<ValueType> >& ValueArray = GetValueArray<ValueType>();
		const int32 ParameterCount = ValueArray.Num();
		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			const TNamedParameter<ValueType>& Parameter = ValueArray[ParameterIndex];
			if (Parameter.Info == ParameterInfo)
			{
				return &Parameter.Value;
			}
		}
		return NULL;
	}
	
private:
	/**
	 * Retrieves the array of values for a given type.
	 */
	template <typename ValueType> TArray<TNamedParameter<ValueType> >& GetValueArray() { return ScalarParameterArray; }
	template <typename ValueType> const TArray<TNamedParameter<ValueType> >& GetValueArray() const { return ScalarParameterArray; }

	/** The parent of the material instance. */
	UMaterialInterface* Parent;

	/** The UMaterialInstance which owns this resource. */
	UMaterialInstance* Owner;

	/** The game thread accessible parent of the material instance. */
	UMaterialInterface* GameThreadParent;
	
	/** Vector parameters for this material instance. */
	TArray<TNamedParameter<FLinearColor> > VectorParameterArray;
	/** Scalar parameters for this material instance. */
	TArray<TNamedParameter<float> > ScalarParameterArray;
	/** Texture parameters for this material instance. */
	TArray<TNamedParameter<const UTexture*> > TextureParameterArray;
	/** Runtime Virtual Texture parameters for this material instance. */
	TArray<TNamedParameter<const URuntimeVirtualTexture*> > RuntimeVirtualTextureParameterArray; 
};

template <> FORCEINLINE TArray<FMaterialInstanceResource::TNamedParameter<float> >& FMaterialInstanceResource::GetValueArray() { return ScalarParameterArray; }
template <> FORCEINLINE TArray<FMaterialInstanceResource::TNamedParameter<FLinearColor> >& FMaterialInstanceResource::GetValueArray() { return VectorParameterArray; }
template <> FORCEINLINE TArray<FMaterialInstanceResource::TNamedParameter<const UTexture*> >& FMaterialInstanceResource::GetValueArray() { return TextureParameterArray; }
template <> FORCEINLINE TArray<FMaterialInstanceResource::TNamedParameter<const URuntimeVirtualTexture*> >& FMaterialInstanceResource::GetValueArray() { return RuntimeVirtualTextureParameterArray; }
template <> FORCEINLINE const TArray<FMaterialInstanceResource::TNamedParameter<float> >& FMaterialInstanceResource::GetValueArray() const { return ScalarParameterArray; }
template <> FORCEINLINE const TArray<FMaterialInstanceResource::TNamedParameter<FLinearColor> >& FMaterialInstanceResource::GetValueArray() const { return VectorParameterArray; }
template <> FORCEINLINE const TArray<FMaterialInstanceResource::TNamedParameter<const UTexture*> >& FMaterialInstanceResource::GetValueArray() const { return TextureParameterArray; }
template <> FORCEINLINE const TArray<FMaterialInstanceResource::TNamedParameter<const URuntimeVirtualTexture*> >& FMaterialInstanceResource::GetValueArray() const { return RuntimeVirtualTextureParameterArray; }

struct FMaterialInstanceParameterSet
{
	TArray<FMaterialInstanceResource::TNamedParameter<float> > ScalarParameters;
	TArray<FMaterialInstanceResource::TNamedParameter<FLinearColor> > VectorParameters;
	TArray<FMaterialInstanceResource::TNamedParameter<const UTexture*> > TextureParameters;
	TArray<FMaterialInstanceResource::TNamedParameter<const URuntimeVirtualTexture*> > RuntimeVirtualTextureParameters;
};
	
/** Finds a parameter by name from the game thread. */
template <typename ParameterType>
ParameterType* GameThread_FindParameterByName(TArray<ParameterType>& Parameters, const FHashedMaterialParameterInfo& ParameterInfo)
{
	for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ParameterIndex++)
	{
		ParameterType* Parameter = &Parameters[ParameterIndex];
		if (Parameter->ParameterInfo == ParameterInfo)
		{
			return Parameter;
		}
	}
	return NULL;
}
template <typename ParameterType>
const ParameterType* GameThread_FindParameterByName(const TArray<ParameterType>& Parameters, const FHashedMaterialParameterInfo& ParameterInfo)
{
	for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ParameterIndex++)
	{
		const ParameterType* Parameter = &Parameters[ParameterIndex];
		if (Parameter->ParameterInfo == ParameterInfo)
		{
			return Parameter;
		}
	}
	return NULL;
}

template <typename ParameterType>
int32 GameThread_FindParameterIndexByName(const TArray<ParameterType>& Parameters, const FHashedMaterialParameterInfo& ParameterInfo)
{
	for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
	{
		const ParameterType* Parameter = &Parameters[ParameterIndex];
		if (Parameter->ParameterInfo == ParameterInfo)
		{
			return ParameterIndex;
		}
	}

	return INDEX_NONE;
}

/** Finds a parameter by index from the game thread. */
template <typename ParameterType>
ParameterType* GameThread_FindParameterByIndex(TArray<ParameterType>& Parameters, int32 Index)
{
	if (!Parameters.IsValidIndex(Index))
	{
		return nullptr;
	}

	return &Parameters[Index];
}

template <typename ParameterType>
const ParameterType* GameThread_FindParameterByIndex(const TArray<ParameterType>& Parameters, int32 Index)
{
	if (!Parameters.IsValidIndex(Index))
	{
		return nullptr;
	}

	return &Parameters[Index];
}
