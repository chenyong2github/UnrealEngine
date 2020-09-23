// Copyright Epic Games, Inc. All Rights Reserved.
#include "Nodes/InterchangeBaseNode.h"

#include "CoreMinimal.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"




void UInterchangeBaseNode::InitializeNode(const FName& UniqueID, const FName& DisplayLabel)
{
	Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(Interchange::FBaseNodeStaticData::UniqueIDKey(), UniqueID, Interchange::EAttributeProperty::NoHash);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), Interchange::FBaseNodeStaticData::UniqueIDKey());
	}

	Result = Attributes.RegisterAttribute(Interchange::FBaseNodeStaticData::DisplayLabelKey(), DisplayLabel, Interchange::EAttributeProperty::NoHash);
	if (!IsAttributeStorageResultSuccess(Result))
	{
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), Interchange::FBaseNodeStaticData::DisplayLabelKey());
	}
	bIsInitialized = true;
}

FString UInterchangeBaseNode::GetTypeName() const
{
	const FString TypeName = TEXT("BaseNode");
	return TypeName;
}

bool UInterchangeBaseNode::HasAttribute(const Interchange::FAttributeKey& NodeAttributeKey) const
{
	return Attributes.ContainAttribute(NodeAttributeKey);
}


FName UInterchangeBaseNode::GetUniqueID() const
{
	ensure(bIsInitialized);
	FName UniqueID = NAME_None;
	Attributes.GetAttributeHandle<FName>(Interchange::FBaseNodeStaticData::UniqueIDKey()).Get(UniqueID);
	return UniqueID;
}

FName UInterchangeBaseNode::GetDisplayLabel() const
{
	ensure(bIsInitialized);
	checkSlow(Attributes.ContainAttribute(Interchange::FBaseNodeStaticData::DisplayLabelKey()));
	FName DisplayLabel = NAME_None;
	Attributes.GetAttributeHandle<FName>(Interchange::FBaseNodeStaticData::DisplayLabelKey()).Get(DisplayLabel);
	return DisplayLabel;
}

FName UInterchangeBaseNode::GetParentUID() const
{
	if (!Attributes.ContainAttribute(Interchange::FBaseNodeStaticData::ParentIDKey()))
	{
		return InvalidNodeUID();
	}

	FName ParentUniqueID = NAME_None;
	Interchange::FAttributeStorage::TAttributeHandle<FName> Handle = Attributes.GetAttributeHandle<FName>(Interchange::FBaseNodeStaticData::ParentIDKey());
	if(Handle.IsValid())
	{
		Handle.Get(ParentUniqueID);
		return ParentUniqueID;
	}
	return InvalidNodeUID();
}

bool UInterchangeBaseNode::SetParentUID(FName ParentUID)
{
	Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(Interchange::FBaseNodeStaticData::ParentIDKey(), ParentUID);
	if(IsAttributeStorageResultSuccess(Result))
	{
		Interchange::FAttributeStorage::TAttributeHandle<FName> Handle = Attributes.GetAttributeHandle<FName>(Interchange::FBaseNodeStaticData::ParentIDKey());
		return Handle.IsValid();
	}
	return false;
}

void UInterchangeBaseNode::GetDependecies(TArray<FName>& OutDependencies ) const
{
	OutDependencies.Reset();

	int32 DepenedenciesCount = 0;
	if (!Attributes.ContainAttribute(Interchange::FBaseNodeStaticData::DependencyCountKey()))
	{
		return;
	}
	Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(Interchange::FBaseNodeStaticData::DependencyCountKey());
	if (!Handle.IsValid())
	{
		return;
	}
	Handle.Get(DepenedenciesCount);
	for (int32 DepIndex = 0; DepIndex < DepenedenciesCount; ++DepIndex)
	{
		FString DepIndexKeyString = Interchange::FBaseNodeStaticData::DependencyBaseKey().Key.ToString() + FString::FromInt(DepIndex);
		Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
		Interchange::FAttributeStorage::TAttributeHandle<FName> HandleDep = Attributes.GetAttributeHandle<FName>(DepIndexKey);
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
	if (!Attributes.ContainAttribute(Interchange::FBaseNodeStaticData::DependencyCountKey()))
	{
		const int32 DependencyCount = 0;
		Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute<int32>(Interchange::FBaseNodeStaticData::DependencyCountKey(), DependencyCount);
	}
	Interchange::FAttributeStorage::TAttributeHandle<int32> Handle = Attributes.GetAttributeHandle<int32>(Interchange::FBaseNodeStaticData::DependencyCountKey());
	if(!ensure(Handle.IsValid()))
	{
		return false;
	}
	int32 DepIndex = 0;
	Handle.Get(DepIndex);
	FString DepIndexKeyString = Interchange::FBaseNodeStaticData::DependencyBaseKey().Key.ToString() + FString::FromInt(DepIndex);
	Interchange::FAttributeKey DepIndexKey(*DepIndexKeyString);
	//Increment the counter
	DepIndex++;
	Handle.Set(DepIndex);

	Interchange::EAttributeStorageResult DepResult = Attributes.RegisterAttribute<FName>(DepIndexKey, DependencyUID);
	return true;
}

bool UInterchangeBaseNode::IsEnabled() const
{
	Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes.GetAttributeHandle<bool>(Interchange::FBaseNodeStaticData::IsEnabledKey());
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
	Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(Interchange::FBaseNodeStaticData::IsEnabledKey(), bIsEnabled);
	if (IsAttributeStorageResultSuccess(Result))
	{
		Interchange::FAttributeStorage::TAttributeHandle<bool> Handle = Attributes.GetAttributeHandle<bool>(Interchange::FBaseNodeStaticData::IsEnabledKey());
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
	for (const TPair<UClass*, TArray<Interchange::FApplyAttributeToAsset>>& ClassDelegatePair : ApplyCustomAttributeDelegates)
	{
		if(ObjectClass->IsChildOf(ClassDelegatePair.Key))
		{
			for (const Interchange::FApplyAttributeToAsset& Delegate : ClassDelegatePair.Value)
			{
				if(Delegate.IsBound())
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
		bIsInitialized = (Attributes.GetAttributeHandle<FName>(Interchange::FBaseNodeStaticData::UniqueIDKey()).IsValid() &&
							Attributes.GetAttributeHandle<FName>(Interchange::FBaseNodeStaticData::DisplayLabelKey()).IsValid());
	}
}
