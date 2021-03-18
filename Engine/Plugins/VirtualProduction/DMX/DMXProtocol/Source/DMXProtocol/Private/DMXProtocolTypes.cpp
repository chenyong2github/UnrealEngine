// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolSettings.h"

IMPLEMENT_DMX_NAMELISTITEM_STATICVARS(FDMXProtocolName)

IMPLEMENT_DMX_NAMELISTITEM_GetAllValues(FDMXProtocolName)
{
	return IDMXProtocol::GetProtocolNames();
}

IMPLEMENT_DMX_NAMELISTITEM_IsValid(FDMXProtocolName)
{
	if (InName.IsNone())
	{
		return false;
	}
	return IDMXProtocol::Get(InName).IsValid();
}

FDMXProtocolName::FDMXProtocolName(IDMXProtocolPtr InProtocol)
{
	if (InProtocol.IsValid())
	{
		Name = InProtocol->GetProtocolName();
	}
}

FDMXProtocolName::FDMXProtocolName(const FName& InName)
{
	Name = InName;
}

FDMXProtocolName::FDMXProtocolName()
{
	// GetFirstProtocolName depends on the FDMXProtocolModule.
	// This can be called on CDO creation, when the module might not be available yet.
	// So we first check if it is available.
	const IModuleInterface* DMXProtocolModule = FModuleManager::Get().GetModule("DMXProtocol");
	if (DMXProtocolModule != nullptr)
	{
		Name = IDMXProtocol::GetFirstProtocolName();
		return;
	}

	Name = NAME_None;
}

IDMXProtocolPtr FDMXProtocolName::GetProtocol() const
{
	if (Name.IsNone())
	{
		return nullptr; 
	}
	return IDMXProtocol::Get(Name);
}

IMPLEMENT_DMX_NAMELISTITEM_STATICVARS(FDMXFixtureCategory)

IMPLEMENT_DMX_NAMELISTITEM_GetAllValues(FDMXFixtureCategory)
{
	return GetDefault<UDMXProtocolSettings>()->FixtureCategories.Array();
}

IMPLEMENT_DMX_NAMELISTITEM_IsValid(FDMXFixtureCategory)
{
	const TArray<FName>&& AvailableNames = GetPossibleValues();
	for (const FName& AvailableName : AvailableNames)
	{
		if (InName.IsEqual(AvailableName))
		{
			return true;
		}
	}

	return false;
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
{
	Name = InName;
}

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
