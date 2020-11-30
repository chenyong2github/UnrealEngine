// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"

class FMediaPlayerFacade;
class IMediaOptions;
class IMediaPlayerFactory;

struct FMediaPlayerOptions;

/*
	A single IMediaPlayerLifecycleManagerDelegate can be registered with the MediaModule to monitor and control
	player lifecycles throughout the media framework.
	One possible use case motivating the implementation is to control player creation as system resources are being
	monitored. As this task is highly dependent on knowledge the media framework itself often does not have, only
	an application-supplied delegate can be expected to make reasonably good decisions.

	The delegate will receive notifications about major lifecycle events of all players handled by the framework.

	To identify the instances the implementation should not rely on pointer comparisons (the pointers in question
	may be used in reallocations or may be no longer valid for some notifications), but should rather use the
	supplied 64-bit ID, which is uniquely (enough) generated for each created instance.

	Notes:

	- All notification callbacks are issues on the game thread
	- The PlayerOpen callback will be issued before any player instance has been created, hence the ID is invalid (but a facade may exist)
	-- If the callback returns "true" it must take care to call SubmitOpenRequest() with the passed along open request as soon as it deems it "ok" for the player to serve the request to be created
	-- (if not "submitted" the open will not create an actual player; if "false" is returned the default creation method will continue as if a submit was done)
	- GetFacade() may return a null pointer in case the facade instance was destroyed since the event was triggered and hence should always be checked before use
	- The PlayerDestroyed callback will be triggered after the player is already destroyed. The facade may or may not still exist. The ID is still valid so this event can be properly tracked

*/
//! Interface to receive global player lifetime events from media framework
class IMediaPlayerLifecycleManagerDelegate
{
public:
	//! Request to create and open a player
	class IOpenRequest
	{
	public:
		virtual ~IOpenRequest() {}

		virtual const FString& GetUrl() const = 0;
		virtual const IMediaOptions* GetOptions() const = 0;
		virtual const FMediaPlayerOptions* GetPlayerOptions() const = 0;
		virtual IMediaPlayerFactory* GetPlayerFactory() const = 0;
		virtual bool WillCreateNewPlayer() const = 0;
	};
	typedef TSharedPtr<IOpenRequest, ESPMode::ThreadSafe> IOpenRequestRef;

	//! Control interface for lifecycle delegate
	class IControl
	{
	public:
		virtual ~IControl() {}

		virtual bool SubmitOpenRequest(IOpenRequestRef && OpenRequest) = 0;

		virtual TSharedPtr<FMediaPlayerFacade, ESPMode::ThreadSafe> GetFacade() const = 0;
		virtual uint64 GetMediaPlayerInstanceID() const = 0;
	};
	typedef TSharedPtr<IControl, ESPMode::ThreadSafe> IControlRef;

	virtual ~IMediaPlayerLifecycleManagerDelegate() {}

	virtual bool OnMediaPlayerOpen(IControlRef Control, IOpenRequestRef OpenRequest) = 0;
	virtual void OnMediaPlayerCreated(IControlRef Control) = 0;
	virtual void OnMediaPlayerClosed(IControlRef Control) = 0;
	virtual void OnMediaPlayerDestroyed(IControlRef Control) = 0;
};
