// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "CoreTypes.h"
#include "Logging/LogMacros.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDatasmithDirectLinkExporterAPI, Log, All);


class IDatasmithScene;

namespace DirectLink
{
class FEndpoint;
}

class DATASMITHEXPORTER_API FDatasmithDirectLink
{
public:
	static int32 ValidateCommunicationSetup();
	static bool Shutdown();

public:
	FDatasmithDirectLink();
	~FDatasmithDirectLink();

	bool InitializeForScene(TSharedRef<IDatasmithScene>& Scene);
	bool UpdateScene(TSharedRef<IDatasmithScene>& Scene);

	static TSharedRef<DirectLink::FEndpoint, ESPMode::ThreadSafe> GetEnpoint();
};
