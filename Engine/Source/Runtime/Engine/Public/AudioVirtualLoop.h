// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ActiveSound.h"

class FAudioDevice;


/**
 * Class that tracks virtualized looping active sounds that are eligible to revive re-trigger
 * as long as no stop request is received from the game thread.
 */
class ENGINE_API FAudioVirtualLoop
{
	float TimeSinceLastUpdate;
	float UpdateInterval;
	FAudioDevice* AudioDevice;
	FActiveSound* ActiveSound;

	FAudioVirtualLoop(FAudioDevice& AudioDevice, const FActiveSound& NewActiveSound);

public:
	~FAudioVirtualLoop();

	/**
	 * Checks if provided active sound is available to be virtualized.  If so, returns new virtual loop ready to be
	 * added to virtual loop management in audio device.
	 */
	static FAudioVirtualLoop* Virtualize(FAudioDevice& InAudioDevice, const FActiveSound& InActiveSound, bool bDoRangeCheck);

	/**
	 * Returns the internally-managed active sound.
	 */
	FActiveSound& GetActiveSound();

	/**
	 * Check if provided active sound is in audible range.
	 */
	static bool IsInAudibleRange(const FAudioDevice& InAudioDevice, const FActiveSound& ActiveSound);

	/**
	 * Overrides the update interval to the provided length.
	 */
	void CalculateUpdateInterval(bool bIsAtMaxConcurrency = false);

	/**
	  * Updates the loop and checks if ready to play (or 'realize').
	  */
	void Update();

private:
	void SetActiveSound(const FActiveSound& InActiveSound);
};
