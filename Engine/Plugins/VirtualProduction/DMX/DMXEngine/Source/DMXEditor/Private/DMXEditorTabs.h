// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**  DMX editor tabs identifiers */
struct FDMXEditorTabs
{
	/**	The tab id for the dmx library editor tab */
	static const FName DMXLibraryEditorTabId;

	/**	The tab id for the dmx fixture types tab */
	static const FName DMXFixtureTypesEditorTabId;

	/**	The tab id for the dmx fixture patch tab */
	static const FName DMXFixturePatchEditorTabId;

	/**	The tab id for the dmx output console tab */
	static const FName DMXOutputConsoleEditorTabId;

	// Disable default constructor
	FDMXEditorTabs() = delete;
};
