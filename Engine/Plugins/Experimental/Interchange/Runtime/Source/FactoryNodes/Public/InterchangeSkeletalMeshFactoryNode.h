// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeMeshFactoryNode.h"
#include "Nodes/InterchangeFactoryBaseNode.h"

#if WITH_ENGINE
#include "Engine/SkeletalMesh.h"
#endif

#include "InterchangeSkeletalMeshFactoryNode.generated.h"

UENUM(BlueprintType)
enum class EInterchangeSkeletalMeshContentType : uint8
{
	All UMETA(DisplayName = "Geometry and Skinning Weights.", ToolTip = "Import all skelatl mesh content: geometry, skinning and weights."),
	Geometry UMETA(DisplayName = "Geometry Only", ToolTip = "Import the skeletal mesh geometry only (will create a default skeleton, or map the geometry to the existing one). Morph and LOD can be imported with it."),
	SkinningWeights UMETA(DisplayName = "Skinning Weights Only", ToolTip = "Import the skeletal mesh skinning and weights only (no geometry will be imported). Morph and LOD will not be imported with this settings."),
	MAX,
};

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeSkeletalMeshFactoryNode : public UInterchangeMeshFactoryNode
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

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	virtual class UClass* GetObjectClass() const override;

public:
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

	/** Query the skeletal mesh import content type. The content type is use by the factory to import partial or full translated content. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool GetCustomImportContentType(EInterchangeSkeletalMeshContentType& AttributeValue) const;

	/** Set the skeletal mesh import content type. The content type is use by the factory to import partial or full translated content. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMesh")
	bool SetCustomImportContentType(const EInterchangeSkeletalMeshContentType& AttributeValue);

	/**
	 * The skeletal mesh thumbnail can have an overlay if the last re-import was geometry only. This thumbnail overlay feature use the metadata to find out if the last import was geometry only.
	 */
	virtual void AppendAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
private:

	virtual void FillAssetClassFromAttribute() override;
	virtual bool SetNodeClassFromClassAttribute() override;

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();
	const UE::Interchange::FAttributeKey Macro_CustomImportMorphTargetKey = UE::Interchange::FAttributeKey(TEXT("ImportMorphTarget"));
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("SkeletonSoftObjectPath"));
	const UE::Interchange::FAttributeKey Macro_CustomCreatePhysicsAssetKey = UE::Interchange::FAttributeKey(TEXT("CreatePhysicsAsset"));
	const UE::Interchange::FAttributeKey Macro_CustomPhysicAssetSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("PhysicAssetSoftObjectPath"));
	const UE::Interchange::FAttributeKey Macro_CustomImportContentTypeKey = UE::Interchange::FAttributeKey(TEXT("ImportContentType"));

protected:
#if WITH_ENGINE
	TSubclassOf<USkeletalMesh> AssetClass = nullptr;
#endif
};
