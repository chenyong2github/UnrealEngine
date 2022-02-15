// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeFactoryBaseNode.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#endif

#include "InterchangeSkeletalMeshFactoryNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct INTERCHANGEFACTORYNODES_API FSkeletalMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& GetLodDependenciesBaseKey();
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeSkeletalMeshFactoryNode : public UInterchangeFactoryBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSkeletalMeshFactoryNode();

	/**
	 * Initialize node data
	 * @param: UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the SkeletalMesh factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void InitializeSkeletalMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass);

	virtual void Serialize(FArchive& Ar) override;

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	virtual class UClass* GetObjectClass() const override;

public:
	/** Return The number of LOD this skeletalmesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	int32 GetLodDataCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	void GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool AddLodDataUniqueId(const FString& LodDataUniqueId);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool RemoveLodDataUniqueId(const FString& LodDataUniqueId);
	
	/** Query the skeletal mesh factory skeleton UObject. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomSkeletonSoftObjectPath(FSoftObjectPath& AttributeValue) const;

	/** Set the skeletal mesh factory skeleton UObject. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomSkeletonSoftObjectPath(const FSoftObjectPath& AttributeValue);

	/** Query weather the skeletal mesh factory should create the morph target. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomImportMorphTarget(bool& AttributeValue) const;

	/** Set weather the skeletal mesh factory should create the morph target. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomImportMorphTarget(const bool& AttributeValue);

	/** Query weather the skeletal mesh factory should create a physics asset. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomCreatePhysicsAsset(bool& AttributeValue) const;

	/** Set weather the skeletal mesh factory should create a physics asset. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomCreatePhysicsAsset(const bool& AttributeValue);

	/** Query a physics asset the skeletal mesh factory should use. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomPhysicAssetSoftObjectPath(FSoftObjectPath& AttributeValue) const;

	/** Set a physics asset the skeletal mesh factory should use. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomPhysicAssetSoftObjectPath(const FSoftObjectPath& AttributeValue);

	/** Query weather the skeletal mesh factory should replace the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomVertexColorReplace(bool& AttributeValue) const;

	/** Set weather the skeletal mesh factory should replace the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomVertexColorReplace(const bool& AttributeValue);

	/** Query weather the skeletal mesh factory should ignore the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomVertexColorIgnore(bool& AttributeValue) const;

	/** Set weather the skeletal mesh factory should ignore the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomVertexColorIgnore(const bool& AttributeValue);

	/** Query weather the skeletal mesh factory should override the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomVertexColorOverride(FColor& AttributeValue) const;

	/** Set weather the skeletal mesh factory should override the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomVertexColorOverride(const FColor& AttributeValue);
private:

	void FillAssetClassFromAttribute();

	bool SetNodeClassFromClassAttribute();

	bool IsEditorOnlyDataDefined();

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();
	const UE::Interchange::FAttributeKey Macro_CustomImportMorphTargetKey = UE::Interchange::FAttributeKey(TEXT("ImportMorphTarget"));
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("SkeletonSoftObjectPath"));
	const UE::Interchange::FAttributeKey Macro_CustomCreatePhysicsAssetKey = UE::Interchange::FAttributeKey(TEXT("CreatePhysicsAsset"));
	const UE::Interchange::FAttributeKey Macro_CustomPhysicAssetSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("PhysicAssetSoftObjectPath"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorReplaceKey = UE::Interchange::FAttributeKey(TEXT("VertexColorReplace"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorIgnoreKey = UE::Interchange::FAttributeKey(TEXT("VertexColorIgnore"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorOverrideKey = UE::Interchange::FAttributeKey(TEXT("VertexColorOverride"));

	UE::Interchange::TArrayAttributeHelper<FString> LodDependencies;
protected:
#if WITH_ENGINE
	TSubclassOf<USkeletalMesh> AssetClass = nullptr;
#endif
	bool bIsNodeClassInitialized = false;
};
