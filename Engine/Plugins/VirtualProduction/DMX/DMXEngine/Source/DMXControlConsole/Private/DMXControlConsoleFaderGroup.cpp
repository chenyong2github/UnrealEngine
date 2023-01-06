// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleFaderGroup.h"

#include "DMXAttribute.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXControlConsoleFixturePatchFunctionFader.h"
#include "DMXControlConsoleRawFader.h"
#include "DMXSubsystem.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleFaderGroup"

UDMXControlConsoleRawFader* UDMXControlConsoleFaderGroup::AddRawFader()
{
	int32 Universe;
	int32 Address;
	GetNextAvailableUniverseAndAddress(Universe, Address);

	UDMXControlConsoleRawFader* Fader = NewObject<UDMXControlConsoleRawFader>(this, NAME_None, RF_Transactional);
	Fader->SetUniverseID(Universe);
	Fader->SetAddressRange(Address);
	Faders.Add(Fader);

	return Fader;
}

UDMXControlConsoleFixturePatchFunctionFader* UDMXControlConsoleFaderGroup::AddFixturePatchFunctionFader(const FDMXFixtureFunction& FixtureFunction, const int32 InUniverseID, const int32 StartingChannel)
{
	UDMXControlConsoleFixturePatchFunctionFader* Fader = NewObject<UDMXControlConsoleFixturePatchFunctionFader>(this, NAME_None, RF_Transactional);
	Fader->SetPropertiesFromFixtureFunction(FixtureFunction, InUniverseID, StartingChannel);
	Faders.Add(Fader);

	return Fader;
}

void UDMXControlConsoleFaderGroup::DeleteFader(const TObjectPtr<UDMXControlConsoleFaderBase>& Fader)
{
	if (!ensureMsgf(Fader, TEXT("Invalid fader, cannot delete from '%s'."), *GetName()))
	{
		return;
	}

	if (!ensureMsgf(Faders.Contains(Fader), TEXT("'%s' fader group is not owner of '%s'. Cannot delete fader correctly."), *GetName(), *Fader->GetName()))
	{
		return;
	}

	Faders.Remove(Fader);
}

void UDMXControlConsoleFaderGroup::ClearFaders()
{
	Faders.Reset();
}

int32 UDMXControlConsoleFaderGroup::GetIndex() const
{
	const UDMXControlConsoleFaderGroupRow& FaderGroupRow = GetOwnerFaderGroupRowChecked();

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = FaderGroupRow.GetFaderGroups();
	return FaderGroups.IndexOfByKey(this);
}

UDMXControlConsoleFaderGroupRow& UDMXControlConsoleFaderGroup::GetOwnerFaderGroupRowChecked() const
{
	UDMXControlConsoleFaderGroupRow* Outer = Cast<UDMXControlConsoleFaderGroupRow>(GetOuter());
	checkf(Outer, TEXT("Invalid outer for '%s', cannot get fader group index correctly."), *GetName());

	return *Outer;
}

void UDMXControlConsoleFaderGroup::SetFaderGroupName(const FString& NewName)
{
	FaderGroupName = NewName;
}

void UDMXControlConsoleFaderGroup::GenerateFromFixturePatch(UDMXEntityFixturePatch* InFixturePatch)
{
	if (!InFixturePatch)
	{
		return;
	}

	Modify();

	FixturePatch = InFixturePatch;
	FaderGroupName = FixturePatch->GetDisplayName();
	EditorColor = FixturePatch->EditorColor;
	ClearFaders();

	const TMap<FDMXAttributeName, FDMXFixtureFunction> FunctionsMap = FixturePatch->GetAttributeFunctionsMap();
	for (const TTuple<FDMXAttributeName, FDMXFixtureFunction>& FunctionTuple : FunctionsMap)
	{
		const FDMXFixtureFunction FixtureFunction = FunctionTuple.Value;
		const int32 UniverseID = FixturePatch->GetUniverseID();
		const int32 StartingChannel = FixturePatch->GetStartingChannel();

		AddFixturePatchFunctionFader(FixtureFunction, UniverseID, StartingChannel);
	}

	bForceRefresh = true;
}

bool UDMXControlConsoleFaderGroup::HasFixturePatch() const
{
	return FixturePatch != nullptr;
}

TMap<int32, TMap<int32, uint8>> UDMXControlConsoleFaderGroup::GetUniverseToFragmentMap() const
{
	TMap<int32, TMap<int32, uint8>> UniverseToFragmentMap;

	if (HasFixturePatch())
	{
		return UniverseToFragmentMap;
	}

	UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
	check(DMXSubsystem);

	for (const UDMXControlConsoleFaderBase* Fader : Faders)
	{
		if (!Fader || Fader->IsMuted())
		{
			continue;
		}

		const UDMXControlConsoleRawFader* RawFader = Cast<UDMXControlConsoleRawFader>(Fader);
		TMap<int32, uint8>& FragmentMapRef = UniverseToFragmentMap.FindOrAdd(RawFader->GetUniverseID());

		TArray<uint8> ByteArray;
		DMXSubsystem->IntValueToBytes(RawFader->GetValue(), RawFader->GetDataType(), ByteArray, RawFader->GetUseLSBMode());

		for (int32 ByteIndex = 0; ByteIndex < ByteArray.Num(); ByteIndex++)
		{
			const int32 CurrentAddress = RawFader->GetStartingAddress() + ByteIndex;
			FragmentMapRef.FindOrAdd(CurrentAddress) = ByteArray[ByteIndex];
		}
	}

	return UniverseToFragmentMap;
}

TMap<FDMXAttributeName, int32> UDMXControlConsoleFaderGroup::GetAttributeMap() const
{
	TMap<FDMXAttributeName, int32> AttributeMap;

	if (!HasFixturePatch())
	{
		return AttributeMap;
	}

	for (const UDMXControlConsoleFaderBase* Fader : Faders)
	{
		if (!Fader || Fader->IsMuted())
		{
			continue;
		}

		const UDMXControlConsoleFixturePatchFunctionFader* FixturePatchFunctionFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(Fader);
		const FDMXAttributeName& AttributeName = FixturePatchFunctionFader->GetAttributeName();
		const uint32 Value = FixturePatchFunctionFader->GetValue();
		AttributeMap.Add(AttributeName, Value);
	}

	return AttributeMap;
}

void UDMXControlConsoleFaderGroup::Destroy()
{
	UDMXControlConsoleFaderGroupRow& FaderGroupRow = GetOwnerFaderGroupRowChecked();
	
	FaderGroupRow.PreEditChange(nullptr);

	FaderGroupRow.DeleteFaderGroup(this);

	FaderGroupRow.PostEditChange();
}

void UDMXControlConsoleFaderGroup::ForceRefresh()
{
	bForceRefresh = false;
}

void UDMXControlConsoleFaderGroup::PostInitProperties()
{
	Super::PostInitProperties();

	FaderGroupName = GetName();
}

void UDMXControlConsoleFaderGroup::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GetFixturePatchPropertyName() &&
		PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive &&
		HasFixturePatch())
	{
		FixturePatch = nullptr;
		EditorColor = FLinearColor::White;

		ClearFaders();
	}
}

void UDMXControlConsoleFaderGroup::GetNextAvailableUniverseAndAddress(int32& OutUniverse, int32& OutAddress) const
{
	if (!Faders.IsEmpty())
	{
		const UDMXControlConsoleRawFader* LastFader = Cast<UDMXControlConsoleRawFader>(Faders.Last());
		if (LastFader)
		{
			OutAddress = LastFader->GetEndingAddress() + 1;
			OutUniverse = LastFader->GetUniverseID();
			if (OutAddress > DMX_MAX_ADDRESS)
			{
				OutAddress = 1;
				OutUniverse++;
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
