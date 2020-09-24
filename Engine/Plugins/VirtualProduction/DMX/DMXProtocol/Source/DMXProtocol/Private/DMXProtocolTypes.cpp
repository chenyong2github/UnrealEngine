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
	const IModuleInterface* DMXProtocolModule = FModuleManager::Get().GetModule(FDMXProtocolModule::BaseModuleName);
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

uint8 FDMXBuffer::GetDMXDataAddress(uint32 InAddress) const
{
	FScopeLock BufferLock(&BufferCritSec);
	return DMXData[InAddress];
}

void FDMXBuffer::AccessDMXData(TFunctionRef<void(TArray<uint8>&)> InFunction)
{
	FScopeLock BufferLock(&BufferCritSec);
	InFunction(DMXData);
}

bool FDMXBuffer::SetDMXFragment(const IDMXFragmentMap & InDMXFragment)
{
	FScopeLock BufferLock(&BufferCritSec);

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

	// Increase Sequence ID
	SequenceID++;

	return true;
}

bool FDMXBuffer::SetDMXBuffer(const uint8* InBuffer, uint32 InSize)
{
	FScopeLock BufferLock(&BufferCritSec);

	if (InSize <= (DMX_UNIVERSE_SIZE) && InSize > 0)
	{
		FMemory::Memcpy(DMXData.GetData(), InBuffer, InSize);
	}
	else
	{
		return false;
	}

	// Increase Sequence ID
	SequenceID++;

	return true;
}

void FDMXBuffer::ZeroDMXBuffer()
{
	FScopeLock BufferLock(&BufferCritSec);

	FMemory::Memset(DMXData.GetData(), 0, DMX_UNIVERSE_SIZE);

	// Increase Sequence ID
	SequenceID++;
}
