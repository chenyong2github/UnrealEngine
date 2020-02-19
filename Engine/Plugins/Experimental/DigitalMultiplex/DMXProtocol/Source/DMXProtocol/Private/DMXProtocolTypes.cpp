// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolSettings.h"

TArray<FName> FDMXProtocolName::GetPossibleValues()
{
	return IDMXProtocol::GetProtocolNames();
}

FDMXProtocolName::FDMXProtocolName(TSharedPtr<IDMXProtocol> InProtocol)
{
	if (InProtocol.IsValid())
	{
		Name = InProtocol->GetProtocolName();
	}
}

FDMXProtocolName::FDMXProtocolName(const FName& InName)
	: Name(InName)
{}

TSharedPtr<IDMXProtocol> FDMXProtocolName::GetProtocol() const
{
	if (Name.IsNone())
	{
		return nullptr; 
	}
	return IDMXProtocol::Get(Name);
}

bool FDMXProtocolName::IsValid() const
{
	return GetProtocol().IsValid();
}

FSimpleMulticastDelegate FDMXFixtureCategory::OnPossibleValuesUpdated;

TArray<FName> FDMXFixtureCategory::GetPossibleValues()
{
	return GetDefault<UDMXProtocolSettings>()->FixtureCategories.Array();
}

FName FDMXFixtureCategory::GetFirstValue()
{
	const TSet<FName>& FixtureCategories = GetDefault<UDMXProtocolSettings>()->FixtureCategories;

	for (const auto& Itt : FixtureCategories)
	{
		return Itt;
	}

	return FName();
}

FDMXFixtureCategory::FDMXFixtureCategory()
{
	Name = GetFirstValue();
}

FDMXFixtureCategory::FDMXFixtureCategory(const FName& InName)
	: Name(InName)
{}

FString UDMXNameContainersConversions::Conv_DMXProtocolNameToString(const FDMXProtocolName & InProtocolName)
{
	return InProtocolName.Name.ToString();
}

FName UDMXNameContainersConversions::Conv_DMXProtocolNameToName(const FDMXProtocolName & InProtocolName)
{
	return InProtocolName.Name;
}

FString UDMXNameContainersConversions::Conv_DMXFixtureCategoryToString(const FDMXFixtureCategory & InFixtureCategory)
{
	return InFixtureCategory.Name.ToString();
}

FName UDMXNameContainersConversions::Conv_DMXFixtureCategoryToName(const FDMXFixtureCategory & InFixtureCategory)
{
	return InFixtureCategory.Name;
}

bool FDMXBuffer::SetDMXFragment(const IDMXFragmentMap & InDMXFragment)
{
	for (const TPair<uint32, uint8>& It : InDMXFragment)
	{
		if (It.Key <= (DMX_UNIVERSE_SIZE) && It.Key > 0)
		{
			DMXData[It.Key-1] = It.Value;
		}
		else
		{
			return false;
		}
	}

	// Increase Sequence
	SequanceID++;

	return true;
}

bool FDMXBuffer::SetDMXBuffer(const uint8* InBuffer, uint32 InSize)
{
	if (InSize <= (DMX_UNIVERSE_SIZE) && InSize > 0)
	{
		FMemory::Memcpy(DMXData.GetData(), InBuffer, InSize);
	}
	else
	{
		return false;
	}

	// Increase Sequence
	SequanceID++;

	return true;
}
