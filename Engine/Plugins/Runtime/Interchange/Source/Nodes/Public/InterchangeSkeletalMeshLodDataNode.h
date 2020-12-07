// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeSkeletalMeshLodDataNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FSkeletalMeshNodeLodDataStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& TranslatorMeshKeysCountKey()
			{
				static FAttributeKey AttributeKey(TEXT("__LOD_DATA_IDS_COUNT_"));
				return AttributeKey;
			}

			static const FAttributeKey& TranslatorMeshKeysBaseKey()
			{
				static FAttributeKey AttributeKey(TEXT("__LOD_DATA_IDS_INDEX_"));
				return AttributeKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType)
class INTERCHANGENODEPLUGIN_API UInterchangeSkeletalMeshLodDataNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSkeletalMeshLodDataNode()
		:UInterchangeBaseNode()
	{
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("SkeletalMeshLodDataNode");
		return TypeName;
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
	}


public:
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool GetCustomSkeletonID(FName& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonID, FName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool SetCustomSkeletonID(const FName& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonID, FName)
	}

	/** Return The number of LOD this skeletalmesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	int32 GetTranslatorMeshKeyCount() const
	{
		int32 MeshCount = 0;
		if (Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey()))
		{
			UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey());
			if (Handle.IsValid())
			{
				Handle.Get(MeshCount);
			}
		}
		return MeshCount;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	void GetTranslatorMeshKeys(TArray<FName>& OutTranslatorMeshKeys) const
	{
		OutTranslatorMeshKeys.Reset();

		int32 MeshCount = 0;
		if (!Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey()))
		{
			return;
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey());
		if (!Handle.IsValid())
		{
			return;
		}
		Handle.Get(MeshCount);
		for (int32 MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex)
		{
			FString DepIndexKeyString = UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysBaseKey().ToString() + FString::FromInt(MeshIndex);
			UE::Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
			UE::Interchange::FAttributeStorage::TAttributeHandle<FName> HandleMesh = Attributes.GetAttributeHandle<FName>(DepIndexKey);
			if (!HandleMesh.IsValid())
			{
				continue;
			}
			FName& NodeUniqueID = OutTranslatorMeshKeys.AddDefaulted_GetRef();
			HandleMesh.Get(NodeUniqueID);
		}
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool AddTranslatorMeshKey(FName TranslatorMeshKey)
	{
		if (!Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey()))
		{
			const int32 DependencyCount = 0;
			UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute<int32>(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey(), DependencyCount);
			if (!UE::Interchange::IsAttributeStorageResultSuccess(Result))
			{
				UE::Interchange::LogAttributeStorageErrors(Result, TEXT("UInterchangeSkeletalMeshLodDataNode.AddTranslatorMeshKey"), UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey());
				return false;
			}
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey());
		if (!ensure(Handle.IsValid()))
		{
			return false;
		}
		int32 MeshIndex = 0;
		Handle.Get(MeshIndex);
		FString MeshIndexKeyString = UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysBaseKey().ToString() + FString::FromInt(MeshIndex);
		UE::Interchange::FAttributeKey MeshIndexKey(*MeshIndexKeyString);
		//Increment the counter
		MeshIndex++;
		Handle.Set(MeshIndex);

		UE::Interchange::EAttributeStorageResult AddMeshResult = Attributes.RegisterAttribute<FName>(MeshIndexKey, TranslatorMeshKey);
		return true;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool RemoveTranslatorMeshKey(FName TranslatorMeshKey)
	{
		int32 MeshCount = 0;
		if (!Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey()))
		{
			return false;
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysCountKey());
		if (!Handle.IsValid())
		{
			return false;
		}
		Handle.Get(MeshCount);
		bool DecrementKey = false;
		for (int32 MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex)
		{
			FString DepIndexKeyString = UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysBaseKey().ToString() + FString::FromInt(MeshIndex);
			UE::Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
			UE::Interchange::FAttributeStorage::TAttributeHandle<FName> HandleMesh = Attributes.GetAttributeHandle<FName>(DepIndexKey);
			if (!HandleMesh.IsValid())
			{
				continue;
			}
			FName MeshUniqueID;
			HandleMesh.Get(MeshUniqueID);
			if (MeshUniqueID == TranslatorMeshKey)
			{
				//Remove this entry
				Attributes.UnregisterAttribute(DepIndexKey);
				Handle.Set(MeshCount-1);
				//We have to rename the key for all the next item
				DecrementKey = true;;
			}
			else if (DecrementKey)
			{
				FString NewDepIndexKeyString = UE::Interchange::FSkeletalMeshNodeLodDataStaticData::TranslatorMeshKeysBaseKey().ToString() + FString::FromInt(MeshIndex-1);
				UE::Interchange::FAttributeKey NewDepIndexKey(*NewDepIndexKeyString);
				UE::Interchange::EAttributeStorageResult UnregisterResult = Attributes.UnregisterAttribute(DepIndexKey);
				if (UE::Interchange::IsAttributeStorageResultSuccess(UnregisterResult))
				{
					UE::Interchange::EAttributeStorageResult RegisterResult = Attributes.RegisterAttribute<FName>(NewDepIndexKey, MeshUniqueID);
					if (!UE::Interchange::IsAttributeStorageResultSuccess(RegisterResult))
					{
						UE::Interchange::LogAttributeStorageErrors(RegisterResult, TEXT("UInterchangeSkeletalMeshLodDataNode.RemoveTranslatorMeshKey"), NewDepIndexKey);
					}
				}
				else
				{
					UE::Interchange::LogAttributeStorageErrors(UnregisterResult, TEXT("UInterchangeSkeletalMeshLodDataNode.RemoveTranslatorMeshKey"), DepIndexKey);
				}
				//make sure the for loop iterate since HandleMesh is now invalid
				continue;
			}
		}
		return true;
	}

private:

	bool IsEditorOnlyDataDefined()
	{
#if WITH_EDITORONLY_DATA
		return true;
#else
		return false;
#endif
	}

	//SkeletalMesh
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonIDKey = UE::Interchange::FAttributeKey(TEXT("SkeletonID"));

protected:
};
