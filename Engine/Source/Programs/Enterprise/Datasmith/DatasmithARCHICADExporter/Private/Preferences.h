// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Palette.h"
#include "Options.h"

BEGIN_NAMESPACE_UE_AC

// Class that handle preferences of the addon
class FPreferences
{
  public:
	// Version
	enum
	{
		kCurrentVersion = 1
	};

	// Content of preferences
	struct SPrefs
	{
		FPalette::SPrefs Palette;
	} Prefs;
	FOptions SyncOptions;
	FOptions ExportOptions;

	// Return the preference singleton object
	static FPreferences* Get();

	// Delete the singleton
	static void Delete();

	// Write to prefs
	void Write();

  private:
	// Constructor
	FPreferences();

	// Destructor
	~FPreferences();

	// The singleton
	static FPreferences* Preferences;
};

END_NAMESPACE_UE_AC
