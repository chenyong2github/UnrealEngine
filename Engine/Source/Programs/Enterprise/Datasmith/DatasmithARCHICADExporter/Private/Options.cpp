// Copyright Epic Games, Inc. All Rights Reserved.

#include "Options.h"
#include "Preferences.h"

BEGIN_NAMESPACE_UE_AC

FOptions::FOptions(ETypeOptions /* InType */) {}

// Save options, return size saved (throw if not enough space)
size_t FOptions::SaveTo(CSaver* IOSaver) const
{
	size_t startPos = IOSaver->GetPos();
	IOSaver->SaveTo(kVersion);
	IOSaver->SaveTo(SpareFlags1);
	IOSaver->SaveTo(SpareFlags2);
	IOSaver->SaveTo(SpareFlags3);
	IOSaver->SaveTo(SpareString1);
	IOSaver->SaveTo(SpareString2);
	IOSaver->SaveTo(SpareString3);

	return IOSaver->GetPos() - startPos;
}

// Read options, return size read or 0 if invalide (or throw if InFromSize too small)
size_t FOptions::ReadFrom(CReader* IOReader)
{
	size_t startPos = IOReader->GetPos();
	int	   version;
	IOReader->ReadFrom(&version);
	if (version != kVersion)
	{
		UE_AC_TraceF("Options version differ (%d != %d)\n", version, kVersion);
		return 0;
	}
	IOReader->ReadFrom(&SpareFlags1);
	IOReader->ReadFrom(&SpareFlags1);
	IOReader->ReadFrom(&SpareFlags1);
	IOReader->ReadFrom(&SpareString1);
	IOReader->ReadFrom(&SpareString2);
	IOReader->ReadFrom(&SpareString3);

	return IOReader->GetPos() - startPos;
}

const GS::UniString& FOptions::TypeOptionsName(ETypeOptions InType)
{
	static const GS::UniString SyncOptions("SyncOptions");
	static const GS::UniString ExportOptions("ExportOptions");

	return InType == kSync ? SyncOptions : ExportOptions;
}

// Get saved options of the last sync
bool FOptions::GetFromModuleData(ETypeOptions InType)
{
	API_ModulData ModulData;
	Zap(&ModulData);
	GS::UniString OptTypeName(TypeOptionsName(InType));
	GSErrCode	  GSErr = ACAPI_ModulData_GetInfo(&ModulData, OptTypeName);
	if (GSErr == NoError)
	{
		if (ModulData.dataVersion == kVersion)
		{
			GSErr = ACAPI_ModulData_Get(&ModulData, OptTypeName);
			if (GSErr == NoError)
			{
				FAutoHandle AutoHandle(ModulData.dataHdl);

				try
				{
					CReader Reader(BMGetHandleSize(ModulData.dataHdl), *ModulData.dataHdl);
					ReadFrom(&Reader);
					return true;
				}
				catch (...)
				{
					UE_AC_DebugF("FOptions::GetFromModuleData - Caught an exception when reading\n");
				}
			}
			else
			{
				UE_AC_DebugF("FOptions::GetFromModuleData - Can't access to identity data (%d)\n", GSErr);
			}
		}
		else
		{
			UE_AC_DebugF("FOptions::GetFromModuleData - Invalid version (%d)\n", ModulData.dataVersion);
		}
	}
	else if (GSErr != APIERR_NOMODULEDATA)
	{
		UE_AC_DebugF("FOptions::GetFromModuleData - Can't access to identity module data (%d)\n", GSErr);
	}

	// No valid options, get the options from preferences
	if (InType == kSync)
	{
		*this = FPreferences::Get()->SyncOptions;
	}
	else
	{
		*this = FPreferences::Get()->ExportOptions;
	}
	return false;
}

// Save options
bool FOptions::SetToModuleData(ETypeOptions InType) const
{
	API_ModulData ModulData;
	Zap(&ModulData);
	ModulData.dataVersion = kVersion;
	ModulData.platformSign = GS::Act_Platform_Sign;
	CSaver CalcSize;
	SaveTo(&CalcSize);
	GSSize RequiredSize = (GSSize)CalcSize.GetPos();
	ModulData.dataHdl = BMAllocateHandle(RequiredSize, 0, 0);
	if (ModulData.dataHdl != NULL)
	{
		FAutoHandle AutoHandle(ModulData.dataHdl);

		CSaver Saver(RequiredSize, *ModulData.dataHdl);
		SaveTo(&Saver);
		GSErrCode GSErr = ACAPI_ModulData_Store(&ModulData, TypeOptionsName(InType));
		if (GSErr == NoError)
		{
			return true;
		}
		UE_AC_DebugF("FOptions::SetToModuleData - Can't store data (%d)\n", GSErr);
	}
	else
	{
		UE_AC_DebugF("FOptions::SetToModuleData - Can't allocate (%d)\n", RequiredSize);
	}
	return false;
}

END_NAMESPACE_UE_AC
