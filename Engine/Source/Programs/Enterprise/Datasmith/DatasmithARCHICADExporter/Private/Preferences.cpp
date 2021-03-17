// Copyright Epic Games, Inc. All Rights Reserved.

#include "Preferences.h"

BEGIN_NAMESPACE_UE_AC

// Return the preference singleton object
FPreferences* FPreferences::Get()
{
	if (Preferences == nullptr)
	{
		Preferences = new FPreferences();
	}
	return Preferences;
}

// Constructor... Load the preference
FPreferences::FPreferences()
	: SyncOptions(FOptions::kSync)
	, ExportOptions(FOptions::kExport)
{
	UE_AC_Assert(Preferences == nullptr); // FPreferences is a singleton

	Int32		   Version = 0;
	GSSize		   NbBytes = 0;
	unsigned short PlatformSign = GS::Act_Platform_Sign;

	Zap(&Prefs);
	GSErrCode GSErr = ACAPI_GetPreferences_Platform(&Version, &NbBytes, nullptr, nullptr);
	if (GSErr == GS::NoError)
	{
		if (Version == kCurrentVersion)
		{
			try
			{
				CReader Reader(NbBytes);
				ACAPI_GetPreferences_Platform(&Version, &NbBytes, Reader.GetBuffer(), &PlatformSign);
				Reader.ReadFrom(&Prefs);
				UE_AC_Assert(SyncOptions.ReadFrom(&Reader) != 0);
				UE_AC_Assert(ExportOptions.ReadFrom(&Reader) != 0);
				UE_AC_Assert(Reader.GetPos() == NbBytes);
			}
			catch (...)
			{
				UE_AC_DebugF("FPreferences::FPreferences - Invalid preferences data\n");
				Zap(&Prefs);
				SyncOptions = FOptions(FOptions::kSync);
				ExportOptions = FOptions(FOptions::kExport);
			}
		}
		else
		{
			UE_AC_DebugF("FPreferences::FPreferences - Invalid version(%d)\n", Version);
		}
	}
	else
	{
		UE_AC_DebugF("FPreferences::FPreferences - Error get preferences %d\n", GSErr);
	}
}

// Destructor
FPreferences::~FPreferences()
{
	if (Preferences == this)
	{
		// Clear the singleton pointer
		Preferences = nullptr;
	}
}

// Delete the singleton
void FPreferences::Delete()
{
	delete Preferences;
	Preferences = nullptr;
}

// Write to pref
void FPreferences::Write()
{
	CSaver Saver(1024);
	Saver.SaveTo(Prefs);
	SyncOptions.SaveTo(&Saver);
	ExportOptions.SaveTo(&Saver);
	GSErrCode GSErr = ACAPI_SetPreferences(kCurrentVersion, (GS::GSSize)Saver.GetPos(),
										   Saver.GetBuffer()); // save the dialog settings
	if (GSErr != NoError)
	{
		UE_AC_DebugF("FPreferences::Write - Error %d\n", GSErr);
	}
}

// The singleton
FPreferences* FPreferences::Preferences = nullptr;

END_NAMESPACE_UE_AC
