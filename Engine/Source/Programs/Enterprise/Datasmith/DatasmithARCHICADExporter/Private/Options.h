// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils/SaverReader.h"

BEGIN_NAMESPACE_UE_AC

// Class for options
class FOptions
{
  public:
	// Current version
	enum EVersion : int
	{
		kVersion = 0x100
	};
	enum ETypeOptions : unsigned char
	{
		kSync,
		kExport
	};

	// Constructor
	FOptions(ETypeOptions InType);

	// Save options, return size saved (throw if not enough space)
	size_t SaveTo(CSaver* IOSaver) const;

	// Read options, return size read (throw if invalid)
	size_t ReadFrom(CReader* IOReader);

	// Get saved option of the last sync
	bool GetFromModuleData(ETypeOptions InType);

	// Save option
	bool SetToModuleData(ETypeOptions InType) const;

	static const GS::UniString& TypeOptionsName(ETypeOptions InType);

	// Options currently nothing except some reserved values
	uint64		SpareFlags1 = 0;
	uint64		SpareFlags2 = 0;
	uint64		SpareFlags3 = 0;
	utf8_string SpareString1;
	utf8_string SpareString2;
	utf8_string SpareString3;
};

END_NAMESPACE_UE_AC
