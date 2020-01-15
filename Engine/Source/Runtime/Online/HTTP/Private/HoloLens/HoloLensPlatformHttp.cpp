// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoloLens/HoloLensPlatformHttp.h"
#include "IXML/HttpIXML.h"

void FHoloLensHttp::Init()
{
}

void FHoloLensHttp::Shutdown()
{
}

FHttpManager * FHoloLensHttp::CreatePlatformHttpManager()
{
	return nullptr;
}

IHttpRequest* FHoloLensHttp::ConstructRequest()
{
	return new FHttpRequestIXML();
}