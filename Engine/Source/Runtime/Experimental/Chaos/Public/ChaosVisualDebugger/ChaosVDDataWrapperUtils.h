// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CHAOS_VISUAL_DEBUGGER
#include "Chaos/ParticleHandleFwd.h"

class FChaosVisualDebuggerTrace;
struct FChaosVDParticleDataWrapper;

/**
 * Helper class used to build Chaos Visual Debugger data wrappers, without directly referencing chaos' types in them.
 *
 * @note: This is needed for now because we want to keep the data wrapper structs/classes on a separate module where possible, but if we reference Chaos's types
 * directly we will end up with a circular dependency issue because the ChaosVDRuntime module will need the Chaos module but the Chaos module will need the ChaosVDRuntime module to use the structs
 * Once development is done and can we commit to backward compatibility, this helper class might go away (trough the proper deprecation process)
 */
class FChaosVDDataWrapperUtils
{
private:
	/** Creates and populates a FChaosVDParticleDataWrapper with the data of the provided ParticleHandlePtr */
	static FChaosVDParticleDataWrapper BuildParticleDataWrapperFromParticle(const Chaos::FGeometryParticleHandle* ParticleHandlePtr);

	friend FChaosVisualDebuggerTrace;
};
#endif //WITH_CHAOS_VISUAL_DEBUGGER
