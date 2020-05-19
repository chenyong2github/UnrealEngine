// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SoundControlBusProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulatorLFOProxy.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	using FPatchId = uint32;
	extern const FPatchId InvalidPatchId;

	struct FModulationInputSettings
	{
		const FControlBusSettings BusSettings;
		const FSoundModulationInputTransform Transform;
		const uint8 bSampleAndHold : 1;

		FModulationInputSettings(const FSoundModulationInputBase& InInput)
			: BusSettings(FControlBusSettings(InInput.GetBusChecked()))
			, Transform(InInput.Transform)
			, bSampleAndHold(InInput.bSampleAndHold)
		{
		}
	};

	/** Modulation input instance */
	class FModulationInputProxy
	{
	public:
		FModulationInputProxy() = default;
		FModulationInputProxy(const FModulationInputSettings& InSettings, FAudioModulationSystem& OutModSystem);

		FBusHandle BusHandle;

		FSoundModulationInputTransform Transform;
		bool bSampleAndHold = false;
	};

	struct FModulationOutputSettings
	{
		/** Operator used to calculate the output proxy value */
		ESoundModulatorOperator Operator = ESoundModulatorOperator::Multiply;

		/** Final transform before passing to output */
		FSoundModulationOutputTransform Transform;

		FModulationOutputSettings() = default;
		FModulationOutputSettings(const FSoundModulationOutputBase& InOutput);
	};

	/** Patch applied as the final stage of a modulation chain prior to output on the sound level (Always active, never removed) */
	struct FModulationOutputProxy
	{
		FModulationOutputProxy() = default;
		FModulationOutputProxy(const FModulationOutputSettings& InSettings);

		/** Whether patch has been initialized or not */
		bool bInitialized = false;

		/** Cached value of sample-and-hold input values */
		float SampleAndHoldValue = 1.0f;

		/** Cached output settings */
		FModulationOutputSettings Settings;
	};

	struct FModulationPatchSettings : public TModulatorBase<FPatchId>
	{
		float DefaultInputValue = 1.0f;
		TArray<FModulationInputSettings> InputSettings;
		FModulationOutputSettings OutputSettings;
		bool bBypass = true;

		FModulationPatchSettings() = default;

		FModulationPatchSettings(const FSoundModulationPatchBase& InPatch)
			: DefaultInputValue(InPatch.DefaultInputValue)
			, OutputSettings(FModulationOutputSettings(InPatch.GetOutputChecked()))
			, bBypass(InPatch.bBypass)
		{
			TArray<const FSoundModulationInputBase*> Inputs = InPatch.GetInputs();
			for (const FSoundModulationInputBase* Input : Inputs)
			{
				if (Input && Input->GetBus())
				{
					InputSettings.Emplace(*Input);
				}
			}
		}

		FModulationPatchSettings(const USoundModulationPatch& InPatch)
			: TModulatorBase<FPatchId>(InPatch.GetName(), InPatch.GetUniqueID())
			, DefaultInputValue(InPatch.PatchSettings.DefaultInputValue)
			, OutputSettings(FModulationOutputSettings(InPatch.PatchSettings.GetOutputChecked()))
			, bBypass(InPatch.PatchSettings.bBypass)
		{
			TArray<const FSoundModulationInputBase*> Inputs = InPatch.PatchSettings.GetInputs();
			for (const FSoundModulationInputBase* Input : Inputs)
			{
				if (Input && Input->GetBus())
				{
					InputSettings.Emplace(*Input);
				}
			}
		}
	};

	class FModulationPatchProxy
	{
	public:
		FModulationPatchProxy() = default;
		FModulationPatchProxy(const FModulationPatchSettings& InSettings, FAudioModulationSystem& InModSystem);

		/** Whether or not the patch is bypassed (effectively just returning the default value) */
		bool IsBypassed() const;

		/** Returns the value of the patch */
		float GetValue() const;

		/** Updates the patch value */
		void Update();

	protected:
		void Init(const FModulationPatchSettings& InSettings, FAudioModulationSystem& InModSystem);

	private:
		/** Default value of patch (Value mixed when inputs are provided or not, regardless of active state)*/
		float DefaultInputValue = 1.0f;

		/** Current value of the patch */
		float Value = 1.0f;

		/** Optional modulation inputs */
		TArray<FModulationInputProxy> InputProxies;

		/** Final output modulation post input combination */
		FModulationOutputProxy OutputProxy;

		/** Bypasses the patch and doesn't update modulation value */
		bool bBypass = true;

		friend class FAudioModulationSystem;
		friend class FModulationSettingsProxy;
	};

	class FModulationPatchRefProxy : public FModulationPatchProxy, public TModulatorProxyRefType<FPatchId, FModulationPatchRefProxy, FModulationPatchSettings>
	{
	public:
		FModulationPatchRefProxy();
		FModulationPatchRefProxy(const FModulationPatchSettings& InSettings, FAudioModulationSystem& OutModSystem);

		FModulationPatchRefProxy& operator =(const FModulationPatchSettings& InSettings);
	};

	using FPatchProxyMap = TMap<FPatchId, FModulationPatchRefProxy>;
	using FPatchHandle = TProxyHandle<FPatchId, FModulationPatchRefProxy, FModulationPatchSettings>;
} // namespace AudioModulation