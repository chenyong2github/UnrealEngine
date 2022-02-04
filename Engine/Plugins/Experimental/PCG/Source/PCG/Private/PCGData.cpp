// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGData.h"
#include "PCGSettings.h"

bool FPCGTaggedData::operator==(const FPCGTaggedData& Other) const
{
	return Data == Other.Data &&
		Usage == Other.Usage &&
		Tags.Num() == Other.Tags.Num() &&
		Tags.Includes(Other.Tags);
}

TArray<FPCGTaggedData> FPCGDataCollection::GetInputs() const
{
	return TaggedData.FilterByPredicate([](const FPCGTaggedData& Data) {
		return Data.Usage == EPCGDataUsage::Input;
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetTaggedInputs(const FString& InTag) const
{
	return TaggedData.FilterByPredicate([&InTag](const FPCGTaggedData& Data) {
		return Data.Usage == EPCGDataUsage::Input && Data.Tags.Contains(InTag);
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetExclusions() const
{
	return TaggedData.FilterByPredicate([](const FPCGTaggedData& Data) {
		return Data.Usage == EPCGDataUsage::Exclusion;
		});
}

TArray<FPCGTaggedData> FPCGDataCollection::GetAllSettings() const
{
	return TaggedData.FilterByPredicate([](const FPCGTaggedData& Data) {
		return Data.Usage == EPCGDataUsage::Settings;
		});
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
			return Data.Usage == EPCGDataUsage::Settings &&
				(Data.Data->GetClass() == InDefaultSettings->GetClass() ||
					Data.Data->GetClass()->IsChildOf(InDefaultSettings->GetClass()));
			});

		return MatchingData ? Cast<const UPCGSettings>(MatchingData->Data) : InDefaultSettings;
	}
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetInputs(const FPCGDataCollection& InCollection)
{
	return InCollection.GetInputs();
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetTaggedInputs(const FPCGDataCollection& InCollection, const FString& InTag)
{
	return InCollection.GetTaggedInputs(InTag);
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetExclusions(const FPCGDataCollection& InCollection)
{
	return InCollection.GetExclusions();
}

TArray<FPCGTaggedData> UPCGDataFunctionLibrary::GetAllSettings(const FPCGDataCollection& InCollection)
{
	return InCollection.GetAllSettings();
}