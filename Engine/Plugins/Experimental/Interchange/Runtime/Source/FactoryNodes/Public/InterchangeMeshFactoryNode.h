// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeFactoryBaseNode.h"

#include "InterchangeMeshFactoryNode.generated.h"


namespace UE::Interchange
{
	struct INTERCHANGEFACTORYNODES_API FMeshFactoryNodeStaticData : public FBaseNodeStaticData
	{
		static const FAttributeKey& GetLodDependenciesBaseKey();
		static const FAttributeKey& GetSlotMaterialDependencyBaseKey();
	};
} // namespace Interchange


UCLASS(BlueprintType, Experimental, Abstract)
class INTERCHANGEFACTORYNODES_API UInterchangeMeshFactoryNode : public UInterchangeFactoryBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMeshFactoryNode();

	/**
	 * Override serialize to restore SlotMaterialDependencies on load.
	 */
	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);

		if (Ar.IsLoading() && bIsInitialized)
		{
			SlotMaterialDependencies.RebuildCache();
#if WITH_ENGINE
			// Make sure the class is properly set when we compile with engine, this will set the bIsNodeClassInitialized to true.
			SetNodeClassFromClassAttribute();
#endif
		}
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

public:
	/** Return The number of LOD this static mesh has.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	int32 GetLodDataCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	void GetLodDataUniqueIds(TArray<FString>& OutLodDataUniqueIds) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool AddLodDataUniqueId(const FString& LodDataUniqueId);

	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool RemoveLodDataUniqueId(const FString& LodDataUniqueId);

	/** Query weather the static mesh factory should replace the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool GetCustomVertexColorReplace(bool& AttributeValue) const;

	/** Set weather the static mesh factory should replace the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool SetCustomVertexColorReplace(const bool& AttributeValue);

	/** Query weather the static mesh factory should ignore the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool GetCustomVertexColorIgnore(bool& AttributeValue) const;

	/** Set weather the static mesh factory should ignore the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool SetCustomVertexColorIgnore(const bool& AttributeValue);

	/** Query weather the static mesh factory should override the vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool GetCustomVertexColorOverride(FColor& AttributeValue) const;

	/** Set weather the static mesh factory should override the vertex color. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool SetCustomVertexColorOverride(const FColor& AttributeValue);

	/** Allow to retrieve the correspondence table between slot names and assigned materials for this object. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	void GetSlotMaterialDependencies(TMap<FString, FString>& OutMaterialDependencies) const;

	/** Allow to retrieve one Material dependency for a given slot of this object. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool GetSlotMaterialDependencyUid(const FString& SlotName, FString& OutMaterialDependency) const;

	/** Add one Material dependency to a specific slot name of this object. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool SetSlotMaterialDependencyUid(const FString& SlotName, const FString& MaterialDependencyUid);

	/** Remove the Material dependency associated with the given slot name from this object. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | FactoryNode | Mesh")
	bool RemoveSlotMaterialDependencyUid(const FString& SlotName);

private:

	const UE::Interchange::FAttributeKey Macro_CustomVertexColorReplaceKey = UE::Interchange::FAttributeKey(TEXT("VertexColorReplace"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorIgnoreKey = UE::Interchange::FAttributeKey(TEXT("VertexColorIgnore"));
	const UE::Interchange::FAttributeKey Macro_CustomVertexColorOverrideKey = UE::Interchange::FAttributeKey(TEXT("VertexColorOverride"));

	UE::Interchange::TArrayAttributeHelper<FString> LodDependencies;
	UE::Interchange::TMapAttributeHelper<FString, FString> SlotMaterialDependencies;

protected:
	virtual void FillAssetClassFromAttribute() PURE_VIRTUAL("FillAssetClassFromAttribute");
	virtual bool SetNodeClassFromClassAttribute() PURE_VIRTUAL("SetNodeClassFromClassAttribute", return false;);

	const UE::Interchange::FAttributeKey ClassNameAttributeKey = UE::Interchange::FBaseNodeStaticData::ClassTypeAttributeKey();

	bool bIsNodeClassInitialized = false;
};
