// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXEditorTabs.h"

// 'GenericEditor_Properties' was the name of the original controller editor tab. 
// We keep the old name to show the library editor tab in place of the controller editor in existing projects.
// This is less confusing for users that upgrade their project from 4.26 to 4.27, otherwise the tab that shows
// ports wouldn't be visible by default after the update.
const FName FDMXEditorTabs::DMXLibraryEditorTabId(TEXT("GenericEditor_Properties"));

const FName FDMXEditorTabs::DMXFixtureTypesEditorTabId("GenericEditor_DMXFixtureTypesEditor");
const FName FDMXEditorTabs::DMXFixturePatchEditorTabId("GenericEditor_DMXFixturePatchEditor");
const FName FDMXEditorTabs::DMXOutputConsoleEditorTabId("GenericEditor_DMXOutputConsole");
