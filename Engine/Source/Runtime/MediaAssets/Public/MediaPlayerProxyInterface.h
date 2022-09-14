// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "MediaPlayerProxyInterface.generated.h"

/**
 * The proxy object provides a higher level of insight/control than just the media player.
 * For example, the object owning the player may control the player in some
 * cases, and the proxy allows you and the object to avoid conflicts in control.
 */
UINTERFACE(MinimalAPI)
class UMediaPlayerProxyInterface : public UInterface
{
	GENERATED_BODY()
};

class MEDIAASSETS_API IMediaPlayerProxyInterface
{
	GENERATED_BODY()

public:
	/**
	 * Call this to see if you can control the media player, or if the owning object is using it.
	 * 
	 * @return				True if you can control the player.
	 */
	virtual bool IsExternalControlAllowed() = 0;
};
