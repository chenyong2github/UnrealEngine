// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#ifdef __EMSCRIPTEN__

#include "CoreMinimal.h"
#include "Misc/Guid.h"

class ITransport;

#include "ITransport.h"

class FHTTPTransport : public ITransport
{

public:

	FHTTPTransport();

	// ITransport Interface.
	virtual bool Initialize(const TCHAR* HostIp) override;
	virtual bool SendPayloadAndReceiveResponse(TArray<uint8>& In, TArray<uint8>& Out) override;
	virtual bool ReceiveResponse(TArray<uint8> &Out) override;

private:

	FGuid Guid;
	TCHAR Url[1048];

	TArray<uint8> ReceiveBuffer;
	uint32 ReadPtr;

};
#endif
