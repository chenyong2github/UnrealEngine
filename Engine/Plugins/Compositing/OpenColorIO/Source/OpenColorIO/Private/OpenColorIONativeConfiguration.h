// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

#if WITH_OCIO
#include "OpenColorIO/OpenColorIO.h"
#endif

// Note: Currently can't delay load OCIO_NAMESPACE::ROLE_INTERCHANGE_SCENE so we define it here
static constexpr ANSICHAR OpenColorIOInterchangeName[] = "aces_interchange";

class FOpenColorIONativeConfiguration {
public:
#if WITH_OCIO
	/* Native config object getter. */
	OCIO_NAMESPACE::ConstConfigRcPtr Get() const;

	/* Native config object setter. */
	void Set(OCIO_NAMESPACE::ConstConfigRcPtr InConfig);

private:
	/* Loaded native config object. */
	OCIO_NAMESPACE::ConstConfigRcPtr Config;
#endif
};


class FOpenColorIONativeInterchangeConfiguration {
public:
#if WITH_OCIO
	FOpenColorIONativeInterchangeConfiguration();

	/* Native config object getter. */
	OCIO_NAMESPACE::ConstConfigRcPtr Get() const;

private:
	/** Minimal config used for CPU-side conversions between the working color space and the interchange one. */
	OCIO_NAMESPACE::ConfigRcPtr Config;
#endif
};