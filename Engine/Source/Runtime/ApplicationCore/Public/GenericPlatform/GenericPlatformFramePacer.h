// Copyright Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	GenericPlatformFramePacer.h: Generic platform frame pacer classes
==============================================================================================*/


#pragma once

#include "CoreTypes.h"
#include "CoreFwd.h"

/**
 * Generic implementation for most platforms, these tend to be unused and unimplemented
 **/
struct APPLICATIONCORE_API FGenericPlatformRHIFramePacer
{
    /**
     * Should the Frame Pacer be enabled?
     */
    bool IsEnabled() { return false; }

    /**
     * Teardown the Frame Pacer.
     */
    static void Destroy() {}
	
	/**
	 * The pace we are running at (30 = 30fps, 0 = unpaced)
	 * The generic implementation returns a result based on rhi.SyncInterval assuming a 60Hz native refresh rate.
	 */
	static int32 GetFramePace();

	/**
	 * Sets the pace we would like to running at (30 = 30fps, 0 = unpaced).
	 * The generic implementation sets the value for rhi.SyncInterval assuming a 60Hz native refresh rate.
	 *
	 * @return the pace we will run at.
	 */
	static int32 SetFramePace(int32 FramePace);

	/**
	 * Rethers whether the hardware is able to frame pace at the specificed frame rate
	 */
	static bool SupportsFramePace(int32 QueryFramePace);

protected:
	/**
	 * The generic implementation returns a result based on rhi.SyncInterval assuming a 60Hz native refresh rate.
	 */
	static int32 GetFramePaceFromSyncInterval();

	/**
	 * The generic sets rhi.SyncInterval assuming a 60Hz native refresh rate.
	 *
	 * @return the pace we will run at.
	 */
	static int32 SetFramePaceToSyncInterval(int32 FramePace);
};
