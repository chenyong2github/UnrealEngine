// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE

#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"

#endif

#include "InterchangeMaterialNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{
		namespace Material
		{
			/**
			 * Remove _SkinXX from a string
			 */
			static FString RemoveSkinFromName(const FString& Name)
			{
				int32 SkinOffset = Name.Find(TEXT("_skin"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (SkinOffset != INDEX_NONE)
				{
					FString MaterialNameNoSkin = Name;
					FString SkinXXNumber = Name.Right(Name.Len() - (SkinOffset + 1)).RightChop(4);
					if (SkinXXNumber.IsNumeric())
					{
						MaterialNameNoSkin = Name.LeftChop(Name.Len() - SkinOffset);
					}
					return MaterialNameNoSkin;
				}
				return Name;
			}
		} //ns Material

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

UCLASS(BlueprintType)
class INTERCHANGENODEPLUGIN_API UInterchangeMaterialNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMaterialNode()
	:UInterchangeBaseNode()
	{
#if WITH_ENGINE
		AssetClass = nullptr;
#endif
	}

	/**
	 * Initialize node data
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the material factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void InitializeMaterialNode(const FName& UniqueID, const FName& DisplayLabel, const FString& InAssetClass)
	{
		bIsMaterialNodeClassInitialized = false;
		InitializeNode(UniqueID, DisplayLabel);
		FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
		InterchangePrivateNodeBase::SetCustomAttribute<FString>(Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
		FillAssetClassFromAttribute();
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("MaterialNode");
		return TypeName;
	}

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	virtual class UClass* GetAssetClass() const override
	{
		ensure(bIsMaterialNodeClassInitialized);
#if WITH_ENGINE
		return AssetClass.Get() != nullptr ? AssetClass.Get() : UMaterial::StaticClass();
#else
		return nullptr;
#endif
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
	}

	virtual const TOptional<FString> GetPayLoadKey() const
	{
		if (!Attributes.ContainAttribute(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey()))
		{
			return TOptional<FString>();
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> AttributeHandle = Attributes.GetAttributeHandle<FString>(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey());
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
		UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey(), PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeMaterialNode.SetPayLoadKey"), UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey());
		}
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomMaterialUsage(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(MaterialUsage, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomMaterialUsage(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeMaterialNode, MaterialUsage, uint8, UMaterialInterface);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetCustomBlendMode(uint8& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(BlendMode, uint8);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool SetCustomBlendMode(const uint8& AttributeValue, bool bAddApplyDelegate = true)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER(UInterchangeMaterialNode, BlendMode, uint8, UMaterialInterface);
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
	 * @param TextureUID - The texture node uniqueID that has the texture we want to set to the specified parameter
	 * @param UVSetNameUseByTextureInput - [Optional] The fbx name of the UV set we want the material input expression use to sample the specified texture.
	 *                                     It can be empty, the material factory should in that case map it on the UV channel 0
	 * @note - A parameter name can have only one of the 3 type of input set, the last input type set is the one that will be created.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	void AddTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FName TextureUID, const int32 UVSetIndex, const float ScaleU, const float ScaleV)
	{
		FParameterData& ParameterData = ParameterDatas.FindOrAdd(ParameterName);
		ParameterData.bIsTextureParameter = true;
		ParameterData.bIsVectorParameter = false;
		ParameterData.bIsScalarParameter = false;
		ParameterData.TextureUID = TextureUID;
		ParameterData.UVSetIndex = UVSetIndex;
		ParameterData.ScaleU = ScaleU;
		ParameterData.ScaleV = ScaleV;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Material")
	bool GetTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FName& OutTextureUID, int32& OutUVSetIndex, float& OutScaleU, float& OutScaleV) const
	{
		if (const FParameterData* ParameterData = ParameterDatas.Find(ParameterName))
		{
			if (!ParameterData->bIsTextureParameter)
			{
				return false;
			}
			OutTextureUID = ParameterData->TextureUID;
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

#if WITH_ENGINE
		if (Ar.IsLoading())
		{
			//Make sure the class is properly set when we compile with engine, this will set the
			//bIsMaterialNodeClassInitialized to true.
			SetMaterialNodeClassFromClassAttribute();
		}
#endif //#if WITH_ENGINE
	}

private:

	void FillAssetClassFromAttribute()
	{
#if WITH_ENGINE
		FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
		FString ClassName;
		InterchangePrivateNodeBase::GetCustomAttribute<FString>(Attributes, ClassNameAttributeKey, OperationName, ClassName);
		if (ClassName.Equals(UMaterial::StaticClass()->GetName()))
		{
			AssetClass = UMaterial::StaticClass();
			bIsMaterialNodeClassInitialized = true;
		}
		else if (ClassName.Equals(UMaterialInstance::StaticClass()->GetName()))
		{
			AssetClass = UMaterialInstance::StaticClass();
			bIsMaterialNodeClassInitialized = true;
		}
#endif
	}

	bool SetMaterialNodeClassFromClassAttribute()
	{
		if (!bIsMaterialNodeClassInitialized)
		{
			FillAssetClassFromAttribute();
		}
		return bIsMaterialNodeClassInitialized;
	}

	struct FParameterData
	{
		bool bIsTextureParameter = false;
		FName TextureUID = NAME_None;
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
			Ar << ParameterData.TextureUID;
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

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FAttributeKey(TEXT("__ClassTypeAttribute__"));

	//Material Attribute
	const UE::Interchange::FAttributeKey Macro_CustomMaterialUsageKey = UE::Interchange::FAttributeKey(TEXT("MaterialUsage"));
	const UE::Interchange::FAttributeKey Macro_CustomBlendModeKey = UE::Interchange::FAttributeKey(TEXT("BlendMode"));

#if WITH_ENGINE

	//Material Ussage is not handle properly, you can have several usage check
	//Currently I can set only one and retrieve the first, TODO make this work (maybe one key per usage...)
	bool ApplyCustomMaterialUsageToAsset(UObject * Asset) const
	{
		if (!Asset)
		{
			return false;
		}
		UMaterialInterface* TypedObject = Cast<UMaterialInterface>(Asset);
		if (!TypedObject)
		{
			return false;
		}
		uint8 ValueData;
		if (GetCustomMaterialUsage(ValueData))
		{
			TypedObject->CheckMaterialUsage_Concurrent(EMaterialUsage(ValueData));
			return true;
		}
		return false;
	}

	bool FillCustomMaterialUsageFromAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}
		UMaterialInterface* TypedObject = Cast<UMaterialInterface>(Asset);
		if (!TypedObject)
		{
			return false;
		}
		const UMaterial* Material = TypedObject->GetMaterial_Concurrent();
		if (!Material)
		{
			return false;
		}
		for (int32 Usage = 0; Usage < MATUSAGE_MAX; Usage++)
		{
			const EMaterialUsage UsageEnum = (EMaterialUsage)Usage;
			if (Material->GetUsageByFlag(UsageEnum))
			{
				if (SetCustomMaterialUsage((int8)UsageEnum, false))
				{
					return true;
				}
			}
		}
		return false;
	}
#endif

	IMPLEMENT_NODE_ATTRIBUTE_APPLY_UOBJECT(BlendMode, uint8, UMaterial, TEnumAsByte<enum EBlendMode>);

protected:
#if WITH_ENGINE
	TSubclassOf<UTexture> AssetClass = nullptr;
#endif
	bool bIsMaterialNodeClassInitialized = false;

};
