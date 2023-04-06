// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorFilterUtils.h"

#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFixturePatchCellAttributeFader.h"
#include "DMXControlConsoleFixturePatchFunctionFader.h"
#include "DMXControlConsoleRawFader.h"
#include "DMXEditorUtils.h"
#include "Library/DMXEntityFixturePatch.h"


namespace UE::DMX::ControlConsoleEditor::FilterUtils::Private::Impl
{
	/** Tests if the Fader's Attribute Name matches the given filter string. True if filter matches Fader's AttributeName. */
	bool DoesFaderAttributeNameMatchFilter(const FString& InFilterString, UDMXControlConsoleFaderBase* Fader)
	{
		const TArray<FString> AttributeNames = FDMXEditorUtils::ParseAttributeNames(InFilterString);
		if (!AttributeNames.IsEmpty())
		{
			FString AttributeNameOfFader;
			if (UDMXControlConsoleFixturePatchFunctionFader* FixturePatchFunctionFader = Cast<UDMXControlConsoleFixturePatchFunctionFader>(Fader))
			{
				AttributeNameOfFader = FixturePatchFunctionFader->GetAttributeName().Name.ToString();
			}
			else if (UDMXControlConsoleFixturePatchCellAttributeFader* FixturePatchCellAttribute = Cast<UDMXControlConsoleFixturePatchCellAttributeFader>(Fader))
			{
				AttributeNameOfFader = FixturePatchCellAttribute->GetAttributeName().Name.ToString();
			}
			else if (UDMXControlConsoleRawFader* RawFader = Cast<UDMXControlConsoleRawFader>(Fader))
			{
				AttributeNameOfFader = RawFader->GetFaderName();
			}

			if (!AttributeNameOfFader.IsEmpty())
			{
				for (const FString& AttributeName : AttributeNames)
				{
					if (AttributeNameOfFader.Contains(AttributeName))
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	/** Tests if the Fader's Universe and Address matches the given filter string. True if filter matches Fader's Universe ID and Address. */
	bool DoesFaderUniverseAndAddressMatchFilter(const FString& InFilterString, UDMXControlConsoleFaderBase* Fader)
	{
		// Universe
		const TArray<int32> Universes = FDMXEditorUtils::ParseUniverses(InFilterString);
		if (!Universes.IsEmpty() && Universes.Contains(Fader->GetUniverseID()))
		{
			return true;
		}

		// Address
		int32 Address;
		if (FDMXEditorUtils::ParseAddress(InFilterString, Address))
		{
			if (!Universes.IsEmpty() && Universes.Contains(Fader->GetUniverseID()) && Address == Fader->GetStartingAddress())
			{
				return true;
			}
		}

		return false;
	}

	/** Tests if the Fader matches the given filter string. True if filter matches Fader's Fixture ID. */
	bool DoesFaderFixtureIDMatchFilter(const FString& InFilterString, UDMXControlConsoleFaderBase* Fader)
	{
		bool bFoundFixtureIDs = false;
		const TArray<int32> FixtureIDs = FDMXEditorUtils::ParseFixtureIDs(InFilterString);
		for (int32 FixtureID : FixtureIDs)
		{
			const UDMXEntityFixturePatch* FixturePatch = Fader->GetOwnerFaderGroupChecked().GetFixturePatch();
			int32 FixtureIDOfPatch;
			if (FixturePatch &&
				FixturePatch->FindFixtureID(FixtureIDOfPatch) &&
				FixtureIDOfPatch == FixtureID)
			{
				bFoundFixtureIDs = true;
			}
		}
		if (bFoundFixtureIDs)
		{
			return true;
		}

		return false;
	}
}

bool UE::DMX::ControlConsoleEditor::FilterUtils::Private::DoesFaderGroupMatchFilter(const FString& InFilterString, const UDMXControlConsoleFaderGroup* FaderGroup)
{
	return IsValid(FaderGroup) ? FaderGroup->GetFaderGroupName().Contains(InFilterString) : false;
}

bool UE::DMX::ControlConsoleEditor::FilterUtils::Private::DoesFaderMatchFilter(const FString& InFilterString, UDMXControlConsoleFaderBase* Fader)
{
	using namespace UE::DMX::ControlConsoleEditor::FilterUtils::Private::Impl;

	if (!Fader)
	{
		return false;
	}

	if (InFilterString.IsEmpty())
	{
		return true;
	}

	// Filter and return in order of precendence
	bool bMatchFilter = false;

	// Attribute Name
	bMatchFilter = DoesFaderAttributeNameMatchFilter(InFilterString, Fader);
	if (bMatchFilter)
	{
		return true;
	}

	//Universe an Address
	bMatchFilter = DoesFaderUniverseAndAddressMatchFilter(InFilterString, Fader);
	if (bMatchFilter)
	{
		return true;
	}

	// Fixture ID
	bMatchFilter = DoesFaderFixtureIDMatchFilter(InFilterString, Fader);
	if (bMatchFilter)
	{
		return true;
	}

	return false;
}
