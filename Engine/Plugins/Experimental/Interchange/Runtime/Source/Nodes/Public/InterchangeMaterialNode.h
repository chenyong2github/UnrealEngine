// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"
#include "UObject/UObjectGlobals.h"

#include "InterchangeMaterialNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGENODES_API FMaterialNodeStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& PayloadSourceFileKey()
			{
				static FAttributeKey AttributeKey(TEXT("__PayloadSourceFile__"));
				return AttributeKey;
			}
		};
	} //ns Interchange
}//ns UE

/**
 * enum use to declare supported parameter
 */
UENUM(BlueprintType)
enum class EInterchangeMaterialNodeParameterName : uint8
{
	BaseColor,
	EmissiveColor,
	Specular,
	Roughness,
	Metallic,
	Normal,
	Opacity,
	OpacityMask
};

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeMaterialNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMaterialNode();

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/**
	 * This function allow to retrieve the number of Texture dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	int32 GetTextureDependeciesCount() const;

	/**
	 * This function allow to retrieve the Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void GetTextureDependencies(TArray<FString>& OutDependencies) const;

	/**
	 * This function allow to retrieve one Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void GetTextureDependency(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one Texture dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetTextureDependencyUid(const FString& DependencyUid);

	/**
	 * Remove one Texture dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool RemoveTextureDependencyUid(const FString& DependencyUid);

	virtual const TOptional<FString> GetPayLoadKey() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	virtual void SetPayLoadKey(const FString& PayloadKey);

	//////////////////////////////////////////////////////////////////////////
	/**
	 * Parameter interface for c++ python and blueprint
	 * Each added parameter will create a material expression input later in the material factory.
	 * You can control the imput type to use: texture sampler, vector parameter or scalar parameter
	 */


	/**
	 * Add a texture parameter for the specified ParameterName.
	 * @param ParameterName - The parameter we want to set the texture for.
	 * @param TextureUid - The texture node uniqueID that has the texture we want to set to the specified parameter
	 * @param UVSetNameUseByTextureInput - [Optional] The fbx name of the UV set we want the material input expression use to sample the specified texture.
	 *                                     It can be empty, the material factory should in that case map it on the UV channel 0
	 * @note - A parameter name can have only one of the 3 type of input set, the last input type set is the one that will be created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void AddTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FString& TextureUid, const int32 UVSetIndex, const float ScaleU, const float ScaleV);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FString& OutTextureUid, int32& OutUVSetIndex, float& OutScaleU, float& OutScaleV) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void AddVectorParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FVector& VectorData);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetVectorParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FVector& OutVectorData) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void AddScalarParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const float ScalarData);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetScalarParameterData(const EInterchangeMaterialNodeParameterName ParameterName, float& OutScalarData) const;

	virtual void Serialize(FArchive& Ar) override;

private:
	const UE::Interchange::FAttributeKey TextureDependenciesKey = UE::Interchange::FAttributeKey(TEXT("__TextureDependenciesKey__"));

	struct FParameterData
	{
		bool bIsTextureParameter = false;
		FString TextureUid;
		int32 UVSetIndex = 0; //The "UV set" index we use to set the material sampler input default is channel 0
		float ScaleU = 1.0f;
		float ScaleV = 1.0f;

		bool bIsVectorParameter = false;
		FVector VectorParameter = FVector(0.0f);

		bool bIsScalarParameter = false;
		float ScalarParameter = 0.0f;

		friend FArchive& operator<<(FArchive& Ar, FParameterData& ParameterData)
		{
			Ar << ParameterData.bIsTextureParameter;
			Ar << ParameterData.TextureUid;
			Ar << ParameterData.UVSetIndex;
			Ar << ParameterData.ScaleU;
			Ar << ParameterData.ScaleV;

			Ar << ParameterData.bIsVectorParameter;
			Ar << ParameterData.VectorParameter;

			Ar << ParameterData.bIsScalarParameter;
			Ar << ParameterData.ScalarParameter;
			return Ar;
		}
	};

	//This member is serialize manually in the serialize override
	TMap<EInterchangeMaterialNodeParameterName, FParameterData> ParameterDatas;

	UE::Interchange::TArrayAttributeHelper<FString> TextureDependencies;
};
