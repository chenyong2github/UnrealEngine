// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE
#include "Engine/StaticMesh.h"
#endif

#include "InterchangeStaticMeshFactoryNode.generated.h"


namespace UE
{
	namespace Interchange
	{
		struct FStaticMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& GetLodDependenciesBaseKey()
			{
				static FString LodDependencies_BaseKey = TEXT("Lod_Dependencies");
				return LodDependencies_BaseKey;
			}
		};

	} // namespace Interchange
} // namespace UE


UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeStaticMeshFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeStaticMeshFactoryNode()
	{
#if WITH_ENGINE
		AssetClass = nullptr;
#endif
		LodDependencies.Initialize(Attributes, UE::Interchange::FStaticMeshNodeStaticData::GetLodDependenciesBaseKey());
	}

	/**
	 * Initialize node data
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the StaticMesh factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void InitializeStaticMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
	{
		bIsNodeClassInitialized = false;
		InitializeNode(UniqueID, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_FactoryData);

		FString OperationName = GetTypeName() + TEXT(".SetAssetClassName");
		InterchangePrivateNodeBase::SetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, InAssetClass);
		FillAssetClassFromAttribute();
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);
#if WITH_ENGINE
		if (Ar.IsLoading())
		{
			// Make sure the class is properly set when we compile with engine, this will set the bIsNodeClassInitialized to true.
			SetNodeClassFromClassAttribute();
		}
#endif //#if WITH_ENGINE
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("StaticMeshNode");
		return TypeName;
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.Key;
		if (NodeAttributeKey.Key.Equals(UE::Interchange::FStaticMeshNodeStaticData::GetLodDependenciesBaseKey()))
		{
			KeyDisplayName = TEXT("LOD Dependencies Count");
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FStaticMeshNodeStaticData::GetLodDependenciesBaseKey()))
		{
			KeyDisplayName = TEXT("LOD Dependencies Index ");
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

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	virtual class UClass* GetObjectClass() const override
	{
		ensure(bIsNodeClassInitialized);
#if WITH_ENGINE
		return AssetClass.Get() != nullptr ? AssetClass.Get() : UStaticMesh::StaticClass();
#else
		return nullptr;
#endif
	}

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}

public:
	/** Return The number of LOD this static mesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	int32 GetLodDataCount() const
	{
		return LodDependencies.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const
	{
		LodDependencies.GetNames(OutLodDataUniqueIds);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool AddLodDataUniqueId(const FString& LodDataUniqueId)
	{
		return LodDependencies.AddName(LodDataUniqueId);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool RemoveLodDataUniqueId(const FString& LodDataUniqueId)
	{
		return LodDependencies.RemoveName(LodDataUniqueId);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetCustomVertexColorReplace(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorReplace, bool)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetCustomVertexColorReplace(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorReplace, bool)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetCustomVertexColorIgnore(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorIgnore, bool)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetCustomVertexColorIgnore(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorIgnore, bool)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetCustomVertexColorOverride(FColor& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorOverride, FColor)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetCustomVertexColorOverride(const FColor& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorOverride, FColor)
	}
private:

	void FillAssetClassFromAttribute()
	{
#if WITH_ENGINE
		FString OperationName = GetTypeName() + TEXT(".GetAssetClassName");
		FString ClassName;
		InterchangePrivateNodeBase::GetCustomAttribute<FString>(*Attributes, ClassNameAttributeKey, OperationName, ClassName);
		if (ClassName.Equals(UStaticMesh::StaticClass()->GetName()))
		{
			AssetClass = UStaticMesh::StaticClass();
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

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorReplaceKey = UE::Interchange::FAttributeKey(TEXT("VertexColorReplace"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorIgnoreKey = UE::Interchange::FAttributeKey(TEXT("VertexColorIgnore"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorOverrideKey = UE::Interchange::FAttributeKey(TEXT("VertexColorOverride"));

	UE::Interchange::FNameAttributeArrayHelper LodDependencies;
protected:
#if WITH_ENGINE
	TSubclassOf<UStaticMesh> AssetClass = nullptr;
#endif
	bool bIsNodeClassInitialized = false;
};
