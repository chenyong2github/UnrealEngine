// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeShaderGraphNode.h"

#if WITH_ENGINE
#include "Materials/MaterialInterface.h"
#endif

#include "InterchangeMaterialFactoryNode.generated.h"

UCLASS(Abstract, Experimental)
class INTERCHANGENODES_API UInterchangeBaseMaterialFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	static FString GetMaterialFactoryNodeUidFromMaterialNodeUid(const FString& TranslatedNodeUid);
};

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeMaterialFactoryNode : public UInterchangeBaseMaterialFactoryNode
{
	GENERATED_BODY()

public:

	virtual FString GetTypeName() const override;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	virtual class UClass* GetObjectClass() const override;

// Material Inputs
public:
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetBaseColorConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToBaseColor(const FString& AttributeValue);
	
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToBaseColor(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetMetallicConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToMetallic(const FString& AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToMetallic(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetSpecularConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToSpecular(const FString& ExpressionNodeUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToSpecular(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetRoughnessConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToRoughness(const FString& ExpressionNodeUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToRoughness(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetEmissiveColorConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToEmissiveColor(const FString& ExpressionNodeUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToEmissiveColor(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetNormalConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToNormal(const FString& ExpressionNodeUid);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToNormal(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetOpacityConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToOpacity(const FString& AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToOpacity(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetOcclusionConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToOcclusion(const FString& AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToOcclusion(const FString& ExpressionNodeUid, const FString& OutputName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool GetRefractionConnection(FString& ExpressionNodeUid, FString& OutputName) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectToRefraction(const FString& AttributeValue);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | MaterialFactory")
	bool ConnectOutputToRefraction(const FString& ExpressionNodeUid, const FString& OutputName);

// Material parameters
public:
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomShadingModel(uint8& AttributeValue) const;
 
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomShadingModel(const uint8& AttributeValue, bool bAddApplyDelegate = true);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomTranslucencyLightingMode(uint8& AttributeValue) const;
 
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomTranslucencyLightingMode(const uint8& AttributeValue, bool bAddApplyDelegate = true);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomBlendMode(uint8& AttributeValue) const;
 
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomBlendMode(const uint8& AttributeValue, bool bAddApplyDelegate = true);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomTwoSided(bool& AttributeValue) const;
 
	/** Sets if this shader graph should be rendered two sided or not. Defaults to off. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomTwoSided(const bool& AttributeValue, bool bAddApplyDelegate = true);

private:
	const UE::Interchange::FAttributeKey Macro_CustomShadingModelKey = UE::Interchange::FAttributeKey(TEXT("ShadingModel"));
	const UE::Interchange::FAttributeKey Macro_CustomTranslucencyLightingModeKey = UE::Interchange::FAttributeKey(TEXT("TranslucencyLightingMode"));
	const UE::Interchange::FAttributeKey Macro_CustomBlendModeKey = UE::Interchange::FAttributeKey(TEXT("BlendMode"));
	const UE::Interchange::FAttributeKey Macro_CustomTwoSidedKey = UE::Interchange::FAttributeKey(TEXT("TwoSided"));
};

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeMaterialExpressionFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	virtual FString GetTypeName() const override;

public:
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomExpressionClassName(FString& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomExpressionClassName(const FString& AttributeValue);

private:
	const UE::Interchange::FAttributeKey Macro_CustomExpressionClassNameKey = UE::Interchange::FAttributeKey(TEXT("ExpressionClassName"));

};

