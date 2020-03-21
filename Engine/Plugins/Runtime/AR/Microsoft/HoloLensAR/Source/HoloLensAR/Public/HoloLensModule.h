// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Features/IModularFeature.h"
#include "HoloLensARSystem.h"
#include "WindowsMixedRealityHMD/Public/IWindowsMixedRealityHMDPlugin.h"

DECLARE_LOG_CATEGORY_EXTERN(LogHoloLensAR, Log, All);

class IXRTrackingSystem;
namespace WindowsMixedReality
{
	class MixedRealityInterop;
}

class HOLOLENSAR_API FHoloLensModuleAR :
	public IHoloLensModuleARInterface
{
public:
	static TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> GetHoloLensARSystem();

	/** Used to init our AR system */
	virtual class IARSystemSupport* CreateARSystem() override;

	virtual void SetTrackingSystem(TSharedPtr<class FXRTrackingSystemBase, ESPMode::ThreadSafe> InTrackingSystem) override;
	virtual void SetInterop(WindowsMixedReality::MixedRealityInterop* InWMRInterop) override;

	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	/** Used to stop our AR system before shutdown */
	void PreExit();
private:
	static TSharedPtr<FHoloLensARSystem, ESPMode::ThreadSafe> ARSystem;
};
