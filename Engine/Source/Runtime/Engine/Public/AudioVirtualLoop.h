// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ActiveSound.h"


/**
 * Class that tracks virtualized looping active sounds that are eligible to revive re-trigger
 * as long as no stop request is received from the game thread.
 */
struct ENGINE_API FAudioVirtualLoop
{
private:
	float TimeSinceLastUpdate;
	float UpdateInterval;

	FActiveSound* ActiveSound;

public:
	FAudioVirtualLoop();

	/**
	 * Checks if provided active sound is available to be virtualized.  If so, returns new active sound ready to be
	 * added to virtual loop management by parent audio device.
	 */
	static bool Virtualize(const FActiveSound& InActiveSound, bool bDoRangeCheck, FAudioVirtualLoop& OutVirtualLoop);
	static bool Virtualize(const FActiveSound& InActiveSound, FAudioDevice& AudioDevice, bool bDoRangeCheck, FAudioVirtualLoop& OutVirtualLoop);

	/**
	 * Whether the virtual loop system is enabled or not
	 */
	static bool IsEnabled();

	/**
	 * Returns the internally-managed active sound.
	 */
	FActiveSound& GetActiveSound();

	/**
	 * Returns the wait interval being observed before next update
	 */
	float GetUpdateInterval() const { return UpdateInterval; }

	/**
	 * Returns the internally-managed active sound.
	 */
	const FActiveSound& GetActiveSound() const;

	/**
	 * Check if provided active sound is in audible range.
	 */
	static bool IsInAudibleRange(const FActiveSound& InActiveSound, const FAudioDevice* InAudioDevice = nullptr);

	/**
	 * Overrides the update interval to the provided length.
	 */
	void CalculateUpdateInterval();

	/**
	  * Updates the loop and checks if ready to play (or 'realize').
	  * Returns whether or not the sound is ready to be realized.
	  */
	bool CanRealize(float DeltaTime);
};
