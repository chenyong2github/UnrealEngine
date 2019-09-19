// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
#include "MaterialExpressionRuntimeVirtualTextureSampleParameter.generated.h"

UCLASS(collapsecategories, hidecategories = Object, MinimalAPI)
class UMaterialExpressionRuntimeVirtualTextureSampleParameter : public UMaterialExpressionRuntimeVirtualTextureSample
{
	GENERATED_UCLASS_BODY()

	/** Name to be referenced when we want to find and set this parameter */
	UPROPERTY(EditAnywhere, Category = MaterialParameter)
	FName ParameterName;

	/** GUID that should be unique within the material, this is used for parameter renaming. */
	UPROPERTY()
	FGuid ExpressionGUID;

	/** The name of the parameter Group to display in MaterialInstance Editor. Default is None group */
	UPROPERTY(EditAnywhere, Category = MaterialParameter)
	FName Group;

#if WITH_EDITORONLY_DATA
	/** Controls where the this parameter is displayed in a material instance parameter list. The lower the number the higher up in the parameter list. */
	UPROPERTY(EditAnywhere, Category = MaterialParameter)
	int32 SortPriority;
#endif

#if WITH_EDITOR
	/** If this is the named parameter from this material expression, then set its value. */
	bool SetParameterValue(FName InParameterName, URuntimeVirtualTexture* InValue);
#endif

	/** Return whether this is the named parameter from this material expression, and if it is then return its value. */
	bool IsNamedParameter(const FMaterialParameterInfo& ParameterInfo, URuntimeVirtualTexture*& OutValue) const;
	/** Adds to arrays of parameter info and id with the values used by this material expression. */
	void GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual bool CanRenameNode() const override { return true; }
	virtual void SetEditableName(const FString& NewName) override;
	virtual FString GetEditableName() const override;
	virtual bool HasAParameterName() const override { return true; }
	virtual void SetParameterName(const FName& Name) override { ParameterName = Name; }
	virtual FName GetParameterName() const override { return ParameterName; }
	virtual void ValidateParameterName(const bool bAllowDuplicateName) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual bool MatchesSearchQuery(const TCHAR* SearchQuery) override;
	virtual void SetValueToMatchingExpression(UMaterialExpression* OtherExpression) override;
#endif
	virtual FGuid& GetParameterExpressionId() override { return ExpressionGUID; }
	//~ End UMaterialExpression Interface
};
