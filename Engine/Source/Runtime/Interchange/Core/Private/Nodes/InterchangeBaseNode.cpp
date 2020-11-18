// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeBaseNode.h"

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"




void UInterchangeBaseNode::InitializeNode(const FName& UniqueID, const FName& DisplayLabel)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(UE::Interchange::FBaseNodeStaticData::UniqueIDKey(), UniqueID, UE::Interchange::EAttributeProperty::NoHash);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), UE::Interchange::FBaseNodeStaticData::UniqueIDKey());
	}

	Result = Attributes.RegisterAttribute(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey(), DisplayLabel, UE::Interchange::EAttributeProperty::NoHash);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), UE::Interchange::FBaseNodeStaticData::DisplayLabelKey());
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
	return Attributes.ContainAttribute(NodeAttributeKey);
}


FName UInterchangeBaseNode::GetUniqueID() const
{
	ensure(bIsInitialized);
	FName UniqueID = NAME_None;
	Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::UniqueIDKey()).Get(UniqueID);
	return UniqueID;
}

FName UInterchangeBaseNode::GetDisplayLabel() const
{
	ensure(bIsInitialized);
	checkSlow(Attributes.ContainAttribute(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey()));
	FName DisplayLabel = NAME_None;
	Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey()).Get(DisplayLabel);
	return DisplayLabel;
}

bool UInterchangeBaseNode::SetDisplayLabel(FName DisplayLabel)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey(), DisplayLabel);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<FName> Handle = Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey());
		return Handle.IsValid();
	}
	return false;
}

FName UInterchangeBaseNode::GetParentUID() const
{
	if (!Attributes.ContainAttribute(UE::Interchange::FBaseNodeStaticData::ParentIDKey()))
	{
		return InvalidNodeUID();
	}

	FName ParentUniqueID = NAME_None;
	UE::Interchange::FAttributeStorage::TAttributeHandle<FName> Handle = Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::ParentIDKey());
	if(Handle.IsValid())
	{
		Handle.Get(ParentUniqueID);
		return ParentUniqueID;
	}
	return InvalidNodeUID();
}

bool UInterchangeBaseNode::SetParentUID(FName ParentUID)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(UE::Interchange::FBaseNodeStaticData::ParentIDKey(), ParentUID);
	if(IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<FName> Handle = Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::ParentIDKey());
		return Handle.IsValid();
	}
	return false;
}

void UInterchangeBaseNode::GetDependecies(TArray<FName>& OutDependencies ) const
{
	OutDependencies.Reset();

	int32 DepenedenciesCount = 0;
	if (!Attributes.ContainAttribute(UE::Interchange::FBaseNodeStaticData::DependencyCountKey()))
	{
		return;
	}
	UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FBaseNodeStaticData::DependencyCountKey());
	if (!Handle.IsValid())
	{
		return;
	}
	Handle.Get(DepenedenciesCount);
	for (int32 DepIndex = 0; DepIndex < DepenedenciesCount; ++DepIndex)
	{
		FString DepIndexKeyString = UE::Interchange::FBaseNodeStaticData::DependencyBaseKey().Key.ToString() + FString::FromInt(DepIndex);
		UE::Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
		UE::Interchange::FAttributeStorage::TAttributeHandle<FName> HandleDep = Attributes.GetAttributeHandle<FName>(DepIndexKey);
		if (!HandleDep.IsValid())
		{
			continue;
		}
		FName& NodeUniqueID = OutDependencies.AddDefaulted_GetRef();
		HandleDep.Get(NodeUniqueID);
	}
}

bool UInterchangeBaseNode::SetDependencyUID(FName DependencyUID)
{
	if (!Attributes.ContainAttribute(UE::Interchange::FBaseNodeStaticData::DependencyCountKey()))
	{
		const int32 DependencyCount = 0;
		UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute<int32>(UE::Interchange::FBaseNodeStaticData::DependencyCountKey(), DependencyCount);
	}
	UE::Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(UE::Interchange::FBaseNodeStaticData::DependencyCountKey());
	if(!ensure(Handle.IsValid()))
	{
		return false;
	}
	int32 DepIndex = 0;
	Handle.Get(DepIndex);
	FString DepIndexKeyString = UE::Interchange::FBaseNodeStaticData::DependencyBaseKey().Key.ToString() + FString::FromInt(DepIndex);
	UE::Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
	//Increment the counter
	DepIndex++;
	Handle.Set(DepIndex);

	UE::Interchange::EAttributeStorageResult DepResult = Attributes.RegisterAttribute<FName>(DepIndexKey, DependencyUID);
	return true;
}

bool UInterchangeBaseNode::IsEnabled() const
{
	UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes.GetAttributeHandle<bool>(UE::Interchange::FBaseNodeStaticData::IsEnabledKey());
	if (Handle.IsValid())
	{
		bool bIsEnabled = false;
		Handle.Get(bIsEnabled);
		return bIsEnabled;
	}
	return true;
}

bool UInterchangeBaseNode::SetEnabled(const bool bIsEnabled)
{
	UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(UE::Interchange::FBaseNodeStaticData::IsEnabledKey(), bIsEnabled);
	if (IsAttributeStorageResultSuccess(Result))
	{
		UE::Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes.GetAttributeHandle<bool>(UE::Interchange::FBaseNodeStaticData::IsEnabledKey());
		return Handle.IsValid();
	}
	return false;
}

FGuid UInterchangeBaseNode::GetHash() const
{
	return Attributes.GetStorageHash();
}

class UClass* UInterchangeBaseNode::GetAssetClass() const
{
	return nullptr;
}

FName UInterchangeBaseNode::InvalidNodeUID()
{
	return NAME_None;
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
	Ar << Attributes;
	if (Ar.IsLoading())
	{
		//The node is consider Initialize if the UniqueID and the Display label are set properly
		bIsInitialized = (Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::UniqueIDKey()).IsValid() &&
							Attributes.GetAttributeHandle<FName>(UE::Interchange::FBaseNodeStaticData::DisplayLabelKey()).IsValid());
	}
}
