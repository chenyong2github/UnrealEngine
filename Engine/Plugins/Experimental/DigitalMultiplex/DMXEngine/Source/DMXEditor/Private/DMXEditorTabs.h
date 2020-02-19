// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**  DMX editor tabs identifiers */
struct FDMXEditorTabs
{
	/**	The tab id for the dmx constollers tab */
	static const FName DMXControllersId;

	/**	The tab id for the dmx fixture types tab */
	static const FName DMXFixtureTypesEditorTabId;

	/**	The tab id for the dmx fixture patch tab */
	static const FName DMXFixturePatchEditorTabId;

	/**	The tab id for the dmx monitor tab */
	static const FName DMXInputConsoleEditorTabId;

	/**	The tab id for the dmx output console tab */
	static const FName DMXOutputConsoleEditorTabId;

	// Disable default constructor
	FDMXEditorTabs() = delete;
};

