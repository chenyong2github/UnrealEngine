// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMeshNode.h"

//Interchange namespace
namespace UE
{
	namespace Interchange
	{

		const FAttributeKey& FMeshNodeStaticData::PayloadSourceFileKey()
		{
			static FAttributeKey AttributeKey(TEXT("__PayloadSourceFile__"));
			return AttributeKey;
		}

		const FAttributeKey& FMeshNodeStaticData::IsSkinnedMeshKey()
		{
			static FAttributeKey AttributeKey(TEXT("__IsSkinnedMeshKey__"));
			return AttributeKey;
		}

		const FAttributeKey& FMeshNodeStaticData::IsBlendShapeKey()
		{
			static FAttributeKey AttributeKey(TEXT("__IsBlendShapeKey__"));
			return AttributeKey;
		}

		const FAttributeKey& FMeshNodeStaticData::BlendShapeNameKey()
		{
			static FAttributeKey AttributeKey(TEXT("__BlendShapeNameKey__"));
			return AttributeKey;
		}

		const FString& FMeshNodeStaticData::GetSkeletonDependenciesKey()
		{
			static FString Dependencies_BaseKey = TEXT("__MeshSkeletonDependencies__");
			return Dependencies_BaseKey;
		}

		const FString& FMeshNodeStaticData::GetMaterialDependenciesKey()
		{
			static FString Dependencies_BaseKey = TEXT("__MeshMaterialDependencies__");
			return Dependencies_BaseKey;
		}

		const FString& FMeshNodeStaticData::GetShapeDependenciesKey()
		{
			static FString Dependencies_BaseKey = TEXT("__MeshShapeDependencies__");
			return Dependencies_BaseKey;
		}

		const FString& FMeshNodeStaticData::GetSceneInstancesUidsKey()
		{
			static FString SceneInstanceUids_BaseKey = TEXT("__MeshSceneInstancesUids__");
			return SceneInstanceUids_BaseKey;
		}

	}//ns Interchange
}//ns UE

UInterchangeMeshNode::UInterchangeMeshNode()
{
	SkeletonDependencies.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetSkeletonDependenciesKey());
	MaterialDependencies.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetMaterialDependenciesKey());
	ShapeDependencies.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetShapeDependenciesKey());
	SceneInstancesUids.Initialize(Attributes, UE::Interchange::FMeshNodeStaticData::GetSceneInstancesUidsKey());
}

FString UInterchangeMeshNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
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
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
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
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
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
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
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
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}
	return Super::GetKeyDisplayName(NodeAttributeKey);
}

FString UInterchangeMeshNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
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

FString UInterchangeMeshNode::GetTypeName() const
{
	const FString TypeName = TEXT("MeshNode");
	return TypeName;
}

FName UInterchangeMeshNode::GetIconName() const
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

bool UInterchangeMeshNode::IsSkinnedMesh() const
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

bool UInterchangeMeshNode::SetSkinnedMesh(const bool bIsSkinnedMesh)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey(), bIsSkinnedMesh);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FMeshNodeStaticData::IsSkinnedMeshKey());
		return Handle.IsValid();
	}
	return false;
}

bool UInterchangeMeshNode::IsBlendShape() const
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

bool UInterchangeMeshNode::SetBlendShape(const bool bIsBlendShape)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey(), bIsBlendShape);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FMeshNodeStaticData::IsBlendShapeKey());
		return Handle.IsValid();
	}
	return false;
}

bool UInterchangeMeshNode::GetBlendShapeName(FString& OutBlendShapeName) const
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

bool UInterchangeMeshNode::SetBlendShapeName(const FString& BlendShapeName)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey(), BlendShapeName);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FMeshNodeStaticData::BlendShapeNameKey());
		return Handle.IsValid();
	}
	return false;
}

const TOptional<FString> UInterchangeMeshNode::GetPayLoadKey() const
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

void UInterchangeMeshNode::SetPayLoadKey(const FString& PayloadKey)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey(), PayloadKey);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("UInterchangeMeshNode.SetPayLoadKey"), UE::Interchange::FMeshNodeStaticData::PayloadSourceFileKey());
	}
}
	
bool UInterchangeMeshNode::GetCustomVertexCount(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(VertexCount, int32);
}

bool UInterchangeMeshNode::SetCustomVertexCount(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(VertexCount, int32);
}

bool UInterchangeMeshNode::GetCustomPolygonCount(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(PolygonCount, int32);
}

bool UInterchangeMeshNode::SetCustomPolygonCount(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(PolygonCount, int32);
}

bool UInterchangeMeshNode::GetCustomBoundingBox(FBox& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(BoundingBox, FBox);
}

bool UInterchangeMeshNode::SetCustomBoundingBox(const FBox& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(BoundingBox, FBox);
}

bool UInterchangeMeshNode::GetCustomHasVertexNormal(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexNormal, bool);
}

bool UInterchangeMeshNode::SetCustomHasVertexNormal(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexNormal, bool);
}

bool UInterchangeMeshNode::GetCustomHasVertexBinormal(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexBinormal, bool);
}

bool UInterchangeMeshNode::SetCustomHasVertexBinormal(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexBinormal, bool);
}

bool UInterchangeMeshNode::GetCustomHasVertexTangent(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexTangent, bool);
}

bool UInterchangeMeshNode::SetCustomHasVertexTangent(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexTangent, bool);
}

bool UInterchangeMeshNode::GetCustomHasSmoothGroup(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasSmoothGroup, bool);
}

bool UInterchangeMeshNode::SetCustomHasSmoothGroup(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasSmoothGroup, bool);
}

bool UInterchangeMeshNode::GetCustomHasVertexColor(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(HasVertexColor, bool);
}

bool UInterchangeMeshNode::SetCustomHasVertexColor(const bool& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(HasVertexColor, bool);
}

bool UInterchangeMeshNode::GetCustomUVCount(int32& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(UVCount, int32);
}

bool UInterchangeMeshNode::SetCustomUVCount(const int32& AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(UVCount, int32);
}

int32 UInterchangeMeshNode::GetSkeletonDependeciesCount() const
{
	return SkeletonDependencies.GetCount();
}

void UInterchangeMeshNode::GetSkeletonDependencies(TArray<FString>& OutDependencies) const
{
	SkeletonDependencies.GetItems(OutDependencies);
}

void UInterchangeMeshNode::GetSkeletonDependency(const int32 Index, FString& OutDependency) const
{
	SkeletonDependencies.GetItem(Index, OutDependency);
}

bool UInterchangeMeshNode::SetSkeletonDependencyUid(const FString& DependencyUid)
{ 
	return SkeletonDependencies.AddItem(DependencyUid);
}

bool UInterchangeMeshNode::RemoveSkeletonDependencyUid(const FString& DependencyUid)
{
	return SkeletonDependencies.RemoveItem(DependencyUid);
}

int32 UInterchangeMeshNode::GetMaterialDependeciesCount() const
{
	return MaterialDependencies.GetCount();
}

void UInterchangeMeshNode::GetMaterialDependencies(TArray<FString>& OutDependencies) const
{
	MaterialDependencies.GetItems(OutDependencies);
}

void UInterchangeMeshNode::GetMaterialDependency(const int32 Index, FString& OutDependency) const
{
	MaterialDependencies.GetItem(Index, OutDependency);
}

bool UInterchangeMeshNode::SetMaterialDependencyUid(const FString& DependencyUid)
{
	return MaterialDependencies.AddItem(DependencyUid);
}

bool UInterchangeMeshNode::RemoveMaterialDependencyUid(const FString& DependencyUid)
{
	return MaterialDependencies.RemoveItem(DependencyUid);
}

int32 UInterchangeMeshNode::GetShapeDependeciesCount() const
{
	return ShapeDependencies.GetCount();
}

void UInterchangeMeshNode::GetShapeDependencies(TArray<FString>& OutDependencies) const
{
	ShapeDependencies.GetItems(OutDependencies);
}

void UInterchangeMeshNode::GetShapeDependency(const int32 Index, FString& OutDependency) const
{
	ShapeDependencies.GetItem(Index, OutDependency);
}

bool UInterchangeMeshNode::SetShapeDependencyUid(const FString& DependencyUid)
{
	return ShapeDependencies.AddItem(DependencyUid);
}

bool UInterchangeMeshNode::RemoveShapeDependencyUid(const FString& DependencyUid)
{
	return ShapeDependencies.RemoveItem(DependencyUid);
}

int32 UInterchangeMeshNode::GetSceneInstanceUidsCount() const
{
	return SceneInstancesUids.GetCount();
}

void UInterchangeMeshNode::GetSceneInstanceUids(TArray<FString>& OutDependencies) const
{
	SceneInstancesUids.GetItems(OutDependencies);
}

void UInterchangeMeshNode::GetSceneInstanceUid(const int32 Index, FString& OutDependency) const
{
	SceneInstancesUids.GetItem(Index, OutDependency);
}

bool UInterchangeMeshNode::SetSceneInstanceUid(const FString& DependencyUid)
{
	return SceneInstancesUids.AddItem(DependencyUid);
}

bool UInterchangeMeshNode::RemoveSceneInstanceUid(const FString& DependencyUid)
{
	return SceneInstancesUids.RemoveItem(DependencyUid);
}