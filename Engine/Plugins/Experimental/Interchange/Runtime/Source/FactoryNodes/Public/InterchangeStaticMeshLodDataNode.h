// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeStaticMeshLodDataNode.generated.h"


namespace UE
{
	namespace Interchange
	{
		struct FStaticMeshNodeLodDataStaticData : public FBaseNodeStaticData
		{
			static const FString& GetMeshUidsBaseKey();
		};
	} // namespace Interchange
} // namespace UE


UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeStaticMeshLodDataNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeStaticMeshLodDataNode();

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

public:
	/* Mesh Uids: It can be either a scene or a mesh node uid. If its a scene it mean we want the mesh factory to bake the geo payload with the global transform of the scene node. */

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	int32 GetMeshUidsCount() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	void GetMeshUids(TArray<FString>& OutMeshNames) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	bool AddMeshUid(const FString& MeshName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	bool RemoveMeshUid(const FString& MeshName);

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	bool RemoveAllMeshes();

private:

	bool IsEditorOnlyDataDefined();

	UE::Interchange::TArrayAttributeHelper<FString> MeshUids;
protected:
};
