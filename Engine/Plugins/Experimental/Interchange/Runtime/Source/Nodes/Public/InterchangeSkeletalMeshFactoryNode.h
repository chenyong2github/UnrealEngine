// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#endif

#include "InterchangeSkeletalMeshFactoryNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FSkeletalMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& GetLodDependenciesBaseKey()
			{
				static FString LodDependencies_BaseKey = TEXT("Lod_Dependencies");
				return LodDependencies_BaseKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeSkeletalMeshFactoryNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSkeletalMeshFactoryNode()
		:UInterchangeBaseNode()
	{
#if WITH_ENGINE
		AssetClass = nullptr;
#endif
		LodDependencies.Initialize(Attributes, UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey());
	}

	/**
	 * Initialize node data
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the SkeletalMesh factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void InitializeSkeletalMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass)
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

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.Key;
		if (NodeAttributeKey.Key.Equals(UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey()))
		{
			KeyDisplayName = TEXT("LOD Dependencies Count");
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey()))
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
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	virtual class UClass* GetObjectClass() const override
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
		return Attributes->GetStorageHash();
	}

public:
	/** Return The number of LOD this skeletalmesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	int32 GetLodDataCount() const
	{
		return LodDependencies.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const
	{
		LodDependencies.GetNames(OutLodDataUniqueIds);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool AddLodDataUniqueId(const FString& LodDataUniqueId)
	{
		return LodDependencies.AddName(LodDataUniqueId);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool RemoveLodDataUniqueId(const FString& LodDataUniqueId)
	{
		return LodDependencies.RemoveName(LodDataUniqueId);
	}
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomSkeletonSoftObjectPath(FSoftObjectPath& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonSoftObjectPath, FSoftObjectPath)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomSkeletonSoftObjectPath(const FSoftObjectPath& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonSoftObjectPath, FSoftObjectPath)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomImportMorphTarget(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(ImportMorphTarget, bool)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomImportMorphTarget(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(ImportMorphTarget, bool)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomCreatePhysicsAsset(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(CreatePhysicsAsset, bool)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomCreatePhysicsAsset(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(CreatePhysicsAsset, bool)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomPhysicAssetSoftObjectPath(FSoftObjectPath& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(PhysicAssetSoftObjectPath, FSoftObjectPath)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomPhysicAssetSoftObjectPath(const FSoftObjectPath& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(PhysicAssetSoftObjectPath, FSoftObjectPath)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomVertexColorReplace(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorReplace, bool)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomVertexColorReplace(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorReplace, bool)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomVertexColorIgnore(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorIgnore, bool)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomVertexColorIgnore(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexColorIgnore, bool)
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomVertexColorOverride(FColor& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexColorOverride, FColor)
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
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

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();
	const UE::Interchange::FAttributeKey Macro_CustomImportMorphTargetKey = UE::Interchange::FAttributeKey(TEXT("ImportMorphTarget"));
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("SkeletonSoftObjectPath"));
	const UE::Interchange::FAttributeKey Macro_CustomCreatePhysicsAssetKey = UE::Interchange::FAttributeKey(TEXT("CreatePhysicsAsset"));
	const UE::Interchange::FAttributeKey Macro_CustomPhysicAssetSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("PhysicAssetSoftObjectPath"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorReplaceKey = UE::Interchange::FAttributeKey(TEXT("VertexColorReplace"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorIgnoreKey = UE::Interchange::FAttributeKey(TEXT("VertexColorIgnore"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorOverrideKey = UE::Interchange::FAttributeKey(TEXT("VertexColorOverride"));

	UE::Interchange::FNameAttributeArrayHelper LodDependencies;
protected:
#if WITH_ENGINE
	TSubclassOf<USkeletalMesh> AssetClass = nullptr;
#endif
	bool bIsNodeClassInitialized = false;
};
