// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Math/Transform.h"
#include "Delegates/DelegateCombinations.h"

/** IAudioLink
  *	Abstract interface for AudioLink instances. 
  *	Purely opaque.
  */
class AUDIOLINKENGINE_API IAudioLink
{
protected:
	IAudioLink();
public:
	virtual ~IAudioLink() = default;
};

/** IAudioLinkSourcePushed
  *	Where the owning object needs to push it's state
  */
class AUDIOLINKENGINE_API IAudioLinkSourcePushed : public IAudioLink
{
protected:
	IAudioLinkSourcePushed() = default;
public:
	virtual ~IAudioLinkSourcePushed() = default;

	struct FOnUpdateWorldStateParams
	{
		FTransform	WorldTransform;
	};
	virtual void OnUpdateWorldState(const FOnUpdateWorldStateParams&) = 0;

	struct FOnNewBufferParams
	{
		TArrayView<float> Buffer;
		int32 SourceId = INDEX_NONE;
	};
	virtual void OnNewBuffer(const FOnNewBufferParams&) = 0;

	virtual void OnSourceReleased(const int32 SourceId) = 0;	
};

/** IAudioLinkSynchronizer
  *	Provides delegates for hooking and external AudioLinks synchronization callbacks.
  */
class IAudioLinkSynchronizer
{
protected:
	IAudioLinkSynchronizer() = default;
public:
	virtual ~IAudioLinkSynchronizer() = default;

	DECLARE_DELEGATE(FOnBeginRender);
	virtual FOnBeginRender& GetOnBeginRender() = 0;

	DECLARE_DELEGATE(FOnEndRender);	
	virtual FOnEndRender& GetOnEndRender() = 0;
};