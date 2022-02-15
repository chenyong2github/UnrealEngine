// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeFactoryBaseNode.h"

#if WITH_ENGINE
#include "Engine/StaticMesh.h"
#endif

#include "InterchangeStaticMeshFactoryNode.generated.h"


namespace UE
{
	namespace Interchange
	{
		struct INTERCHANGEFACTORYNODES_API FStaticMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FString& GetLodDependenciesBaseKey();
		};
	} // namespace Interchange
} // namespace UE


UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeStaticMeshFactoryNode : public UInterchangeFactoryBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeStaticMeshFactoryNode();

	/**
	 * Initialize node data
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 * @param InAssetClass - The class the StaticMesh factory will create for this node.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void InitializeStaticMeshNode(const FString& UniqueID, const FString& DisplayLabel, const FString& InAssetClass);

	virtual void Serialize(FArchive& Ar) override;

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	virtual class UClass* GetObjectClass() const override;

public:
	/** Return The number of LOD this static mesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	int32 GetLodDataCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool AddLodDataUniqueId(const FString& LodDataUniqueId);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool RemoveLodDataUniqueId(const FString& LodDataUniqueId);

	/** Query weather the static mesh factory should replace the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetCustomVertexColorReplace(bool& AttributeValue) const;

	/** Set weather the static mesh factory should replace the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetCustomVertexColorReplace(const bool& AttributeValue);

	/** Query weather the static mesh factory should ignore the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetCustomVertexColorIgnore(bool& AttributeValue) const;

	/** Set weather the static mesh factory should ignore the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetCustomVertexColorIgnore(const bool& AttributeValue);

	/** Query weather the static mesh factory should override the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetCustomVertexColorOverride(FColor& AttributeValue) const;

	/** Set weather the static mesh factory should override the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetCustomVertexColorOverride(const FColor& AttributeValue);

private:

	void FillAssetClassFromAttribute();
	bool SetNodeClassFromClassAttribute();
	bool IsEditorOnlyDataDefined();

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorReplaceKey = UE::Interchange::FAttributeKey(TEXT("VertexColorReplace"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorIgnoreKey = UE::Interchange::FAttributeKey(TEXT("VertexColorIgnore"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorOverrideKey = UE::Interchange::FAttributeKey(TEXT("VertexColorOverride"));

	UE::Interchange::TArrayAttributeHelper<FString> LodDependencies;
protected:
#if WITH_ENGINE
	TSubclassOf<UStaticMesh> AssetClass = nullptr;
#endif
	bool bIsNodeClassInitialized = false;
};
