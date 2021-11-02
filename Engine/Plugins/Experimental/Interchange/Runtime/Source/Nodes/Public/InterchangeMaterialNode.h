// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE

#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"

#endif

#include "InterchangeMaterialNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{
		struct FMaterialNodeStaticData : public FBaseNodeStaticData
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
	UInterchangeMaterialNode()
	:UInterchangeBaseNode()
	{
		TextureDependencies.Initialize(Attributes, TextureDependenciesKey.Key);
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("MaterialNode");
		return TypeName;
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.ToString();
		if (NodeAttributeKey == UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey())
		{
			return KeyDisplayName = TEXT("Payload Source Key");
		}
		else if (NodeAttributeKey == TextureDependenciesKey)
		{
			return KeyDisplayName = TEXT("Texture Dependencies count");
		}
		else if (NodeAttributeKey.Key.StartsWith(TextureDependenciesKey.Key))
		{
			KeyDisplayName = TEXT("Texture Dependencies Index ");
			const FString IndexKey = UE::Interchange::FNameAttributeArrayHelper::IndexKey();
			int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
			if (IndexPosition < NodeAttributeKey.Key.Len())
			{
				KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
			}
			return KeyDisplayName;
		}
		return Super::GetKeyDisplayName(NodeAttributeKey);
	}

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		if (NodeAttributeKey.Key.StartsWith(TextureDependenciesKey.Key))
		{
			return FString(TEXT("TextureDependencies"));
		}
		return Super::GetAttributeCategory(NodeAttributeKey);
	}

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}

	/**
	 * This function allow to retrieve the number of Texture dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	int32 GetTextureDependeciesCount() const
	{
		return TextureDependencies.GetCount();
	}

	/**
	 * This function allow to retrieve the Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void GetTextureDependencies(TArray<FString>& OutDependencies) const
	{
		TextureDependencies.GetNames(OutDependencies);
	}

	/**
	 * This function allow to retrieve one Texture dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void GetTextureDependency(const int32 Index, FString& OutDependency) const
	{
		TextureDependencies.GetName(Index, OutDependency);
	}

	/**
	 * Add one Texture dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetTextureDependencyUid(const FString& DependencyUid)
	{
		return TextureDependencies.AddName(DependencyUid);
	}

	/**
	 * Remove one Texture dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool RemoveTextureDependencyUid(const FString& DependencyUid)
	{
		return TextureDependencies.RemoveName(DependencyUid);
	}

	virtual const TOptional<FString> GetPayLoadKey() const
	{
		if (!Attributes->ContainAttribute(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey()))
		{
			return TOptional<FString>();
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> AttributeHandle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey());
		if (!AttributeHandle.IsValid())
		{
			return TOptional<FString>();
		}
		FString PayloadKey;
		UE::Interchange::EAttributeStorageResult Result = AttributeHandle.Get(PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeMaterialNode.GetPayLoadKey"), UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey());
			return TOptional<FString>();
		}
		return TOptional<FString>(PayloadKey);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	virtual void SetPayLoadKey(const FString& PayloadKey)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey(), PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeMaterialNode.SetPayLoadKey"), UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey());
		}
	}

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
	void AddTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FString& TextureUid, const int32 UVSetIndex, const float ScaleU, const float ScaleV)
	{
		FParameterData& ParameterData = ParameterDatas.FindOrAdd(ParameterName);
		ParameterData.bIsTextureParameter = true;
		ParameterData.bIsVectorParameter = false;
		ParameterData.bIsScalarParameter = false;
		ParameterData.TextureUid = TextureUid;
		ParameterData.UVSetIndex = UVSetIndex;
		ParameterData.ScaleU = ScaleU;
		ParameterData.ScaleV = ScaleV;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FString& OutTextureUid, int32& OutUVSetIndex, float& OutScaleU, float& OutScaleV) const
	{
		if (const FParameterData* ParameterData = ParameterDatas.Find(ParameterName))
		{
			if (!ParameterData->bIsTextureParameter)
			{
				return false;
			}
			OutTextureUid = ParameterData->TextureUid;
			OutUVSetIndex = ParameterData->UVSetIndex;
			OutScaleU = ParameterData->ScaleU;
			OutScaleV = ParameterData->ScaleV;
			return true;
		}

		return false;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void AddVectorParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FVector& VectorData)
	{
		FParameterData& ParameterData = ParameterDatas.FindOrAdd(ParameterName);
		ParameterData.bIsTextureParameter = false;
		ParameterData.bIsVectorParameter = true;
		ParameterData.bIsScalarParameter = false;
		ParameterData.VectorParameter = VectorData;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetVectorParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FVector& OutVectorData) const
	{
		if (const FParameterData* ParameterData = ParameterDatas.Find(ParameterName))
		{
			if (!ParameterData->bIsVectorParameter)
			{
				return false;
			}
			OutVectorData = ParameterData->VectorParameter;
			return true;
		}
		return false;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void AddScalarParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const float ScalarData)
	{
		FParameterData& ParameterData = ParameterDatas.FindOrAdd(ParameterName);
		ParameterData.bIsTextureParameter = false;
		ParameterData.bIsVectorParameter = false;
		ParameterData.bIsScalarParameter = true;
		ParameterData.ScalarParameter = ScalarData;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetScalarParameterData(const EInterchangeMaterialNodeParameterName ParameterName, float& OutScalarData) const
	{
		if (const FParameterData* ParameterData = ParameterDatas.Find(ParameterName))
		{
			if (!ParameterData->bIsScalarParameter)
			{
				return false;
			}
			OutScalarData = ParameterData->ScalarParameter;
			return true;
		}
		return false;
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);

		if (Ar.IsLoading())
		{
			ParameterDatas.Reset();
		}
		int32 ParamCount = ParameterDatas.Num();
		Ar << ParamCount;

		if (Ar.IsSaving())
		{
			for (TPair<EInterchangeMaterialNodeParameterName, FParameterData> ParamPair : ParameterDatas)
			{
				Ar << ParamPair.Key;
				Ar << ParamPair.Value;
			}
		}
		else if (Ar.IsLoading())
		{
			for (int32 ParamIndex = 0; ParamIndex < ParamCount; ++ParamIndex)
			{
				EInterchangeMaterialNodeParameterName ParamKey;
				Ar << ParamKey;
				FParameterData& ParamValue = ParameterDatas.FindOrAdd(ParamKey);
				Ar << ParamValue;
			}
		}
	}

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

	UE::Interchange::FNameAttributeArrayHelper TextureDependencies;
};
