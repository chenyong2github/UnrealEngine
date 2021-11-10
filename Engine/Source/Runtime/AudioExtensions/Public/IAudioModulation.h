// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "DSP/BufferVectorOperations.h"
#include "IAudioExtensionPlugin.h"
#include "IAudioProxyInitializer.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "UObject/NameTypes.h"

#include "IAudioModulation.generated.h"


// Forward Declarations
class IAudioModulation;
class ISoundModulatable;
class USoundModulatorBase;
class UObject;

#if !UE_BUILD_SHIPPING
class FCanvas;
class FCommonViewportClient;
class FViewport;
class UFont;
#endif // !UE_BUILD_SHIPPING

namespace Audio
{
	using FModulatorId = uint32;
	using FModulatorTypeId = uint32;
	using FModulatorHandleId = uint32;

	using FModulationUnitConversionFunction = TFunction<void(float& /* OutValueNormalizedToUnit */)>;
	using FModulationNormalizedConversionFunction = TFunction<void(float& /* OutValueUnitToNormalized */)>;
	using FModulationMixFunction = TFunction<void(float& /* OutNormalizedA */, float /* InNormalizedB */)>;


	struct AUDIOEXTENSIONS_API FModulationParameter
	{
		FModulationParameter();
		FModulationParameter(const FModulationParameter& InParam);
		FModulationParameter(FModulationParameter&& InParam);

		FModulationParameter& operator=(FModulationParameter&& InParam);
		FModulationParameter& operator=(const FModulationParameter& InParam);

		FName ParameterName;

		// Default value of parameter in unit space
		float DefaultValue = 1.0f;

		// Default minimum value of parameter in unit space
		float MinValue = 0.0f;

		// Default minimum value of parameter in unit space
		float MaxValue = 1.0f;

		// Whether or not unit conversion is required
		bool bRequiresConversion = false;

#if WITH_EDITORONLY_DATA
		FText UnitDisplayName;
#endif // WITH_EDITORONLY_DATA

		// Function used to mix normalized values together.
		FModulationMixFunction MixFunction;

		// Function used to convert value buffer from normalized, unitless space [0.0f, 1.0f] to unit space.
		FModulationUnitConversionFunction UnitFunction;

		// Function used to convert value buffer from unit space to normalized, unitless [0.0f, 1.0f] space.
		FModulationNormalizedConversionFunction NormalizedFunction;

		static const FModulationMixFunction& GetDefaultMixFunction();
		static const FModulationUnitConversionFunction& GetDefaultUnitConversionFunction();
		static const FModulationNormalizedConversionFunction& GetDefaultNormalizedConversionFunction();
	};

	/** Handle to a modulator which interacts with the modulation API to manage lifetime of internal objects */
	struct AUDIOEXTENSIONS_API FModulatorHandle
	{
		FModulatorHandle() = default;
		FModulatorHandle(IAudioModulation& InModulation, const USoundModulatorBase* InModulatorBase, FName InParameterName);
		FModulatorHandle(const FModulatorHandle& InOther);
		FModulatorHandle(FModulatorHandle&& InOther);

		~FModulatorHandle();

		FModulatorHandle& operator=(const FModulatorHandle& InOther);
		FModulatorHandle& operator=(FModulatorHandle&& InOther);

		FModulatorId GetModulatorId() const;
		const FModulationParameter& GetParameter() const;
		FModulatorTypeId GetTypeId() const;
		FModulatorHandleId GetHandleId() const;
		bool GetValue(float& OutValue) const;
		bool GetValueThreadSafe(float& OutValue) const;
		bool IsValid() const;

	private:
		FModulationParameter Parameter;
		FModulatorHandleId HandleId = INDEX_NONE;
		FModulatorTypeId ModulatorTypeId = INDEX_NONE;
		FModulatorId ModulatorId = INDEX_NONE;
		TWeakPtr<IAudioModulation> Modulation;
	};
} // namespace Audio

class AUDIOEXTENSIONS_API IAudioModulation : public TSharedFromThis<IAudioModulation>
{
public:
	/** Virtual destructor */
	virtual ~IAudioModulation() { }

	/** Returns parameter info for the given parameter name */
	virtual Audio::FModulationParameter GetParameter(FName InParamName) { return Audio::FModulationParameter(); }

	/** Initialize the modulation plugin with the same rate and number of sources */
	virtual void Initialize(const FAudioPluginInitializationParams& InitializationParams) { }

	virtual void OnAuditionEnd() { }

#if !UE_BUILD_SHIPPING
	/** Request to post help from active plugin (non-shipping builds only) */
	virtual bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; };

	/** Render stats pertaining to modulation (non-shipping builds only) */
	virtual int32 OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }

	/** Toggle showing render stats pertaining to modulation (non-shipping builds only) */
	virtual bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif //!UE_BUILD_SHIPPING

	/** Processes audio with the given input and output data structs.*/
	virtual void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) { }

	/** Processes all modulators Run on the audio render thread prior to processing audio */
	virtual void ProcessModulators(const double InElapsed) { }

	/** Updates modulator definition on the AudioRender Thread with that provided by the UObject representation */
	virtual void UpdateModulator(const USoundModulatorBase& InModulator) { }

protected:
	virtual Audio::FModulatorTypeId RegisterModulator(uint32 InHandleId, const USoundModulatorBase* InModulatorBase, Audio::FModulationParameter& OutParameter) { return INDEX_NONE; }
	virtual void RegisterModulator(uint32 InHandleId, Audio::FModulatorId InModulatorId) { }

	// Get the modulator value from the AudioRender Thread
	virtual bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const { return false; }

	// Get the modulator value from any thread.
	virtual bool GetModulatorValueThreadSafe(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const { return false; }

	virtual void UnregisterModulator(const Audio::FModulatorHandle& InHandle) { }

	friend Audio::FModulatorHandle;
};

/**
 * Base class for all modulators
 */
UCLASS(config = Engine, abstract, editinlinenew, BlueprintType)
class AUDIOEXTENSIONS_API USoundModulatorBase : public UObject, public IAudioProxyDataFactory
{
	GENERATED_BODY()

public:
	virtual FName GetOutputParameterName() const
	{
		return FName();
	}

	virtual TUniquePtr<Audio::IProxyData> CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams) override;
};


/** Proxy to modulator, allowing for modulator to be referenced by the Audio Render Thread independently
  * from the implementing modulation plugin (ex. for MetaSound implementation).
  */
class AUDIOEXTENSIONS_API FSoundModulatorAssetProxy : public Audio::TProxyData<FSoundModulatorAssetProxy>, public TSharedFromThis<FSoundModulatorAssetProxy, ESPMode::ThreadSafe>
{
public:
	IMPL_AUDIOPROXY_CLASS(FSoundModulatorAssetProxy);

	virtual Audio::IProxyDataPtr Clone() const override
	{
		return Audio::IProxyDataPtr();
	}

	virtual float GetValue() const
	{
		return 1.0f;
	}

	virtual const Audio::FModulationParameter& GetParameter() const
	{
		static const Audio::FModulationParameter DefaultParam;
		return DefaultParam;
	}
};
using FSoundModulatorAssetProxyPtr = TSharedPtr<FSoundModulatorAssetProxy, ESPMode::ThreadSafe>;

/** Proxy to modulator, allowing for modulator to be referenced by the Audio Render Thread independently
  * from the implementing modulation plugin (ex. for MetaSound implementation).
  */
class AUDIOEXTENSIONS_API FSoundModulationParameterAssetProxy : public Audio::TProxyData<FSoundModulationParameterAssetProxy>, public TSharedFromThis<FSoundModulationParameterAssetProxy, ESPMode::ThreadSafe>
{
public:
	IMPL_AUDIOPROXY_CLASS(FSoundModulationParameterAssetProxy);

	virtual Audio::IProxyDataPtr Clone() const override
	{
		return Audio::IProxyDataPtr();
	}

	virtual const Audio::FModulationParameter& GetParameter() const
	{
		static const Audio::FModulationParameter DefaultParam;
		return DefaultParam;
	}
};
using FSoundModulationParameterAssetProxyPtr = TSharedPtr<FSoundModulationParameterAssetProxy, ESPMode::ThreadSafe>;

/** Interface to sound that is modulateable, allowing for certain specific
  * behaviors to be controlled on the sound level by the modulation system.
  */
class AUDIOEXTENSIONS_API ISoundModulatable
{
public:
	virtual ~ISoundModulatable() = default;

	/**
	 * Gets the object definition id of the given playing sound's instance
	 */
	virtual uint32 GetObjectId() const = 0;

	/**
	 * Returns number of actively instances of sound playing (including virtualized instances)
	 */
	virtual int32 GetPlayCount() const = 0;

	/**
	 * Returns whether or not sound is an editor preview sound
	 */
	virtual bool IsPreviewSound() const = 0;

	/**
	 * Stops sound.
	 */
	virtual void Stop() = 0;
};
