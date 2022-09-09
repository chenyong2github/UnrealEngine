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

UCLASS(BlueprintType)
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
	virtual bool GetSourceFilenames(const UObject* Object, TArray<FString>& OutSourceFilenames) const override;
	virtual bool SetSourceFilename(const UObject* Object, const FString& SourceFilename, int32 SourceIndex) const override;

	// Interchange factory base interface end
	//////////////////////////////////////////////////////////////////////////
private:
#if WITH_EDITOR
	void SetupMaterial(UMaterial* Material, const FCreateAssetParams& Arguments, const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode);
#endif // #if WITH_EDITOR

	void SetupMaterialInstance(UMaterialInstance* MaterialInstance, const UInterchangeBaseNodeContainer* NodeContainer, const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode);
};

UCLASS(BlueprintType)
class INTERCHANGEIMPORT_API UInterchangeMaterialFunctionFactory : public UInterchangeFactoryBase
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
	void SetupMaterial(class UMaterialFunction* Material, const FCreateAssetParams& Arguments, const class UInterchangeMaterialFunctionFactoryNode* MaterialFactoryNode);
#endif
};


