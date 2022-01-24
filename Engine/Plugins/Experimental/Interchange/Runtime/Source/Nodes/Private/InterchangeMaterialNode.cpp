// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMaterialNode.h"

UInterchangeMaterialNode::UInterchangeMaterialNode()
{
	TextureDependencies.Initialize(Attributes, TextureDependenciesKey.Key);
}

/**
 * Return the node type name of the class, we use this when reporting error
 */
FString UInterchangeMaterialNode::GetTypeName() const
{
	const FString TypeName = TEXT("MaterialNode");
	return TypeName;
}

FString UInterchangeMaterialNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
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
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	return Super::GetKeyDisplayName(NodeAttributeKey);
}

FString UInterchangeMaterialNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	if (NodeAttributeKey.Key.StartsWith(TextureDependenciesKey.Key))
	{
		return FString(TEXT("TextureDependencies"));
	}
	return Super::GetAttributeCategory(NodeAttributeKey);
}

int32 UInterchangeMaterialNode::GetTextureDependeciesCount() const
{
	return TextureDependencies.GetCount();
}

void UInterchangeMaterialNode::GetTextureDependencies(TArray<FString>& OutDependencies) const
{
	TextureDependencies.GetItems(OutDependencies);
}

void UInterchangeMaterialNode::GetTextureDependency(const int32 Index, FString& OutDependency) const
{
	TextureDependencies.GetItem(Index, OutDependency);
}

bool UInterchangeMaterialNode::SetTextureDependencyUid(const FString& DependencyUid)
{
	return TextureDependencies.AddItem(DependencyUid);
}

bool UInterchangeMaterialNode::RemoveTextureDependencyUid(const FString& DependencyUid)
{
	return TextureDependencies.RemoveItem(DependencyUid);
}

const TOptional<FString> UInterchangeMaterialNode::GetPayLoadKey() const
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

void UInterchangeMaterialNode::SetPayLoadKey(const FString& PayloadKey)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey(), PayloadKey);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("UInterchangeMaterialNode.SetPayLoadKey"), UE::Interchange::FMaterialNodeStaticData::PayloadSourceFileKey());
	}
}

void UInterchangeMaterialNode::AddTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FString& TextureUid, const int32 UVSetIndex, const float ScaleU, const float ScaleV)
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

bool UInterchangeMaterialNode::GetTextureParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FString& OutTextureUid, int32& OutUVSetIndex, float& OutScaleU, float& OutScaleV) const
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

void UInterchangeMaterialNode::AddVectorParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const FVector& VectorData)
{
	FParameterData& ParameterData = ParameterDatas.FindOrAdd(ParameterName);
	ParameterData.bIsTextureParameter = false;
	ParameterData.bIsVectorParameter = true;
	ParameterData.bIsScalarParameter = false;
	ParameterData.VectorParameter = VectorData;
}

bool UInterchangeMaterialNode::GetVectorParameterData(const EInterchangeMaterialNodeParameterName ParameterName, FVector& OutVectorData) const
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

void UInterchangeMaterialNode::AddScalarParameterData(const EInterchangeMaterialNodeParameterName ParameterName, const float ScalarData)
{
	FParameterData& ParameterData = ParameterDatas.FindOrAdd(ParameterName);
	ParameterData.bIsTextureParameter = false;
	ParameterData.bIsVectorParameter = false;
	ParameterData.bIsScalarParameter = true;
	ParameterData.ScalarParameter = ScalarData;
}

bool UInterchangeMaterialNode::GetScalarParameterData(const EInterchangeMaterialNodeParameterName ParameterName, float& OutScalarData) const
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

void UInterchangeMaterialNode::Serialize(FArchive& Ar)
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