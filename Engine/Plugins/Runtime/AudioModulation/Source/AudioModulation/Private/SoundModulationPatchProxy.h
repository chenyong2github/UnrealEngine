// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SoundControlBusProxy.h"
#include "SoundControlBusMixProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulatorLFOProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	using FPatchId = uint32;
	extern const FPatchId InvalidPatchId;

	/** Modulation input instance */
	class FModulationInputProxy
	{
	public:

		FModulationInputProxy() = default;
		FModulationInputProxy(const FSoundModulationInputBase& Patch, FAudioModulationSystem& ModSystem);

		FBusHandle BusHandle;
		FSoundModulationInputTransform Transform;
		bool bSampleAndHold = false;
	};

	/** Patch applied as the final stage of a modulation chain prior to output on the sound level (Always active, never removed) */
	struct FModulationOutputProxy
	{
		FModulationOutputProxy() = default;
		FModulationOutputProxy(const FSoundModulationOutputBase& Patch);

		/** Whether patch has been initialized or not */
		bool bInitialized = false;

		/** Operator used to calculate the output proxy value */
		ESoundModulatorOperator Operator = ESoundModulatorOperator::Multiply;

		/** Cached value of sample-and-hold input values */
		float SampleAndHoldValue = 1.0f;

		/** Final transform before passing to output */
		FSoundModulationOutputTransform Transform;
	};

	class FModulationPatchProxy
	{
	public:
		FModulationPatchProxy() = default;
		FModulationPatchProxy(const FSoundModulationPatchBase& InPatch, FAudioModulationSystem& InModSystem);

		bool IsBypassed() const;

		// Updates the patch and returns the current value
		float Update();

	protected:
		void Init(const FSoundModulationPatchBase& InPatch, FAudioModulationSystem& InModSystem);

	private:
		/** Default value of patch (Value mixed when inputs are provided or not, regardless of active state)*/
		float DefaultInputValue = 1.0f;

		/** Optional modulation inputs */
		TArray<FModulationInputProxy> InputProxies;

		/** Final output modulation post input combination */
		FModulationOutputProxy OutputProxy;

		/** Bypasses the patch and doesn't update modulation value */
		bool bBypass = true;

		friend class FAudioModulationSystem;
		friend class FModulationSettingsProxy;
	};

	class FModulationPatchRefProxy : public FModulationPatchProxy, public TModulatorProxyRefType<FPatchId, FModulationPatchRefProxy, USoundModulationPatch>
	{
	public:
		FModulationPatchRefProxy();
		FModulationPatchRefProxy(const USoundModulationPatch& InPatch, FAudioModulationSystem& InModSystem);

		FModulationPatchRefProxy& operator =(const USoundModulationPatch& InPatch);
	};

	using FPatchProxyMap = TMap<FPatchId, FModulationPatchRefProxy>;
	using FPatchHandle = TProxyHandle<FPatchId, FModulationPatchRefProxy, USoundModulationPatch>;
} // namespace AudioModulation