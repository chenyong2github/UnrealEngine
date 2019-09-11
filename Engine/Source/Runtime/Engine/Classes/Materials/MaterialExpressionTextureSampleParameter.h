// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "MaterialExpressionTextureSampleParameter.generated.h"

class UTexture;
struct FMaterialParameterInfo;

UCLASS(collapsecategories, abstract, hidecategories=Object)
class ENGINE_API UMaterialExpressionTextureSampleParameter : public UMaterialExpressionTextureSample
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=MaterialExpressionTextureSampleParameter)
	FName ParameterName;

	/** GUID that should be unique within the material, this is used for parameter renaming. */
	UPROPERTY()
	FGuid ExpressionGUID;

	/** The name of the parameter Group to display in MaterialInstance Editor. Default is None group */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionTextureSampleParameter)
	FName Group;

#if WITH_EDITORONLY_DATA
	/** Controls where the this parameter is displayed in a material instance parameter list.  The lower the number the higher up in the parameter list. */
	UPROPERTY(EditAnywhere, Category = MaterialExpressionTextureSampleParameter)
	int32 SortPriority;

	UPROPERTY(EditAnywhere, Category = ParameterCustomization)
	FParameterChannelNames ChannelNames;
#endif

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual bool MatchesSearchQuery(const TCHAR* SearchQuery) override;
	virtual bool CanRenameNode() const override { return true; }
	virtual FString GetEditableName() const override;
	virtual void SetEditableName(const FString& NewName) override;

	virtual bool HasAParameterName() const override { return true; }
	virtual FName GetParameterName() const override { return ParameterName; }
	virtual void SetParameterName(const FName& Name) override { ParameterName = Name; }
	virtual void ValidateParameterName(const bool bAllowDuplicateName) override;
	virtual void SetValueToMatchingExpression(UMaterialExpression* OtherExpression) override;
#endif
	//~ End UMaterialExpression Interface

	/** Return whether this is the named parameter, and fill in its value */
	bool IsNamedParameter(const FMaterialParameterInfo& ParameterInfo, UTexture*& OutValue) const;

#if WITH_EDITOR
	bool SetParameterValue(FName InParameterName, UTexture* InValue);
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void ApplyChannelNames();
#endif

	/**
	 * Return true if the texture is a movie texture
	 *
	 * @param	InTexture - texture to test
	 * @param	OutMessage - if texture isn't valid, gives a description of the problem
	 * @return	true/false
	 */	
	virtual bool TextureIsValid(UTexture* InTexture, FString& OutMessage);

	/**
	 *	Sets the default texture if none is set
	 */
	virtual void SetDefaultTexture();

	virtual FGuid& GetParameterExpressionId() override
	{
		return ExpressionGUID;
	}

#if WITH_EDITOR
	FParameterChannelNames GetTextureChannelNames() const
	{
		return ChannelNames;
	}
#endif

	void GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const;
};
