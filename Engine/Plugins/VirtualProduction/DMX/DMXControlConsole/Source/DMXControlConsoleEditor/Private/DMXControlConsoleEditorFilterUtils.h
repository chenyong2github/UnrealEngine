// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroup;

class FString;


/** Utility for apply filters to DMX Control Console elements */
namespace UE::DMX::ControlConsoleEditor::FilterUtils::Private
{
	/** Tests if the Fader Group matches the given filter string. True if filter matches Fader Group parameters. */
	bool DoesFaderGroupMatchFilter(const FString& InFilterString, const UDMXControlConsoleFaderGroup* FaderGroup);

	/** Tests if the  Fader matches the given filter string. True if filter matches Fader parameters. */
	bool DoesFaderMatchFilter(const FString& InFilterString, UDMXControlConsoleFaderBase* Fader);
};
