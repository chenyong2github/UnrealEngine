// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeBaseNode.h"

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"


UInterchangeBaseNode::UInterchangeBaseNode()
{
	Attributes = MakeShared<UE::Interchange::FAttributeStorage, ESPMode::ThreadSafe>();
	FactoryDependencies.Initialize(Attributes, UE::Interchange::FBaseNodeStaticData::GetFactoryDependenciesBaseKey());
	TargetNodes.Initialize(Attributes, UE::Interchange::FBaseNodeStaticData::TargetAssetIDsKey());
	RegisterAttribute<bool>(UE::Interchange::FBaseNodeStaticData::IsEnabledKey(), true);
	RegisterAttribute<uint8>(UE::Interchange::FBaseNodeStaticData::NodeContainerTypeKey(), static_cast<uint8>(EInterchangeNodeContainerType::NodeContainerType_None));
}

void UInterchangeBaseNode::InitializeNode(const FString& UniqueID, const FString& DisplayLabel, const EInterchangeNodeContainerType NodeContainerType)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::UniqueIDKey(), UniqueID, UE::Interchange::EAttributeProperty::NoHash);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), UE::Interchange::FBaseNodeStaticData::UniqueIDKey());
	}

	Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey(), DisplayLabel, UE::Interchange::EAttributeProperty::NoHash);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), UE::Interchange::FBaseNodeStaticData::DisplayLabelKey());
	}

	Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::NodeContainerTypeKey(), static_cast<uint8>(NodeContainerType));
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), UE::Interchange::FBaseNodeStaticData::NodeContainerTypeKey());
	}

	bIsInitialized = true;
}

FString UInterchangeBaseNode::GetTypeName() const
{
	const FString TypeName = TEXT("BaseNode");
	return TypeName;
}

bool UInterchangeBaseNode::HasAttribute(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	return Attributes->ContainAttribute(NodeAttributeKey);
}


FString UInterchangeBaseNode::GetUniqueID() const
{
	ensure(bIsInitialized);
	FString UniqueID;
	Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::UniqueIDKey()).Get(UniqueID);
	return UniqueID;
}

FString UInterchangeBaseNode::GetDisplayLabel() const
{
	ensure(bIsInitialized);
	checkSlow(Attributes->ContainAttribute(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey()));
	FString DisplayLabel;
	Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey()).Get(DisplayLabel);
	return DisplayLabel;
}

bool UInterchangeBaseNode::SetDisplayLabel(const FString& DisplayLabel)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey(), DisplayLabel);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey());
		return Handle.IsValid();
	}
	return false;
}

FString UInterchangeBaseNode::GetParentUid() const
{
	if (!Attributes->ContainAttribute(UE::Interchange::FBaseNodeStaticData::ParentIDKey()))
	{
		return InvalidNodeUid();
	}

	FString ParentUniqueID;
	UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::ParentIDKey());
	if(Handle.IsValid())
	{
		Handle.Get(ParentUniqueID);
		return ParentUniqueID;
	}
	return InvalidNodeUid();
}

bool UInterchangeBaseNode::SetParentUid(const FString& ParentUid)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::ParentIDKey(), ParentUid);
	if(IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::ParentIDKey());
		return Handle.IsValid();
	}
	return false;
}

int32 UInterchangeBaseNode::GetFactoryDependenciesCount() const
{
	return FactoryDependencies.GetCount();
}

void UInterchangeBaseNode::GetFactoryDependency(const int32 Index, FString& OutDependency) const
{
	FactoryDependencies.GetName(Index, OutDependency);
}

void UInterchangeBaseNode::GetFactoryDependencies(TArray<FString>& OutDependencies ) const
{
	FactoryDependencies.GetNames(OutDependencies);
}

bool UInterchangeBaseNode::AddFactoryDependencyUid(const FString& DependencyUid)
{
	return FactoryDependencies.AddName(DependencyUid);
}

bool UInterchangeBaseNode::RemoveFactoryDependencyUid(const FString& DependencyUid)
{
	return FactoryDependencies.RemoveName(DependencyUid);
}

bool UInterchangeBaseNode::IsEnabled() const
{
	if (!Attributes->ContainAttribute(UE::Interchange::FBaseNodeStaticData::IsEnabledKey()))
	{
		return false;
	}

	UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FBaseNodeStaticData::IsEnabledKey());
	if (Handle.IsValid())
	{
		bool bIsEnabled = false;
		Handle.Get(bIsEnabled);
		return bIsEnabled;
	}
	return false;
}

bool UInterchangeBaseNode::SetEnabled(const bool bIsEnabled)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::IsEnabledKey(), bIsEnabled);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes->GetAttributeHandle<bool>(UE::Interchange::FBaseNodeStaticData::IsEnabledKey());
		return Handle.IsValid();
	}
	return false;
}

EInterchangeNodeContainerType UInterchangeBaseNode::GetNodeContainerType() const
{
	if (!Attributes->ContainAttribute(UE::Interchange::FBaseNodeStaticData::NodeContainerTypeKey()))
	{
		return EInterchangeNodeContainerType::NodeContainerType_None;
	}
	UE::Interchange::FAttributeStorage::TAttributeHandle<uint8> Handle = Attributes->GetAttributeHandle<uint8>(UE::Interchange::FBaseNodeStaticData::NodeContainerTypeKey());
	if (Handle.IsValid())
	{
		uint8 Value = static_cast<uint8>(EInterchangeNodeContainerType::NodeContainerType_None);
		Handle.Get(Value);
		return static_cast<EInterchangeNodeContainerType>(Value);
	}
	return EInterchangeNodeContainerType::NodeContainerType_None;
}

FGuid UInterchangeBaseNode::GetHash() const
{
	return Attributes->GetStorageHash();
}

UClass* UInterchangeBaseNode::GetObjectClass() const
{
	return nullptr;
}

FString UInterchangeBaseNode::GetAssetName() const
{
	if (!Attributes->ContainAttribute(UE::Interchange::FBaseNodeStaticData::AssetNameKey()))
	{
		return GetDisplayLabel();
	}

	UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::AssetNameKey());
	if (Handle.IsValid())
	{
		FString Value;
		Handle.Get(Value);
		return Value;
	}

	return GetDisplayLabel();
}

bool UInterchangeBaseNode::SetAssetName(const FString& AssetName)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(UE::Interchange::FBaseNodeStaticData::AssetNameKey(), AssetName);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<FString> Handle = Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::AssetNameKey());
		return Handle.IsValid();
	}
	return false;
}

int32 UInterchangeBaseNode::GetTargetNodeCount() const
{
	return TargetNodes.GetCount();
}

void UInterchangeBaseNode::GetTargetNodeUids(TArray<FString>& OutTargetAssets) const
{
	TargetNodes.GetNames(OutTargetAssets);
}

bool UInterchangeBaseNode::AddTargetNodeUid(const FString& AssetUid) const
{
	return TargetNodes.AddName(AssetUid);
}

bool UInterchangeBaseNode::RemoveTargetNodeUid(const FString& AssetUid) const
{
	return TargetNodes.RemoveName(AssetUid);
}

FString UInterchangeBaseNode::InvalidNodeUid()
{
	return FString();
}

void UInterchangeBaseNode::ApplyAllCustomAttributeToAsset(UObject* Object) const
{
	UClass* ObjectClass = Object->GetClass();
	for (const TPair<UClass*, TArray<UE::Interchange::FApplyAttributeToAsset>>& ClassDelegatePair : ApplyCustomAttributeDelegates)
	{
		if(ObjectClass->IsChildOf(ClassDelegatePair.Key))
		{
			for (const UE::Interchange::FApplyAttributeToAsset& Delegate : ClassDelegatePair.Value)
			{
				if(Delegate.IsBound())
				{
					Delegate.Execute(Object);
				}
			}
		}
	}
}

void UInterchangeBaseNode::FillAllCustomAttributeFromAsset(UObject* Object) const
{
	UClass* ObjectClass = Object->GetClass();
	for (const TPair<UClass*, TArray<UE::Interchange::FFillAttributeToAsset>>& ClassDelegatePair : FillCustomAttributeDelegates)
	{
		if (ObjectClass->IsChildOf(ClassDelegatePair.Key))
		{
			for (const UE::Interchange::FFillAttributeToAsset& Delegate : ClassDelegatePair.Value)
			{
				if (Delegate.IsBound())
				{
					Delegate.Execute(Object);
				}
			}
		}
	}
}

void UInterchangeBaseNode::Serialize(FArchive& Ar)
{
	UE::Interchange::FAttributeStorage& RefAttributes = *(Attributes.Get());
	Ar << RefAttributes;
	if (Ar.IsLoading())
	{
		//The node is consider Initialize if the UniqueID and the Display label are set properly
		bIsInitialized = (Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::UniqueIDKey()).IsValid() &&
						  Attributes->GetAttributeHandle<FString>(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey()).IsValid());

	}
}
