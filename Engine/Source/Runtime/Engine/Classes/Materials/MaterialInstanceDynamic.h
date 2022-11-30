// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialInstance.h"
#include "MaterialInstanceDynamic.generated.h"

UCLASS(hidecategories=Object, collapsecategories, BlueprintType)
class ENGINE_API UMaterialInstanceDynamic : public UMaterialInstance
{
	GENERATED_UCLASS_BODY()

	/** Set a MID scalar (float) parameter value */
	UFUNCTION(BlueprintCallable, meta=(Keywords = "SetFloatParameterValue"), Category="Rendering|Material")
	void SetScalarParameterValue(FName ParameterName, float Value);

	/** Set a MID scalar (float) parameter value using MPI (to allow access to layer parameters) */
	UFUNCTION(BlueprintCallable, meta = (Keywords = "SetFloatParameterValue"), Category = "Rendering|Material")
	void SetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, float Value);

	// NOTE: These Index-related functions should be used VERY carefully, and only in cases where optimization is
	// critical.  Generally that's only if you're using an unusually high number of parameters in a material AND
	// setting a huge number of parameters in the same frame.

	// Use this function to set an initial value and fetch the index for use in the following function.
	bool InitializeScalarParameterAndGetIndex(const FName& ParameterName, float Value, int32& OutParameterIndex);
	// Use the cached value of OutParameterIndex above to set the scalar parameter ONLY on the exact same MID
	bool SetScalarParameterByIndex(int32 ParameterIndex, float Value);

	// Use this function to set an initial value and fetch the index for use in the following function.
	bool InitializeVectorParameterAndGetIndex(const FName& ParameterName, const FLinearColor& Value, int32& OutParameterIndex);
	// Use the cached value of OutParameterIndex above to set the vector parameter ONLY on the exact same MID
	bool SetVectorParameterByIndex(int32 ParameterIndex, const FLinearColor& Value);

	/** Get the current scalar (float) parameter value from an MID */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "GetScalarParameterValue", ScriptName = "GetScalarParameterValue", Keywords = "GetFloatParameterValue"), Category="Rendering|Material")
	float K2_GetScalarParameterValue(FName ParameterName);

	/** Get the current scalar (float) parameter value from an MID, using MPI (to allow access to layer parameters) */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetScalarParameterValueByInfo", ScriptName = "GetScalarParameterValueByInfo", Keywords = "GetFloatParameterValue"), Category = "Rendering|Material")
	float K2_GetScalarParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo);

	/** Set an MID texture parameter value */
	UFUNCTION(BlueprintCallable, Category="Rendering|Material")
	void SetTextureParameterValue(FName ParameterName, class UTexture* Value);

	/** Set an MID texture parameter value using MPI (to allow access to layer parameters) */
	UFUNCTION(BlueprintCallable, Category = "Rendering|Material")
	void SetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, class UTexture* Value);

	/** Get the current MID texture parameter value */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "GetTextureParameterValue", ScriptName = "GetTextureParameterValue"), Category="Rendering|Material")
	class UTexture* K2_GetTextureParameterValue(FName ParameterName);

	/** Get the current MID texture parameter value, using MPI (to allow access to layer parameters) */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetTextureParameterValueByInfo", ScriptName = "GetTextureParameterValueByInfo"), Category = "Rendering|Material")
	class UTexture* K2_GetTextureParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo);

	/** Set an MID vector parameter value */
	UFUNCTION(BlueprintCallable, meta=(Keywords = "SetColorParameterValue"), Category="Rendering|Material")
	void SetVectorParameterValue(FName ParameterName, FLinearColor Value);

	/** Set an MID vector parameter value, using MPI (to allow access to layer parameters) */
	UFUNCTION(BlueprintCallable, meta = (Keywords = "SetColorParameterValue"), Category = "Rendering|Material")
	void SetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo, FLinearColor Value);

	/** Get the current MID vector parameter value */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "GetVectorParameterValue", ScriptName = "GetVectorParameterValue", Keywords = "GetColorParameterValue"), Category="Rendering|Material")
	FLinearColor K2_GetVectorParameterValue(FName ParameterName);

	/** Get the current MID vector parameter value, using MPI (to allow access to layer parameters) */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "GetVectorParameterValueByInfo", ScriptName = "GetVectorParameterValueByInfo", Keywords = "GetColorParameterValue"), Category = "Rendering|Material")
	FLinearColor K2_GetVectorParameterValueByInfo(const FMaterialParameterInfo& ParameterInfo);
	
	/**
	 * Interpolates the scalar and vector parameters of this material instance based on two other material instances, and an alpha blending factor
	 * The output is the object itself (this).
	 * Supports the case SourceA==this || SourceB==this
	 * Both material have to be from the same base material
	 * @param SourceA value that is used for Alpha=0, silently ignores the case if 0
	 * @param SourceB value that is used for Alpha=1, silently ignores the case if 0
	 * @param Alpha usually in the range 0..1, values outside the range extrapolate
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "InterpolateMaterialInstanceParameters", ScriptName = "InterpolateMaterialInstanceParameters"), Category="Rendering|Material")
	void K2_InterpolateMaterialInstanceParams(UMaterialInstance* SourceA, UMaterialInstance* SourceB, float Alpha);

	/**
	 * Copies over parameters given a material interface (copy each instance following the hierarchy)
	 * Very slow implementation, avoid using at runtime. Hopefully we can replace it later with something like CopyInterpParameters()
	 * The output is the object itself (this). Copying 'quick parameters only' will result in a much
	 * faster copy process but will only copy dynamic scalar, vector and texture parameters on clients.
	 * @param bQuickParametersOnly Copy scalar, vector and texture parameters only. Much faster but may not include required data
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "CopyMaterialInstanceParameters", ScriptName = "CopyMaterialInstanceParameters"), Category="Rendering|Material")
	void K2_CopyMaterialInstanceParameters(UMaterialInterface* Source, bool bQuickParametersOnly = false);

	/**
	* Copies the uniform parameters (scalar, vector and texture) from a material or instance hierarchy.
	* This will typically be faster than parsing all expressions but still slow as it must walk the full
	* material hierarchy as each parameter may be overridden at any level in the chain.
	* Note: This will not copy static or font parameters
	*/
	void CopyMaterialUniformParameters(UMaterialInterface* Source);

	/**
	 * Copies over parameters given a material instance (only copy from the instance, not following the hierarchy)
	 * much faster than K2_CopyMaterialInstanceParameters(), 
	 * The output is the object itself (this).
	 * @param Source ignores the call if 0
	 */
	UFUNCTION(meta=(DisplayName = "CopyInterpParameters"), Category="Rendering|Material")
	void CopyInterpParameters(UMaterialInstance* Source);

	/**
	 * Create a material instance dynamic parented to the specified material.
	 */
	static UMaterialInstanceDynamic* Create(class UMaterialInterface* ParentMaterial, class UObject* InOuter);

	/**
	* Create a material instance dynamic parented to the specified material with the specified name.
	*/
	static UMaterialInstanceDynamic* Create( class UMaterialInterface* ParentMaterial, class UObject* InOuter, FName Name );

	/**
	 * Set the value of the given font parameter.  
	 * @param ParameterName - The name of the font parameter
	 * @param OutFontValue - New font value to set for this MIC
	 * @param OutFontPage - New font page value to set for this MIC
	 */
	void SetFontParameterValue(const FMaterialParameterInfo& ParameterInfo, class UFont* FontValue, int32 FontPage);

	/** Remove all parameter values */
	void ClearParameterValues();

	/**
	 * Copy parameter values from another material instance. This will copy only
	 * parameters explicitly overridden in that material instance!!
	 */
	UFUNCTION(BlueprintCallable, meta=(DisplayName = "CopyParameterOverrides"), Category="Rendering|Material")
	void CopyParameterOverrides(UMaterialInstance* MaterialInstance);
		
	/**
	 * Copy all interpolatable (scalar/vector) parameters from *SourceMaterialToCopyFrom to *this, using the current QualityLevel and given FeatureLevel
	 * For runtime use. More specialized and efficient than CopyMaterialInstanceParameters().
	 */
	void CopyScalarAndVectorParameters(const UMaterialInterface& SourceMaterialToCopyFrom, ERHIFeatureLevel::Type FeatureLevel);

	virtual bool HasOverridenBaseProperties()const override{ return false; }

	//Material base property overrides. MIDs cannot override these so they just grab from their parent.
	virtual float GetOpacityMaskClipValue() const override;
	virtual int32 GetTranslucentSortPriority() const override;
	virtual bool GetCastDynamicShadowAsMasked() const override;
	virtual FMaterialShadingModelField GetShadingModels() const override;
	virtual bool IsShadingModelFromMaterialExpression() const override;
	virtual EBlendMode GetBlendMode() const override;
	virtual bool IsTwoSided() const override;
	virtual bool IsDitheredLODTransition() const override;
	virtual bool IsMasked() const override;

	/**
	 * In order to remap to the correct texture streaming data, we must keep track of each texture renamed.
	 * The following map converts from a texture from the dynamic material to the texture from the static material.
	 * The following map converts from a texture from the dynamic material to the texture from the static material.
	 */
	TMap<FName, TArray<FName> > RenamedTextures;
	
	// This overrides does the remapping before looking at the parent data.
	virtual float GetTextureDensity(FName TextureName, const struct FMeshUVChannelInfo& UVChannelData) const override;

};

