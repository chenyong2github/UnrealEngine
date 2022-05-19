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

		struct INTERCHANGENODES_API FMeshNodeStaticData : public FBaseNodeStaticData
		{
			static const FAttributeKey& PayloadSourceFileKey();
			static const FAttributeKey& IsSkinnedMeshKey();
			static const FAttributeKey& IsBlendShapeKey();
			static const FAttributeKey& BlendShapeNameKey();
			static const FAttributeKey& GetSkeletonDependenciesKey();
			static const FAttributeKey& GetShapeDependenciesKey();
			static const FAttributeKey& GetSceneInstancesUidsKey();
			static const FAttributeKey& GetSlotMaterialDependenciesKey();
		};

	}//ns Interchange
}//ns UE

UCLASS(BlueprintType, Experimental)
class INTERCHANGENODES_API UInterchangeMeshNode : public UInterchangeBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeMeshNode();

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	/**
	 * Icon name are simply create by adding "InterchangeIcon_" in front of the specialized type. If there is no special type the function will return NAME_None which will use the default icon.
	 */
	virtual FName GetIconName() const override;
	
	/**
	 * Override serialize to restore SlotMaterialDependencies on load.
	 */
	virtual void Serialize(FArchive& Ar) override
	{
		Super::Serialize(Ar);

		if (Ar.IsLoading() && bIsInitialized)
		{
			SlotMaterialDependencies.RebuildCache();
		}
	}

	/**
	 * Return true if this node represent a skinned mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool IsSkinnedMesh() const;

	/**
	 * Set the IsSkinnedMesh attribute to determine if this node represent a skinned mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetSkinnedMesh(const bool bIsSkinnedMesh);

	/**
	 * Return true if this node represent a blend shape
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool IsBlendShape() const;

	/**
	 * Set the IsBlendShape attribute to determine if this node represent a blend shape.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetBlendShape(const bool bIsBlendShape);

	/**
	 * Get the blend shape name.
	 * Return true if we successfully query the BlendShapeName attribute
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetBlendShapeName(FString& OutBlendShapeName) const;

	/**
	 * Set the BlendShapeName attribute to determine if this node represent a blend shape.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetBlendShapeName(const FString& BlendShapeName);

	/** Mesh node Interface Begin */
	virtual const TOptional<FString> GetPayLoadKey() const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	virtual void SetPayLoadKey(const FString& PayloadKey);
	
	/** Query this mesh vertices count. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomVertexCount(int32& AttributeValue) const;

	/** Set this mesh vertices count. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomVertexCount(const int32& AttributeValue);

	/** Query this mesh polygon count. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomPolygonCount(int32& AttributeValue) const;

	/** Set this mesh polygon count. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomPolygonCount(const int32& AttributeValue);

	/** Query this mesh bounding box. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomBoundingBox(FBox& AttributeValue) const;

	/** Set this mesh bounding box. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomBoundingBox(const FBox& AttributeValue);

	/** Query if this mesh has vertex normal. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexNormal(bool& AttributeValue) const;

	/** Set this mesh has vertex normal attribute. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexNormal(const bool& AttributeValue);

	/** Query if this mesh has vertex bi-normal. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexBinormal(bool& AttributeValue) const;

	/** Set this mesh has vertex bi-normal attribute. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexBinormal(const bool& AttributeValue);

	/** Query if this mesh has vertex tangent. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexTangent(bool& AttributeValue) const;

	/** Set this mesh has vertex tangent attribute. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexTangent(const bool& AttributeValue);

	/** Query if this mesh has smooth group. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasSmoothGroup(bool& AttributeValue) const;

	/** Set this mesh has smooth group attribute. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasSmoothGroup(const bool& AttributeValue);

	/** Query if this mesh has vertex color. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomHasVertexColor(bool& AttributeValue) const;

	/** Set this mesh has vertex color attribute. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomHasVertexColor(const bool& AttributeValue);

	/** Query this mesh UV count. Return false if the attribute was not set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool GetCustomUVCount(int32& AttributeValue) const;

	/** Set this mesh UV count attribute. Return false if the attribute cannot be set.*/
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetCustomUVCount(const int32& AttributeValue);

	/**
	 * This function allow to retrieve the number of skeleton dependencies for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetSkeletonDependeciesCount() const;

	/**
	 * This function allow to retrieve the skeleton dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSkeletonDependencies(TArray<FString>& OutDependencies) const;

	/**
	 * This function allow to retrieve one skeleton dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSkeletonDependency(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one skeleton dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetSkeletonDependencyUid(const FString& DependencyUid);

	/**
	 * Remove one skeleton dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveSkeletonDependencyUid(const FString& DependencyUid);

	/**
	 * This function allow to retrieve the number of Shape dependencies for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetShapeDependeciesCount() const;

	/**
	 * This function allow to retrieve the Shape dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetShapeDependencies(TArray<FString>& OutDependencies) const;

	/**
	 * This function allow to retrieve one Shape dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetShapeDependency(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one Shape dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetShapeDependencyUid(const FString& DependencyUid);

	/**
	 * Remove one Shape dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveShapeDependencyUid(const FString& DependencyUid);

	/**
	 * This function allow to retrieve the number of scene node instancing this mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	int32 GetSceneInstanceUidsCount() const;

	/**
	 * This function allow to retrieve the Shape dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSceneInstanceUids(TArray<FString>& OutDependencies) const;

	/**
	 * This function allow to retrieve one Shape dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	void GetSceneInstanceUid(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one Shape dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool SetSceneInstanceUid(const FString& DependencyUid);

	/**
	 * Remove one Shape dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | Mesh")
	bool RemoveSceneInstanceUid(const FString& DependencyUid);

	/**
	 * Allow to retrieve the correspondence table between slot names and assigned materials for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	void GetSlotMaterialDependencies(TMap<FString, FString>& OutMaterialDependencies) const;

	/**
	 * Allow to retrieve one Material dependency for a given slot of this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool GetSlotMaterialDependencyUid(const FString& SlotName, FString& OutMaterialDependency) const;

	/**
	 * Add one Material dependency to a specific slot name of this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool SetSlotMaterialDependencyUid(const FString& SlotName, const FString& MaterialDependencyUid);

	/**
	 * Remove the Material dependency associated with the given slot name from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | StaticMesh")
	bool RemoveSlotMaterialDependencyUid(const FString& SlotName);

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

	UE::Interchange::TArrayAttributeHelper<FString> SkeletonDependencies;
	UE::Interchange::TArrayAttributeHelper<FString> MaterialDependencies;
	UE::Interchange::TArrayAttributeHelper<FString> ShapeDependencies;
	UE::Interchange::TArrayAttributeHelper<FString> SceneInstancesUids;
	
	UE::Interchange::TMapAttributeHelper<FString, FString> SlotMaterialDependencies;
};
