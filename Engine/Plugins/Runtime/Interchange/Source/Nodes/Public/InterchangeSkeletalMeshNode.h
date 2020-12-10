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
			static const FString& GetLodDependenciesBaseKey()
			{
				static FString LodDependencies_BaseKey = TEXT("Lod_Dependencies");
				return LodDependencies_BaseKey;
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
		LodDependencies.Initialize(Attributes, UE::Interchange::FSkeletalMeshNodeStaticData::GetLodDependenciesBaseKey());
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
	void GetLodDataUniqueIds(TArray<FName>& OutLodDataUniqueIds) const
	{
		LodDependencies.GetNames(OutLodDataUniqueIds);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool AddLodDataUniqueId(FName LodDataUniqueId)
	{
		return LodDependencies.AddName(LodDataUniqueId);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool RemoveLodDataUniqueId(FName LodDataUniqueId)
	{
		return LodDependencies.RemoveName(LodDataUniqueId);
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

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FAttributeKey(TEXT("__ClassTypeAttribute__"));

	UE::Interchange::FNameAttributeArrayHelper LodDependencies;
protected:
#if WITH_ENGINE
	TSubclassOf<USkeletalMesh> AssetClass = nullptr;
#endif
	bool bIsNodeClassInitialized = false;
};
