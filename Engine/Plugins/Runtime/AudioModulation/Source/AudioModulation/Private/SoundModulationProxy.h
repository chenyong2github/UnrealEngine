// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationPatch.h"
#include "SoundModulationTransform.h"


namespace AudioModulation
{
	// Modulator Ids
	using FBusMixId = uint32;
	extern const FBusMixId InvalidBusMixId;

	using FBusId = uint32;
	extern const FBusId InvalidBusId;

	using FLFOId = uint32;
	extern const FLFOId InvalidLFOId;


	// Modulator Proxy Templates
	template <typename IdType>
	class TModulatorProxyBase
	{
	private:
		IdType Id;

#if !UE_BUILD_SHIPPING
		FString Name;
#endif // !UE_BUILD_SHIPPING

	public:
		TModulatorProxyBase()
			: Id(static_cast<IdType>(0))
		{
		}

		TModulatorProxyBase(const FString& InName, const uint32 InId)
			: Id(static_cast<IdType>(InId))
#if !UE_BUILD_SHIPPING
			, Name(InName)
#endif // !UE_BUILD_SHIPPING
		{
		}

		IdType GetId() const
		{
			return Id;
		}

		// FOR DEBUG USE ONLY (Not available in shipped builds):
		// Provides name of object that generated proxy.
		const FString& GetName() const
		{
#if UE_BUILD_SHIPPING
			static FString Name;
#endif // UE_BUILD_SHIPPING

			return Name;
		}
	};

	template <typename IdType>
	class TModulatorProxyRefBase : public TModulatorProxyBase<IdType>
	{
	public:
		TModulatorProxyRefBase()
			: bAutoActivate(false)
		{
		}

		TModulatorProxyRefBase(const FString& Name, const IdType InId, const bool bInAutoActivate)
			: TModulatorProxyBase<IdType>(Name, InId)
			, bAutoActivate(bInAutoActivate)
		{
		}

		virtual ~TModulatorProxyRefBase() = default;

		virtual bool CanDestroy() const
		{
			return !bAutoActivate || (bAutoActivate && RefSounds.Num() == 0);
		}

		int32 OnReleaseSound(const ISoundModulatable& Sound)
		{
			if (bAutoActivate)
			{
				const int32 NumRemoved = RefSounds.Remove(&Sound);
				check(NumRemoved == 1);
			}

			return RefSounds.Num();
		}

		bool GetAutoActivate() const
		{
			return static_cast<bool>(bAutoActivate);
		}

		const TArray<const ISoundModulatable*>& GetRefSounds() const
		{
			return RefSounds;
		}

		int32 OnInitSound(const ISoundModulatable& Sound)
		{
			// Preview sounds force proxies into being auto-activated
			// to allow for auditioning with the provided modulation settings.
			if (Sound.IsPreviewSound())
			{
				bAutoActivate = 1;
			}

			if (bAutoActivate)
			{
				RefSounds.AddUnique(&Sound);
			}

			return RefSounds.Num();
		}

	private:
		uint8 bAutoActivate : 1;
		TArray<const ISoundModulatable*> RefSounds;
	};

	struct FModulatorBusMixChannelProxy : public TModulatorProxyBase<FBusId>
	{
		FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel);
		FString Address;
		uint32 ClassId;
		FSoundModulationValue Value;
	};

	class FModulatorLFOProxy : public TModulatorProxyRefBase<FLFOId>
	{
	public:
		FModulatorLFOProxy();
		FModulatorLFOProxy(const USoundBusModulatorLFO& InLFO);

		void OnUpdateProxy(const FModulatorLFOProxy& InLFOProxy);

		float GetValue() const;
		void Update(float InElapsed);

	private:
		Audio::FLFO LFO;
		float Offset;
		float Value;
	};
	using LFOProxyMap = TMap<FBusId, FModulatorLFOProxy>;

	class FControlBusProxy : public TModulatorProxyRefBase<FBusId>
	{
	public:
		FControlBusProxy();
		FControlBusProxy(const USoundControlBusBase& Bus);

		void OnUpdateProxy(const FControlBusProxy& InBusProxy);

		float GetDefaultValue() const;
		const TArray<FLFOId>& GetLFOIds() const;
		float GetLFOValue() const;
		float GetMixValue() const;
		FVector2D GetRange() const;
		float GetValue() const;
		void MixIn(const float InValue);
		void MixLFO(LFOProxyMap& LFOMap);
		void Reset();

	private:
		float Mix(float ValueA) const;
		float Mix(float ValueA, float ValueB) const;

		float DefaultValue;

		// Cached values
		float LFOValue;
		float MixValue;

		TArray<FLFOId> LFOIds;
		ESoundModulatorOperator Operator;
		FVector2D Range;
	};

	using BusProxyMap = TMap<FBusId, FControlBusProxy>;


	class FModulatorBusMixProxy : public TModulatorProxyRefBase<FBusMixId>
	{
	public:
		enum class BusMixStatus : uint8
		{
			Enabled,
			Stopping,
			Stopped
		};

		FModulatorBusMixProxy(const USoundControlBusMix& Mix);
		virtual ~FModulatorBusMixProxy() = default;

		virtual bool CanDestroy() const override;

		virtual void OnUpdateProxy(const FModulatorBusMixProxy& InBusMixProxy);

		void SetEnabled();
		void SetMix(const TArray<FSoundControlBusMixChannel>& InChannels);
		void SetMixByFilter(const FString& InAddressFilter, uint32 InFilterClassId, const FSoundModulationValue& InValue);
		void SetStopping();

		void Update(const float Elapsed, BusProxyMap& ProxyMap);

		TMap<FBusId, FModulatorBusMixChannelProxy> Channels;

	private:
		BusMixStatus Status;
	};

	using BusMixProxyMap = TMap<FBusMixId, FModulatorBusMixProxy>;

	/** Modulation input instance */
	struct FModulationInputProxy
	{
		FModulationInputProxy();
		FModulationInputProxy(const FSoundModulationInputBase& Patch);

		FBusId BusId;
		FSoundModulationInputTransform Transform;
		uint8 bSampleAndHold : 1;
	};

	/** Patch applied as the final stage of a modulation chain prior to output on the sound level (Always active, never removed) */
	struct FModulationOutputProxy
	{
		FModulationOutputProxy();
		FModulationOutputProxy(const FSoundModulationOutputBase& Patch);

		/** Whether patch has been initialized or not */
		uint8 bInitialized : 1;

		/** Operator used to calculate the output proxy value */
		ESoundModulatorOperator Operator;

		/** Cached value of sample-and-hold input values */
		float SampleAndHoldValue;

		/** Final transform before passing to output */
		FSoundModulationOutputTransform Transform;
	};

	struct FModulationPatchProxy
	{
		FModulationPatchProxy();
		FModulationPatchProxy(const FSoundModulationPatchBase& Patch);

		/** Default value of patch (Value mixed when inputs are provided or not, regardless of active state)*/
		float DefaultInputValue;

		/** Optional modulation inputs */
		TArray<FModulationInputProxy> InputProxies;

		/** Final output modulation post input combination */
		FModulationOutputProxy OutputProxy;
	};

	struct FModulationSettingsProxy : public TModulatorProxyBase<uint32>
	{
		FModulationSettingsProxy();
		FModulationSettingsProxy(const USoundModulationSettings& Settings);

		FModulationPatchProxy Volume;
		FModulationPatchProxy Pitch;
		FModulationPatchProxy Lowpass;
		FModulationPatchProxy Highpass;

		TMap<FName, FModulationPatchProxy> Controls;

		TArray<FBusMixId> Mixes;
	};
} // namespace AudioModulation