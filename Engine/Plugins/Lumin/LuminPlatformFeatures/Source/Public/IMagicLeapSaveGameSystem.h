// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SaveGameSystem.h"

class FMagicLeapSaveGameSystem : public ISaveGameSystem
{
public:
	FMagicLeapSaveGameSystem();
	virtual ~FMagicLeapSaveGameSystem();

	// ISaveGameSystem interface
	virtual bool PlatformHasNativeUI() override
	{
		return false;
	}

	virtual bool DoesSaveGameExist(const TCHAR* Name, const int32 UserIndex) override
	{
		return ESaveExistsResult::OK == DoesSaveGameExistWithResult(Name, UserIndex);
	}

	virtual ESaveExistsResult DoesSaveGameExistWithResult(const TCHAR* Name, const int32 UserIndex) override;

	virtual bool SaveGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, const TArray<uint8>& Data) override;

	virtual bool LoadGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, TArray<uint8>& Data) override;

	virtual bool DeleteGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex) override;
};
