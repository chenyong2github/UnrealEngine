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
			static const FString& GetMeshUidsBaseKey()
			{
				static FString MeshUids_BaseKey = TEXT("__MeshUids__Key");
				return MeshUids_BaseKey;
			}
		};

	} // namespace Interchange
} // namespace UE


UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeStaticMeshLodDataNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeStaticMeshLodDataNode()
	{
		MeshUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey());
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("StaticMeshLodDataNode");
		return TypeName;
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.Key;
		if (NodeAttributeKey.Key.Equals(UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
		{
			KeyDisplayName = TEXT("Mesh count");
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
		{
			KeyDisplayName = TEXT("Mesh index ");
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

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
		{
			return FString(TEXT("Meshes"));
		}
		return Super::GetAttributeCategory(NodeAttributeKey);
	}

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}


public:
	/* Mesh Uids: It can be either a scene or a mesh node uid. If its a scene it mean we want the mesh factory to bake the geo payload with the global transform of the scene node. */

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	int32 GetMeshUidsCount() const
	{
		return MeshUids.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	void GetMeshUids(TArray<FString>& OutMeshNames) const
	{
		MeshUids.GetNames(OutMeshNames);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	bool AddMeshUid(const FString& MeshName)
	{
		return MeshUids.AddName(MeshName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	bool RemoveMeshUid(const FString& MeshName)
	{
		return MeshUids.RemoveName(MeshName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMeshLodData")
	bool RemoveAllMeshes()
	{
		return MeshUids.RemoveAllNames();
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

	UE::Interchange::FNameAttributeArrayHelper MeshUids;
protected:
};
