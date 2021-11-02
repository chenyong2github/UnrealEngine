// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeSkeletalMeshLodDataNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FSkeletalMeshNodeLodDataStaticData : public FBaseNodeStaticData
		{
			static const FString& GetMeshUidsBaseKey()
			{
				static FString MeshUids_BaseKey = TEXT("__MeshUids__Key");
				return MeshUids_BaseKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeSkeletalMeshLodDataNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeSkeletalMeshLodDataNode()
		:UInterchangeBaseNode()
	{
		MeshUids.Initialize(Attributes, UE::Interchange::FSkeletalMeshNodeLodDataStaticData::GetMeshUidsBaseKey());
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("SkeletalMeshLodDataNode");
		return TypeName;
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.Key;
		if (NodeAttributeKey.Key.Equals(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
		{
			KeyDisplayName = TEXT("Mesh count");
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
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
		else if (NodeAttributeKey == Macro_CustomSkeletonUidKey)
		{
			KeyDisplayName = TEXT("Skeleton factory node");
			return KeyDisplayName;
		}
		return Super::GetKeyDisplayName(NodeAttributeKey);
	}

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FSkeletalMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
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
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool GetCustomSkeletonUid(FString& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(SkeletonUid, FString);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool SetCustomSkeletonUid(const FString& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(SkeletonUid, FString)
	}

	/* Mesh Uids: It can be either a scene or a mesh node uid. If its a scene it mean we want the mesh factory to bake the geo payload with the global transform of the scene node. */

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	int32 GetMeshUidsCount() const
	{
		return MeshUids.GetCount();
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	void GetMeshUids(TArray<FString>& OutBlendShapeNames) const
	{
		MeshUids.GetNames(OutBlendShapeNames);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool AddMeshUid(const FString& BlendShapeName)
	{
		return MeshUids.AddName(BlendShapeName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
	bool RemoveMeshUid(const FString& BlendShapeName)
	{
		return MeshUids.RemoveName(BlendShapeName);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | SkeletalMeshLodData")
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

	//SkeletalMesh
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonUidKey = UE::Interchange::FAttributeKey(TEXT("__SkeletonUid__Key"));

	UE::Interchange::FNameAttributeArrayHelper MeshUids;
protected:
};
