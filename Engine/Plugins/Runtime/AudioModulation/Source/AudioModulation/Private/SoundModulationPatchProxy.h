// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioModulation.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationParameter.h"
#include "SoundModulationProxy.h"
#include "SoundModulationGeneratorLFOProxy.h"
#include "Templates/Function.h"


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

	/** Patch applied as the final stage of a modulation chain prior to output on the sound level (Always active, never removed) */
	struct FModulationOutputProxy
	{
		FModulationOutputProxy() = default;
		FModulationOutputProxy(FSoundModulationOutputTransform InTransform, float InDefaultValue, const Audio::FModulationMixFunction& InMixFunction);

		/** Whether patch has been initialized or not */
		bool bInitialized = false;

		/** Cached value of sample-and-hold input values */
		float SampleAndHoldValue = 1.0f;

		/** Function used to mix values together */
		Audio::FModulationMixFunction MixFunction;

		/** Default value if no inputs are provided */
		float DefaultValue = 1.0f;

		/** Final transform before passing to output */
		FSoundModulationOutputTransform Transform;
	};

	struct FModulationPatchSettings : public TModulatorBase<FPatchId>
	{
		float DefaultInputValue = 1.0f;
		float DefaultOutputValue = 1.0f;

		TArray<FModulationInputSettings> InputSettings;
		bool bBypass = true;

		/** Final transform before passing to output */
		FSoundModulationOutputTransform Transform;

		/** Function used to mix patch inputs together */
		Audio::FModulationMixFunction MixFunction;

		FModulationPatchSettings() = default;

		FModulationPatchSettings(const FSoundControlModulationPatch& InPatch)
			: bBypass(InPatch.bBypass)
			, Transform(InPatch.Transform)
		{
			if (InPatch.InputParameter)
			{
				DefaultInputValue = InPatch.InputParameter->Settings.ValueLinear;
				MixFunction = InPatch.InputParameter->GetMixFunction();
			}

			if (InPatch.OutputParameter)
			{
				DefaultOutputValue = InPatch.OutputParameter->Settings.ValueLinear;
			}

			for (const FSoundControlModulationInput& Input : InPatch.Inputs)
			{
				if (Input.GetBus())
				{
					InputSettings.Emplace(Input);
				}
			}
		}

		FModulationPatchSettings(const FSoundModulationPatchBase& InPatch)
			: DefaultInputValue(InPatch.GetDefaultInputValue())
			, bBypass(InPatch.bBypass)
			, Transform(InPatch.GetOutputChecked().Transform)
			, MixFunction(InPatch.GetMixFunction())
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
			, bBypass(InPatch.PatchSettings.bBypass)
			, Transform(InPatch.PatchSettings.Transform)
		{
			if (USoundModulationParameter* Parameter = InPatch.PatchSettings.InputParameter)
			{
				MixFunction = Parameter->GetMixFunction();
			}

			DefaultInputValue = 1.0f;
			if (USoundModulationParameter* Parameter = InPatch.PatchSettings.InputParameter)
			{
				DefaultInputValue = Parameter->Settings.ValueLinear;
			}

			for (const FSoundControlModulationInput& Input : InPatch.PatchSettings.Inputs)
			{
				if (Input.GetBus())
				{
					InputSettings.Emplace(Input);
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