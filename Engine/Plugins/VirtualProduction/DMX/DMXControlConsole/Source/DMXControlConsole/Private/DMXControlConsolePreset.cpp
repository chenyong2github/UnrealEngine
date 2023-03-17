// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsolePreset.h"

#include "DMXControlConsole.h"


#define LOCTEXT_NAMESPACE "DMXControlConsolePreset"

UDMXControlConsolePreset::UDMXControlConsolePreset()
{
	ControlConsole = CreateDefaultSubobject<UDMXControlConsole>(TEXT("ControlConsole"));
}

void UDMXControlConsolePreset::CopyControlConsole(UDMXControlConsole* InControlConsole)
{
	if (!InControlConsole)
	{
		return;
	}

	// Unsaved Control Consoles live in the Transient Package
	if (InControlConsole->GetPackage() == GetTransientPackage())
	{
		ControlConsole = InControlConsole;
	}
	else
	{
		ControlConsole = DuplicateObject<UDMXControlConsole>(InControlConsole, this);
	}

	ControlConsole->Rename(nullptr, this);
	ControlConsole->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
}

void UDMXControlConsolePreset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	if (Ar.IsSaving() && Ar.IsPersistent())
	{
		OnControlConsolePresetSaved.Broadcast(this);
	}
#endif // WITH_EDITOR
}

#undef LOCTEXT_NAMESPACE
