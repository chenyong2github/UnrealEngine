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

