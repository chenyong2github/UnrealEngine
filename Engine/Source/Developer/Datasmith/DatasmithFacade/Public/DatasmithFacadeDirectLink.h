// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithDirectLink.h"

#include "CoreTypes.h"

class FDatasmithFacadeScene;


class DATASMITHFACADE_API FDatasmithFacadeDirectLink
{
public:
	static bool Init();
	static bool Init(bool bUseDatasmithExporterUI, const TCHAR* RemoteEngineDirPath);
	static int ValidateCommunicationSetup() { return FDatasmithDirectLink::ValidateCommunicationSetup(); }
	static bool Shutdown();

	bool InitializeForScene(FDatasmithFacadeScene* FacadeScene);
	bool UpdateScene(FDatasmithFacadeScene* FacadeScene);

private:
	FDatasmithDirectLink Impl;
};