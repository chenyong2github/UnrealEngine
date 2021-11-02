// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNode.h"

#include "InterchangeMeshNode.generated.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		struct FMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& PayloadSourceFileKey()
			{
				static FAttributeKey AttributeKey(TEXT("__PayloadSourceFile__"));
				return AttributeKey;
			}

			static const FAttributeKey& IsSkinnedMeshKey()
			{
				static FAttributeKey AttributeKey(TEXT("__IsSkinnedMeshKey__"));
				return AttributeKey;
			}

			static const FAttributeKey& IsBlendShapeKey()
			{
				static FAttributeKey AttributeKey(TEXT("__IsBlendShapeKey__"));
				return AttributeKey;
			}

			static const FAttributeKey& BlendShapeNameKey()
			{
				static FAttributeKey AttributeKey(TEXT("__BlendShapeNameKey__"));
				return AttributeKey;
			}
			static const FString& GetSkeletonDependenciesKey()
			{
				static FString Dependencies_BaseKey = TEXT("__MeshSkeletonDependencies__");
				return Dependencies_BaseKey;
			}
			static const FString& GetMaterialDependenciesKey()
			{
				static FString Dependencies_BaseKey = TEXT("__MeshMaterialDependencies__");
				return Dependencies_BaseKey;
			}

			static const FString& GetShapeDependenciesKey()
			{
				static FString Dependencies_BaseKey = TEXT("__MeshShapeDependencies__");
				return Dependencies_BaseKey;
			}

			static const FString& GetSceneInstancesUidsKey()
			{
				static FString SceneInstanceUids_BaseKey = TEXT("__MeshSceneInstancesUids__");
				return SceneInstanceUids_BaseKey;
			}
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeMeshNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMeshNode()
		:UInterchangeBaseNode()
	{
		SkeletonDependencies.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetSkeletonDependenciesKey());
		MaterialDependencies.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetMaterialDependenciesKey());
		ShapeDependencies.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetShapeDependenciesKey());
		SceneInstancesUids.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetSceneInstancesUidsKey());
	}

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override
	{
		FString KeyDisplayName = NodeAttributeKey.ToString();
		if (NodeAttributeKey == UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey())
		{
			return KeyDisplayName = TEXT("Payload Source Key");
		}
		else if (NodeAttributeKey == UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey())
		{
			return KeyDisplayName = TEXT("Is a Skinned Mesh");
		}
		else if (NodeAttributeKey == UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey())
		{
			return KeyDisplayName = TEXT("Is a Blend Shape");
		}
		else if (NodeAttributeKey == UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey())
		{
			return KeyDisplayName = TEXT("Blend Shape Name");
		}
		else if (NodeAttributeKey.Key.Equals(UE::Interchange::FMeshNodeStaticData::GetSkeletonDependenciesKey()))
		{
			return KeyDisplayName = TEXT("Skeleton Dependencies count");
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetSkeletonDependenciesKey()))
		{
			KeyDisplayName = TEXT("Skeleton Dependencies Index ");
			const FString IndexKey = UE::Interchange::FNameAttributeArrayHelper::IndexKey();
			int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
			if (IndexPosition < NodeAttributeKey.Key.Len())
			{
				KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
			}
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.Equals(UE::Interchange::FMeshNodeStaticData::GetMaterialDependenciesKey()))
		{
			return KeyDisplayName = TEXT("Material Dependencies count");
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetMaterialDependenciesKey()))
		{
			KeyDisplayName = TEXT("Material Dependencies Index ");
			const FString IndexKey = UE::Interchange::FNameAttributeArrayHelper::IndexKey();
			int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
			if (IndexPosition < NodeAttributeKey.Key.Len())
			{
				KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
			}
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.Equals(UE::Interchange::FMeshNodeStaticData::GetShapeDependenciesKey()))
		{
			return KeyDisplayName = TEXT("Shape Dependencies count");
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetShapeDependenciesKey()))
		{
			KeyDisplayName = TEXT("Shape Dependencies Index ");
			const FString IndexKey = UE::Interchange::FNameAttributeArrayHelper::IndexKey();
			int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
			if (IndexPosition < NodeAttributeKey.Key.Len())
			{
				KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
			}
			return KeyDisplayName;
		}
		else if (NodeAttributeKey.Key.Equals(UE::Interchange::FMeshNodeStaticData::GetSceneInstancesUidsKey()))
		{
			return KeyDisplayName = TEXT("Scene mesh instances count");
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetSceneInstancesUidsKey()))
		{
			KeyDisplayName = TEXT("Scene mesh instances Index ");
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
		if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetSkeletonDependenciesKey()))
		{
			return FString(TEXT("SkeletonDependencies"));
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetMaterialDependenciesKey()))
		{
			return FString(TEXT("MaterialDependencies"));
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetShapeDependenciesKey()))
		{
			return FString(TEXT("ShapeDependencies"));
		}
		else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FMeshNodeStaticData::GetSceneInstancesUidsKey()))
		{
			return FString(TEXT("SceneInstances"));
		}
		else if (NodeAttributeKey == Macro_CustomVertexCountKey
				 || NodeAttributeKey == Macro_CustomPolygonCountKey
				 || NodeAttributeKey == Macro_CustomBoundingBoxKey
				 || NodeAttributeKey == Macro_CustomHasVertexNormalKey
				 || NodeAttributeKey == Macro_CustomHasVertexBinormalKey
				 || NodeAttributeKey == Macro_CustomHasVertexTangentKey
				 || NodeAttributeKey == Macro_CustomHasSmoothGroupKey
				 || NodeAttributeKey == Macro_CustomHasVertexColorKey
				 || NodeAttributeKey == Macro_CustomUVCountKey)
		{
			return FString(TEXT("MeshInfo"));
		}
		return Super::GetAttributeCategory(NodeAttributeKey);
	}

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override
	{
		const FString TypeName = TEXT("MeshNode");
		return TypeName;
	}

	virtual FGuid GetHash() const override
	{
		return Attributes->GetStorageHash();
	}

	/**
	 * Icon name are simply create by adding "InterchangeIcon_" in front of the specialized type. If there is no special type the function will return NAME_None which will use the default icon.
	 */
	virtual FName GetIconName() const override
	{
		FString MeshIconName = TEXT("MeshIcon.");
		if (IsSkinnedMesh())
		{
			MeshIconName += TEXT("Skinned");
		}
		else
		{
			MeshIconName += TEXT("Static");
		}
		
		return FName(*MeshIconName);
	}

	/**
	 * Return true if this node represent a skinned mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool IsSkinnedMesh() const
	{
		if (!Attributes->ContainAttribute(UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey()))
		{
			return false;
		}

		UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey());
		if (Handle.IsValid())
		{
			bool bValue = false;
			Handle.Get(bValue);
			return bValue;
		}
		return false;
	}

	/**
	 * Set the IsSkinnedMesh attribute to determine if this node represent a skinned mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetSkinnedMesh(const bool bIsSkinnedMesh)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey(), bIsSkinnedMesh);
		if (IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey());
			return Handle.IsValid();
		}
		return false;
	}

	/**
	 * Return true if this node represent a skinned mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool IsBlendShape() const
	{
		if (!Attributes->ContainAttribute(UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey()))
		{
			return false;
		}

		UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey());
		if (Handle.IsValid())
		{
			bool bValue = false;
			Handle.Get(bValue);
			return bValue;
		}
		return false;
	}

	/**
	 * Set the IsBlendShape attribute to determine if this node represent a blend shape.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetBlendShape(const bool bIsBlendShape)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey(), bIsBlendShape);
		if (IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey());
			return Handle.IsValid();
		}
		return false;
	}

	/**
	 * Return true if we successfully query the BlendShapeName attribute
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetBlendShapeName(FString& OutBlendShapeName) const
	{
		OutBlendShapeName.Empty();
		if (!Attributes->ContainAttribute(UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey()))
		{
			return false;
		}

		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey());
		if (Handle.IsValid())
		{
			Handle.Get(OutBlendShapeName);
			return true;
		}
		return false;
	}

	/**
	 * Set the BlendShapeName attribute to determine if this node represent a blend shape.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetBlendShapeName(const FString& BlendShapeName)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey(), BlendShapeName);
		if (IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey());
			return Handle.IsValid();
		}
		return false;
	}

	/** Mesh node Interface Begin */
	virtual const TOptional<FString> GetPayLoadKey() const
	{
		if (!Attributes->ContainAttribute(UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey()))
		{
			return TOptional<FString>();
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> AttributeHandle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey());
		if (!AttributeHandle.IsValid())
		{
			return TOptional<FString>();
		}
		FString PayloadKey;
		UE::Interchange::EAttributeStorageResult Result = AttributeHandle.Get(PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeMeshNode.GetPayLoadKey"), UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey());
			return TOptional<FString>();
		}
		return TOptional<FString>(PayloadKey);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	virtual void SetPayLoadKey(const FString& PayloadKey)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey(), PayloadKey);
		if (!IsAttributeStorageResultSuccess(Result))
		{
			LogAttributeStorageErrors(Result, TEXT("UInterchangeMeshNode.SetPayLoadKey"), UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey());
		}
	}
	
	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomVertexCount(int32& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexCount, int32);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomVertexCount(const int32& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexCount, int32);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomPolygonCount(int32& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(PolygonCount, int32);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomPolygonCount(const int32& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(PolygonCount, int32);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomBoundingBox(FBox& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(BoundingBox, FBox);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomBoundingBox(const FBox& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(BoundingBox, FBox);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexNormal(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexNormal, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexNormal(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexNormal, bool);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexBinormal(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexBinormal, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexBinormal(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexBinormal, bool);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexTangent(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexTangent, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexTangent(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexTangent, bool);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasSmoothGroup(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasSmoothGroup, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasSmoothGroup(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasSmoothGroup, bool);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexColor(bool& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexColor, bool);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexColor(const bool& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexColor, bool);
	}

	/** Return false if the Attribute was not set previously.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomUVCount(int32& AttributeValue) const
	{
		IMPLEMENT_NODE_ATTRIBUTE_GETTER(UVCount, int32);
	}

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomUVCount(const int32& AttributeValue)
	{
		IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(UVCount, int32);
	}

	/**
	 * This function allow to retrieve the number of skeleton dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetSkeletonDependeciesCount() const
	{
		return SkeletonDependencies.GetCount();
	}

	/**
	 * This function allow to retrieve the skeleton dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSkeletonDependencies(TArray<FString>& OutDependencies) const
	{
		SkeletonDependencies.GetNames(OutDependencies);
	}

	/**
	 * This function allow to retrieve one skeleton dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSkeletonDependency(const int32 Index, FString& OutDependency) const
	{
		SkeletonDependencies.GetName(Index, OutDependency);
	}

	/**
	 * Add one skeleton dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetSkeletonDependencyUid(const FString& DependencyUid)
	{ 
		return SkeletonDependencies.AddName(DependencyUid);
	}

	/**
	 * Remove one skeleton dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveSkeletonDependencyUid(const FString& DependencyUid)
	{
		return SkeletonDependencies.RemoveName(DependencyUid);
	}

	/**
	 * This function allow to retrieve the number of Material dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetMaterialDependeciesCount() const
	{
		return MaterialDependencies.GetCount();
	}

	/**
	 * This function allow to retrieve the Material dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetMaterialDependencies(TArray<FString>& OutDependencies) const
	{
		MaterialDependencies.GetNames(OutDependencies);
	}

	/**
	 * This function allow to retrieve one Material dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetMaterialDependency(const int32 Index, FString& OutDependency) const
	{
		MaterialDependencies.GetName(Index, OutDependency);
	}

	/**
	 * Add one Material dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetMaterialDependencyUid(const FString& DependencyUid)
	{
		return MaterialDependencies.AddName(DependencyUid);
	}

	/**
	 * Remove one Material dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveMaterialDependencyUid(const FString& DependencyUid)
	{
		return MaterialDependencies.RemoveName(DependencyUid);
	}

	/**
	 * This function allow to retrieve the number of Shape dependencies for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetShapeDependeciesCount() const
	{
		return ShapeDependencies.GetCount();
	}

	/**
	 * This function allow to retrieve the Shape dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetShapeDependencies(TArray<FString>& OutDependencies) const
	{
		ShapeDependencies.GetNames(OutDependencies);
	}

	/**
	 * This function allow to retrieve one Shape dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetShapeDependency(const int32 Index, FString& OutDependency) const
	{
		ShapeDependencies.GetName(Index, OutDependency);
	}

	/**
	 * Add one Shape dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetShapeDependencyUid(const FString& DependencyUid)
	{
		return ShapeDependencies.AddName(DependencyUid);
	}

	/**
	 * Remove one Shape dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveShapeDependencyUid(const FString& DependencyUid)
	{
		return ShapeDependencies.RemoveName(DependencyUid);
	}

	/**
	 * This function allow to retrieve the number of scene node instancing this mesh.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetSceneInstanceUidsCount() const
	{
		return SceneInstancesUids.GetCount();
	}

	/**
	 * This function allow to retrieve the Shape dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSceneInstanceUids(TArray<FString>& OutDependencies) const
	{
		SceneInstancesUids.GetNames(OutDependencies);
	}

	/**
	 * This function allow to retrieve one Shape dependency for this object.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSceneInstanceUid(const int32 Index, FString& OutDependency) const
	{
		SceneInstancesUids.GetName(Index, OutDependency);
	}

	/**
	 * Add one Shape dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetSceneInstanceUid(const FString& DependencyUid)
	{
		return SceneInstancesUids.AddName(DependencyUid);
	}

	/**
	 * Remove one Shape dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveSceneInstanceUid(const FString& DependencyUid)
	{
		return SceneInstancesUids.RemoveName(DependencyUid);
	}
private:
	const UE::Interchange::FAttributeKey Macro_CustomVertexCountKey = UE::Interchange::FAttributeKey(TEXT("VertexCount"));
	const UE::Interchange::FAttributeKey Macro_CustomPolygonCountKey = UE::Interchange::FAttributeKey(TEXT("PolygonCount"));
	const UE::Interchange::FAttributeKey Macro_CustomBoundingBoxKey = UE::Interchange::FAttributeKey(TEXT("BoundingBox"));
	const UE::Interchange::FAttributeKey Macro_CustomHasVertexNormalKey = UE::Interchange::FAttributeKey(TEXT("HasVertexNormal"));
	const UE::Interchange::FAttributeKey Macro_CustomHasVertexBinormalKey = UE::Interchange::FAttributeKey(TEXT("HasVertexBinormal"));
	const UE::Interchange::FAttributeKey Macro_CustomHasVertexTangentKey = UE::Interchange::FAttributeKey(TEXT("HasVertexTangent"));
	const UE::Interchange::FAttributeKey Macro_CustomHasSmoothGroupKey = UE::Interchange::FAttributeKey(TEXT("HasSmoothGroup"));
	const UE::Interchange::FAttributeKey Macro_CustomHasVertexColorKey = UE::Interchange::FAttributeKey(TEXT("HasVertexColor"));
	const UE::Interchange::FAttributeKey Macro_CustomUVCountKey = UE::Interchange::FAttributeKey(TEXT("UVCount"));

	UE::Interchange::FNameAttributeArrayHelper SkeletonDependencies;
	UE::Interchange::FNameAttributeArrayHelper MaterialDependencies;
	UE::Interchange::FNameAttributeArrayHelper ShapeDependencies;
	UE::Interchange::FNameAttributeArrayHelper SceneInstancesUids;
};
