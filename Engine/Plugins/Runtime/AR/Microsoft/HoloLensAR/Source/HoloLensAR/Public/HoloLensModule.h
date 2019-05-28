// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Features/IModularFeature.h"
#include "HoloLensARSystem.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHoloLensAR, Log, All);

class IXRTrackingSystem;
namespace WindowsMixedReality
{
	class MixedRealityInterop;
}

class HOLOLENSAR_API FHoloLensModuleAR :
	public IModuleInterface
{
public:
	/** Used to init our AR system */
	static IARSystemSupport* CreateARSystem();

	static TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> GetHoloLensARSystem();
	
	static void SetTrackingSystem(TSharedPtr<FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem);
	static void SetInterop(WindowsMixedReality::MixedRealityInterop* InWMRInterop);

	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	/** Used to stop our AR system before shutdown */
	void PreExit();
private:
	static TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem;
};
