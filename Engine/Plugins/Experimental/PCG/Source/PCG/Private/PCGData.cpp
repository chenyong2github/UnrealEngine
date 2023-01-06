// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGData.h"
#include "PCGSettings.h"
#include "PCGParamData.h"
#include "Data/PCGSpatialData.h"

#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGData)

void FPCGRootSet::Clear()
{
	for (TPair<UObject*, int32>& Entry : RootSet)
	{
		Entry.Key->RemoveFromRoot();
	}

	RootSet.Reset();
}

void FPCGRootSet::Add(UObject* InObject)
{
	check(InObject);
	AddInternal(InObject);
}

void FPCGRootSet::AddInternal(UObject* InObject)
{
	check(InObject && !InObject->IsA<UPackage>());

	if (int32* Found = RootSet.Find(InObject))
	{
		(*Found)++;
	}
	else if (!InObject->IsRooted() && InObject->GetPackage() == GetTransientPackage())
	{
		InObject->AddToRoot();
		RootSet.Emplace(InObject, 1);
	}

	// Recurse to outermost
	UObject* OuterObject = InObject->GetOuter();
	if (OuterObject && !OuterObject->IsA<UPackage>())
	{
		AddInternal(OuterObject);
	}

	// Recurse on metadata
	UPCGMetadata* InMetadata = nullptr;
	if (UPCGSpatialData* InSpatialData = Cast<UPCGSpatialData>(InObject))
	{
		InMetadata = InSpatialData->Metadata;
	}
	else if (UPCGParamData* InParamData = Cast<UPCGParamData>(InObject))
	{
		InMetadata = InParamData->Metadata;
	}

	if (InMetadata && InMetadata->GetParent())
	{
		UObject* MetadataParentOuter = InMetadata->GetParent()->GetOuter();
		if (MetadataParentOuter && !MetadataParentOuter->IsA<UPackage>())
		{
			AddInternal(MetadataParentOuter);
		}
	}
}

void FPCGRootSet::Remove(UObject* InObject)
{
	if (!InObject)
	{
		UE_LOG(LogPCG, Warning, TEXT("Trying to remove a null object from the rootset"));
		return;
	}

	RemoveInternal(InObject);
}

void FPCGRootSet::RemoveInternal(UObject* InObject)
{
	check(InObject && !InObject->IsA<UPackage>());

	if (int32* Found = RootSet.Find(InObject))
	{
		check(InObject->IsRooted());
		(*Found)--;

		if (*Found == 0)
		{
			InObject->RemoveFromRoot();
			RootSet.Remove(InObject);
		}
	}

	// Recurse to outermost
	UObject* OuterObject = InObject->GetOuter();
	if (OuterObject && !OuterObject->IsA<UPackage>())
	{
		RemoveInternal(OuterObject);
	}

	// Recurse on metadata
	UPCGMetadata* InMetadata = nullptr;

	if (UPCGSpatialData* InSpatialData = Cast<UPCGSpatialData>(InObject))
	{
		InMetadata = InSpatialData->Metadata;
	}
	else if (UPCGParamData* InParamData = Cast<UPCGParamData>(InObject))
	{
		InMetadata = InParamData->Metadata;
	}

	if (InMetadata && InMetadata->GetParent())
	{
		UObject* MetadataParentOuter = InMetadata->GetParent()->GetOuter();
		if (MetadataParentOuter && !MetadataParentOuter->IsA<UPackage>())
		{
			RemoveInternal(MetadataParentOuter);
		}
	}
}

bool FPCGTaggedData::operator==(const FPCGTaggedData& Other) const
{
	return Data == Other.Data &&
		Tags.Num() == Other.Tags.Num() &&
		Tags.Includes(Other.Tags) &&
		Pin == Other.Pin;
}

bool FPCGTaggedData::operator!=(const FPCGTaggedData& Other) const
{
	return !operator==(Other);
}

TArray<FPCGTaggedData> FPCGDataCollection::GetInputs() const
{
	return TaggedData.FilterByPredicate([](const FPCGTaggedData& Data) {
		return Cast<UPCGSpatialData>(Data.Data) != nullptr;
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetInputsByPin(const FName& InPinLabel) const
{
	return TaggedData.FilterByPredicate([&InPinLabel](const FPCGTaggedData& Data) {
		return Data.Pin == InPinLabel;
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetTaggedInputs(const FString& InTag) const
{
	return TaggedData.FilterByPredicate([&InTag](const FPCGTaggedData& Data) {
		return Data.Tags.Contains(InTag) && Cast<UPCGSpatialData>(Data.Data);
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetAllSettings() const
{
	return TaggedData.FilterByPredicate([](const FPCGTaggedData& Data) {
		return Cast<UPCGSettings>(Data.Data) != nullptr;
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetAllParams() const
{
	return TaggedData.FilterByPredicate([](const FPCGTaggedData& Data) {
		return Cast<UPCGParamData>(Data.Data) != nullptr;
	});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetParamsByPin(const FName& InPinLabel) const
{
	return TaggedData.FilterByPredicate([&InPinLabel](const FPCGTaggedData& Data) {
		return Data.Pin == InPinLabel && Cast<UPCGParamData>(Data.Data);
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetTaggedParams(const FString& InTag) const
{
	return TaggedData.FilterByPredicate([&InTag](const FPCGTaggedData& Data) {
		return Data.Tags.Contains(InTag) && Cast<UPCGParamData>(Data.Data) != nullptr;
		});
}

UPCGParamData* FPCGDataCollection::GetParams() const
{
	for (const FPCGTaggedData& TaggedDatum : TaggedData)
	{
		if (UPCGParamData* Params = Cast<UPCGParamData>(TaggedDatum.Data))
		{
			return Params; 
		}
	}

	return nullptr;
}

UPCGParamData* FPCGDataCollection::GetParamsOnParamsPin() const
{
	TArray<FPCGTaggedData> ParamsOnDefaultPin = GetParamsByPin(PCGPinConstants::DefaultParamsLabel);
	return (ParamsOnDefaultPin.IsEmpty() ? nullptr : Cast<UPCGParamData>(ParamsOnDefaultPin[0].Data));
}

const UPCGSettings* FPCGDataCollection::GetSettings(const UPCGSettings* InDefaultSettings) const
{
	if (!InDefaultSettings)
	{
		return GetSettings<UPCGSettings>();
	}
	else
	{
		const FPCGTaggedData* MatchingData = TaggedData.FindByPredicate([InDefaultSettings](const FPCGTaggedData& Data) {
			return Data.Data &&
				(Data.Data->GetClass() == InDefaultSettings->GetClass() ||
					Data.Data->GetClass()->IsChildOf(InDefaultSettings->GetClass()));
			});

		return MatchingData ? Cast<const UPCGSettings>(MatchingData->Data) : InDefaultSettings;
	}
}

const UPCGSettingsInterface* FPCGDataCollection::GetSettingsInterface() const
{
	return GetSettings<UPCGSettingsInterface>();
}

const UPCGSettingsInterface* FPCGDataCollection::GetSettingsInterface(const UPCGSettingsInterface* InDefaultSettingsInterface) const
{
	if (!InDefaultSettingsInterface || InDefaultSettingsInterface->GetSettings() == nullptr)
	{
		return GetSettingsInterface();
	}
	else
	{
		const FPCGTaggedData* MatchingData = TaggedData.FindByPredicate([InDefaultSettingsInterface](const FPCGTaggedData& Data) {
			if (UPCGSettingsInterface* DataSettingsInterface = Cast<UPCGSettingsInterface>(Data.Data))
			{
				// Compare settings classes
				return DataSettingsInterface->GetSettings()->GetClass() == InDefaultSettingsInterface->GetSettings()->GetClass() ||
					DataSettingsInterface->GetSettings()->GetClass()->IsChildOf(InDefaultSettingsInterface->GetSettings()->GetClass());
			}

			return false;
		});

		return MatchingData ? Cast<const UPCGSettingsInterface>(MatchingData->Data) : InDefaultSettingsInterface;
	}
}

bool FPCGDataCollection::operator==(const FPCGDataCollection& Other) const
{
	if (bCancelExecution != Other.bCancelExecution || TaggedData.Num() != Other.TaggedData.Num())
	{
		return false;
	}

	// TODO: Once we make the arguments order irrelevant, then this should be updated
	for (int32 DataIndex = 0; DataIndex < TaggedData.Num(); ++DataIndex)
	{
		if (TaggedData[DataIndex] != Other.TaggedData[DataIndex])
		{
			return false;
		}
	}
	
	return true;
}

bool FPCGDataCollection::operator!=(const FPCGDataCollection& Other) const
{
	return !operator==(Other);
}

void FPCGDataCollection::AddToRootSet(FPCGRootSet& RootSet) const
{
	for (const FPCGTaggedData& Data : TaggedData)
	{
		if (Data.Data)
		{
			// This is technically a const_cast
			RootSet.Add(Cast<UObject>(Data.Data));
		}
	}
}

void FPCGDataCollection::RemoveFromRootSet(FPCGRootSet& RootSet) const
{
	for (const FPCGTaggedData& Data : TaggedData)
	{
		if (Data.Data)
		{
			// This is technically a const_cast
			RootSet.Remove(Cast<UObject>(Data.Data));
		}
	}
}

void FPCGDataCollection::Reset()
{
	// Implementation note: We are assuming that there is no need to remove the data from the rootset here.
	TaggedData.Reset();
	bCancelExecutionOnEmpty = false;
	bCancelExecution = false;
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetInputs(const FPCGDataCollection& InCollection)
{
	return InCollection.GetInputs();
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetInputsByPin(const FPCGDataCollection& InCollection, const FName& InPinLabel)
{
	return InCollection.GetInputsByPin(InPinLabel);
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetTaggedInputs(const FPCGDataCollection& InCollection, const FString& InTag)
{
	return InCollection.GetTaggedInputs(InTag);
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetParams(const FPCGDataCollection& InCollection)
{
	return InCollection.GetAllParams();
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetParamsByPin(const FPCGDataCollection& InCollection, const FName& InPinLabel)
{
	return InCollection.GetParamsByPin(InPinLabel);
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetTaggedParams(const FPCGDataCollection& InCollection, const FString& InTag)
{
	return InCollection.GetTaggedParams(InTag);
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetAllSettings(const FPCGDataCollection& InCollection)
{
	return InCollection.GetAllSettings();
}
