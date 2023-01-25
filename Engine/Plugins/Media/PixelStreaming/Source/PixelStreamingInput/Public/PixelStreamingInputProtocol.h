// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Containers/Map.h"
#include "PixelStreamingInputMessage.h"
#include "Dom/JsonObject.h"

struct PIXELSTREAMINGINPUT_API FPixelStreamingInputProtocol
{
public:
	static TMap<FString, FPixelStreamingInputMessage> ToStreamerProtocol;
	static TMap<FString, FPixelStreamingInputMessage> FromStreamerProtocol;

	static TSharedPtr<FJsonObject> ToJson(EPixelStreamingMessageDirection Direction);
};