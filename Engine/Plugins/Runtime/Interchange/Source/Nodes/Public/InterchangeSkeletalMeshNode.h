// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#endif

#include "InterchangeSkeletalMeshNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FSkeletalMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& LodDataIdsCountKey()
			{
				static FAttributeKey AttributeKey(TEXT("__LOD_DATA_IDS_COUNT_"));
				return AttributeKey;
			}

			static const FAttributeKey& LodDataIdsBaseKey()
			{
				static FAttributeKey AttributeKey(TEXT("__LOD_DATA_IDS_INDEX_"));
				return AttributeKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType)
class INTERCHANGENODEPLUGIN_API UInterchangeSkeletalMeshNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSkeletalMeshNode()
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
	 * @param InAssetClass - The class the SkeletalMesh factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void InitializeSkeletalMeshNode(const FName& UniqueID, const FName& DisplayLabel, const FString& InAssetClass)
	{
		bIsNodeClassInitialized = false;
		InitializeNode(UniqueID, DisplayLabel);
		FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
		InterchangePrivateNodeBase::SetCustomAttribute<FString>(Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
		FillAssetClassFromAttribute();
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);
#if WITH_ENGINE
		if (Ar.IsLoading())
		{
			//Make sure the class is properly set when we compile with engine, this will set the
			//bIsNodeClassInitialized to true.
			SetNodeClassFromClassAttribute();
		}
#endif //#if WITH_ENGINE
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("SkeletalMeshNode");
		return TypeName;
	}

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	virtual class UClass* GetAssetClass() const override
	{
		ensure(bIsNodeClassInitialized);
#if WITH_ENGINE
		return AssetClass.Get() != nullptr ? AssetClass.Get() : USkeletalMesh::StaticClass();
#else
		return nullptr;
#endif
	}

	virtual FGuid GetHash() const override
	{
		return Attributes.GetStorageHash();
	}

public:
	/** Return The number of LOD this skeletalmesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	int32 GetLodDataCount() const
	{
		int32 LodCount = 0;
		if (Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey()))
		{
			UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey());
			if (Handle.IsValid())
			{
				Handle.Get(LodCount);
			}
		}
		return LodCount;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void GetLodDataUniqueIds(TArray<FName>& OutLodDataUniqueIds) const
	{
		OutLodDataUniqueIds.Reset();

		int32 LodCount = 0;
		if (!Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey()))
		{
			return;
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey());
		if (!Handle.IsValid())
		{
			return;
		}
		Handle.Get(LodCount);
		for (int32 LODIndex = 0; LODIndex < LodCount; ++LODIndex)
		{
			FString DepIndexKeyString = UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsBaseKey().ToString() + FString::FromInt(LODIndex);
			UE::Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
			UE::Interchange::FAttributeStorage::TAttributeHandle<FName> HandleLOD = Attributes.GetAttributeHandle<FName>(DepIndexKey);
			if (!HandleLOD.IsValid())
			{
				continue;
			}
			FName& NodeUniqueID = OutLodDataUniqueIds.AddDefaulted_GetRef();
			HandleLOD.Get(NodeUniqueID);
		}
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool AddLodDataUniqueId(FName LodDataUniqueId)
	{
		if (!Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey()))
		{
			const int32 DependencyCount = 0;
			UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute<int32>(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey(), DependencyCount);
			if (!UE::Interchange::IsAttributeStorageResultSuccess(Result))
			{
				UE::Interchange::LogAttributeStorageErrors(Result, TEXT("UInterchangeSkeletalMeshNode.AddLodDataUniqueId"), UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey());
				return false;
			}
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey());
		if (!ensure(Handle.IsValid()))
		{
			return false;
		}
		int32 LODIndex = 0;
		Handle.Get(LODIndex);
		FString LodIndexKeyString = UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsBaseKey().ToString() + FString::FromInt(LODIndex);
		UE::Interchange::FAttributeKey LodIndexKey(*LodIndexKeyString);
		//Increment the counter
		LODIndex++;
		Handle.Set(LODIndex);

		UE::Interchange::EAttributeStorageResult AddLodResult = Attributes.RegisterAttribute<FName>(LodIndexKey, LodDataUniqueId);
		if (!UE::Interchange::IsAttributeStorageResultSuccess(AddLodResult))
		{
			UE::Interchange::LogAttributeStorageErrors(AddLodResult, TEXT("UInterchangeSkeletalMeshNode.AddLodDataUniqueId"), LodIndexKey);
			return false;
		}
		return true;
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool RemoveLodDataUniqueId(FName LodDataUniqueId)
	{
		int32 LodCount = 0;
		if (!Attributes.ContainAttribute(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey()))
		{
			return false;
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsCountKey());
		if (!Handle.IsValid())
		{
			return false;
		}
		Handle.Get(LodCount);
		bool DecrementKey = false;
		for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
		{
			FString DepIndexKeyString = UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsBaseKey().ToString() + FString::FromInt(LodIndex);
			UE::Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
			UE::Interchange::FAttributeStorage::TAttributeHandle<FName> HandleMesh = Attributes.GetAttributeHandle<FName>(DepIndexKey);
			if (!HandleMesh.IsValid())
			{
				continue;
			}
			FName MeshUniqueID;
			HandleMesh.Get(MeshUniqueID);
			if (MeshUniqueID == LodDataUniqueId)
			{
				//Remove this entry
				Attributes.UnregisterAttribute(DepIndexKey);
				Handle.Set(LodCount - 1);
				//We have to rename the key for all the next item
				DecrementKey = true;;
			}
			else if (DecrementKey)
			{
				FString NewDepIndexKeyString = UE::Interchange::FSkeletalMeshNodeStaticData::LodDataIdsBaseKey().ToString() + FString::FromInt(LodIndex - 1);
				UE::Interchange::FAttributeKey NewDepIndexKey(*NewDepIndexKeyString);
				UE::Interchange::EAttributeStorageResult UnregisterResult = Attributes.UnregisterAttribute(DepIndexKey);
				if (UE::Interchange::IsAttributeStorageResultSuccess(UnregisterResult))
				{
					UE::Interchange::EAttributeStorageResult RegisterResult = Attributes.RegisterAttribute<FName>(NewDepIndexKey, MeshUniqueID);
					if (!UE::Interchange::IsAttributeStorageResultSuccess(RegisterResult))
					{
						UE::Interchange::LogAttributeStorageErrors(RegisterResult, TEXT("UInterchangeSkeletalMeshNode.RemoveLodDataUniqueId"), NewDepIndexKey);
					}
				}
				else
				{
					UE::Interchange::LogAttributeStorageErrors(UnregisterResult, TEXT("UInterchangeSkeletalMeshNode.RemoveLodDataUniqueId"), DepIndexKey);
				}

				//Avoid doing more code in the for since the HandleMesh is now invalid
				continue;
			}
		}
		return true;
	}
private:

	void FillAssetClassFromAttribute()
	{
#if WITH_ENGINE
		FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
		FString ClassName;
		InterchangePrivateNodeBase::GetCustomAttribute<FString>(Attributes, ClassNameAttributeKey, OperationName, ClassName);
		if (ClassName.Equals(USkeletalMesh::StaticClass()->GetName()))
		{
			AssetClass = USkeletalMesh::StaticClass();
			bIsNodeClassInitialized = true;
		}
#endif
	}

	bool SetNodeClassFromClassAttribute()
	{
		if (!bIsNodeClassInitialized)
		{
			FillAssetClassFromAttribute();
		}
		return bIsNodeClassInitialized;
	}

	bool IsEditorOnlyDataDefined()
	{
#if WITH_EDITORONLY_DATA
		return true;
#else
		return false;
#endif
	}

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FAttributeKey(TEXT("__ClassTypeAttribute__"));

protected:
#if WITH_ENGINE
	TSubclassOf<USkeletalMesh> AssetClass = nullptr;
#endif
	bool bIsNodeClassInitialized = false;
};
