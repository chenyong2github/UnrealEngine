// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeMaterialFactory.generated.h"

class UInterchangeBaseMaterialFactoryNode;
class UInterchangeMaterialExpressionFactoryNode;
class UMaterial;
class UMaterialExpression;
class UMaterialInstance;

UCLASS(BlueprintType, Experimental)
class INTERCHANGEIMPORT_API UInterchangeMaterialFactory : public UInterchangeFactoryBase
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////
	// Interchange factory base interface begin

	virtual UClass* GetFactoryClass() const override;
	virtual UObject* CreateEmptyAsset(const FCreateAssetParams& Arguments) override;
	virtual UObject* CreateAsset(const FCreateAssetParams& Arguments) override;
	virtual void PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments) override;

	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////
private:
#if WITH_EDITOR
	void SetupMaterial(UMaterial* Material, const FCreateAssetParams& Arguments, const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode);

	UMaterialExpression* CreateExpressionsForNode(UMaterial* Material, const UInterchangeFactoryBase::FCreateAssetParams& Arguments,
	const UInterchangeMaterialExpressionFactoryNode* Expression, TMap<FString, UMaterialExpression*>& Expressions);

	UMaterialExpression* CreateExpression(UMaterial* Material, const UInterchangeFactoryBase::FCreateAssetParams& Arguments, const UInterchangeMaterialExpressionFactoryNode& ExpressionNode);
#endif // #if WITH_EDITOR

	void SetupMaterialInstance(UMaterialInstance* MaterialInstance, const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode);
};


