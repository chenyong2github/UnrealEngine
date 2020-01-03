// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMagicLeapPlugin.h"

/**
	The public interface for MagicLeapHMD to control the order of tracker creation, desctruction, provide late update etc.

	Perception related trackers should only be created after a successful call to MLPerceptionStartup() and be destroyed
	before a call to MLPerceptionShutdown(). This interface will ensure that.

	Entities must register with the IMagicLeapPlugin using it's RegisterMagicLeapTrackerEntity() function.
*/
class IMagicLeapTrackerEntity
{
public:
	virtual void CreateEntityTracker() {}
	virtual void DestroyEntityTracker() {}
	virtual void OnBeginRendering_GameThread() {}
};

namespace MagicLeap
{
	/** 
	 * Utility class to scope guard enabling and disabling game viewport client input processing.
	 * On creation it will enable the input processing, and on exit it will restore it to its
	 * previous state. Usage is:
	 *
	 * { MagicLeap::EnableInput EnableInput; PostSomeInputToMessageHandlers(); }
	 */
	struct EnableInput
	{
	#if WITH_EDITOR
		inline EnableInput()
		{
			SavedIgnoreInput = IMagicLeapPlugin::Get().SetIgnoreInput(false);
		}
		inline ~EnableInput()
		{
			IMagicLeapPlugin::Get().SetIgnoreInput(SavedIgnoreInput);
		}

	private:
		bool SavedIgnoreInput;
	#endif
	};
}
