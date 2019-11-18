// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "AudioModulationLogging.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationPatch.h"
#include "SoundModulationTransform.h"
#include "SoundModulatorLFO.h"


namespace AudioModulation
{
	// Forward Declarations
	struct FReferencedProxies;

	// Modulator Ids
	using FBusMixId = uint32;
	extern const FBusMixId InvalidBusMixId;

	using FBusId = uint32;
	extern const FBusId InvalidBusId;

	using FLFOId = uint32;
	extern const FBusId InvalidLFOId;

	/*
	 * Handle for all ref-counted proxy types, to be used only on the audio thread (not thread safe). 
	 */
	template<typename IdType, typename ProxyType, typename ProxyUObjType>
	class TProxyHandle
	{
		IdType Id;
		TMap<IdType, ProxyType>* ProxyMap;

	public:
		FORCEINLINE bool IsValid() const
		{
			return static_cast<uint32>(Id) != INDEX_NONE;
		}

		FORCEINLINE IdType GetId() const
		{
			return Id;
		}

		FORCEINLINE ProxyType& FindProxy()
		{
			check(IsValid());
			check(ProxyMap);

			return ProxyMap->FindChecked(Id);
		}

		FORCEINLINE const ProxyType& FindProxy() const
		{
			check(IsValid());
			check(ProxyMap);

			return ProxyMap->FindChecked(Id);
		}

		friend FORCEINLINE uint32 GetTypeHash(const TProxyHandle<IdType, ProxyType, ProxyUObjType>& InHandle)
		{
			return static_cast<uint32>(InHandle.Id);
		}

		TProxyHandle()
			: Id(static_cast<IdType>(INDEX_NONE))
			, ProxyMap(nullptr)
		{
		}

		TProxyHandle(IdType InId, TMap<IdType, ProxyType>& InProxyMap)
			: Id(InId)
			, ProxyMap(&InProxyMap)
		{
		}

		TProxyHandle(const TProxyHandle& InHandle)
		{
			Id = InHandle.Id;
			ProxyMap = InHandle.ProxyMap;

			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.IncRef();
			}
		}

		TProxyHandle(TProxyHandle&& InHandle)
		{
			Id = InHandle.Id;
			ProxyMap = InHandle.ProxyMap;

			InHandle.Id = static_cast<IdType>(INDEX_NONE);
			InHandle.ProxyMap = nullptr;
		}

		~TProxyHandle()
		{
			if (ProxyMap)
			{
				if (ProxyType* Proxy = ProxyMap->Find(Id))
				{
					const uint32 RefCount = Proxy->DecRef();
					if (RefCount == 0)
					{
						UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy released: Id '%u'."), Id);
						ProxyMap->Remove(Id);
					}
				}
			}
		}

		/*
		 * Creates a handle to a proxy modulation object tracked in the provided InProxyMap if it exists, otherwise returns invalid handle.
		 */
		static TProxyHandle<IdType, ProxyType, ProxyUObjType> Get(const ProxyUObjType& InObject, TMap<IdType, ProxyType>& InProxyMap)
		{
			const IdType ObjectId = static_cast<IdType>(InObject.GetUniqueID());
			if (ProxyType* Proxy = InProxyMap.Find(ObjectId))
			{
				TProxyHandle<IdType, ProxyType, ProxyUObjType> NewHandle(ObjectId, InProxyMap);
				Proxy->IncRef();

				return NewHandle;
			} 

			return TProxyHandle<IdType, ProxyType, ProxyUObjType>();
		}

		/*
		 * Creates a handle to a proxy modulation object tracked in the provided InProxyMap.  Creates new proxy if it doesn't exist.
		 */
		static TProxyHandle<IdType, ProxyType, ProxyUObjType> Create(const ProxyUObjType& InObject, TMap<IdType, ProxyType>& InProxyMap)
		{
			const IdType ObjectId = static_cast<IdType>(InObject.GetUniqueID());
			TProxyHandle<IdType, ProxyType, ProxyUObjType> NewHandle(ObjectId, InProxyMap);

			ProxyType* Proxy = InProxyMap.Find(NewHandle.Id);
			if (!Proxy)
			{
				UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy created: Id '%u' for object '%s'."), NewHandle.Id, *InObject.GetName());
				Proxy = &InProxyMap.Add(NewHandle.Id, ProxyType(InObject));
			}
			Proxy->IncRef();

			return NewHandle;
		}

		static TProxyHandle<IdType, ProxyType, ProxyUObjType> Create(const ProxyUObjType& InObject, TMap<IdType, ProxyType>& InProxyMap, TFunction<void(ProxyType&)> OnCreateProxy)
		{
			const IdType ObjectId = static_cast<IdType>(InObject.GetUniqueID());
			TProxyHandle<IdType, ProxyType, ProxyUObjType> NewHandle(ObjectId, InProxyMap);

			ProxyType* Proxy = InProxyMap.Find(NewHandle.Id);
			if (!Proxy)
			{
				UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy created: Id '%u' for object '%s'."), NewHandle.Id, *InObject.GetName());
				Proxy = &InProxyMap.Add(NewHandle.Id, ProxyType(InObject));
				OnCreateProxy(*Proxy);
			}
			Proxy->IncRef();

			return NewHandle;
		}

		FORCEINLINE bool operator ==(const TProxyHandle<IdType, ProxyType, ProxyUObjType>& InHandle) const
		{
			return InHandle.Id == Id;
		}

		FORCEINLINE TProxyHandle<IdType, ProxyType, ProxyUObjType>& operator =(const TProxyHandle<IdType, ProxyType, ProxyUObjType>& InHandle)
		{
			if (InHandle.Id == Id)
			{
				check(ProxyMap == InHandle.ProxyMap);
				return *this;
			}

			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.DecRef();
			}

			Id = InHandle.Id;
			ProxyMap = InHandle.ProxyMap;

			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.IncRef();
			}

			return *this;
		}

		FORCEINLINE TProxyHandle<IdType, ProxyType, ProxyUObjType>& operator =(TProxyHandle<IdType, ProxyType, ProxyUObjType>&& InHandle)
		{
			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.DecRef();
			}

			Id = InHandle.Id;
			ProxyMap = InHandle.ProxyMap;

			InHandle.Id = static_cast<IdType>(INDEX_NONE);
			InHandle.ProxyMap = nullptr;

			return *this;
		}
	};

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
			: Id(static_cast<IdType>(INDEX_NONE))
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

	struct FModulatorBusMixChannelProxy : public TModulatorProxyBase<FBusId>
	{
		FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel);
		FString Address;
		uint32 ClassId;
		FSoundModulationValue Value;
	};

	class FModulatorLFOProxy;

	using FLFOProxyMap = TMap<FLFOId, FModulatorLFOProxy>;
	using FLFOHandle = TProxyHandle<FLFOId, FModulatorLFOProxy, USoundBusModulatorLFO>;

	template<typename IdType, typename ProxyType, typename ProxyUObjType>
	class TModulatorProxyRefType : public TModulatorProxyBase<IdType>
	{
	protected:
		uint32 RefCount;

	public:
		TModulatorProxyRefType()
			: TModulatorProxyBase<IdType>()
			, RefCount(0)
		{
		}

		TModulatorProxyRefType(const FString& InName, const uint32 InId)
			: TModulatorProxyBase<IdType>(InName, InId)
			, RefCount(0)
		{
		}

		TModulatorProxyRefType(const TModulatorProxyRefType& InProxyRef)
			: TModulatorProxyBase<IdType>(InProxyRef.GetName(), InProxyRef.GetId())
			, RefCount(InProxyRef.RefCount)
		{
		}

		TModulatorProxyRefType(TModulatorProxyRefType&& InProxyRef)
			: TModulatorProxyBase<IdType>(InProxyRef.GetName(), InProxyRef.GetId())
			, RefCount(InProxyRef.RefCount)
		{
			InProxyRef.RefCount = 0;
		}

		TModulatorProxyRefType& operator=(const TModulatorProxyRefType& InOther)
		{
			RefCount = 0;
			return *this;
		}

		TModulatorProxyRefType& operator=(const TModulatorProxyRefType&& InOther)
		{
			RefCount = InOther.RefCount;
			InOther.RefCount = 0;
			return *this;
		}

		virtual ~TModulatorProxyRefType()
		{
			check(RefCount == 0);
		}

		uint32 GetRefCount() const
		{
			return RefCount;
		}

	protected:
		FORCEINLINE void IncRef()
		{
			RefCount++;
		}

		FORCEINLINE uint32 DecRef()
		{
			check(RefCount != 0);

			RefCount--;
			return RefCount;
		}

	private:
		friend class TProxyHandle<IdType, ProxyType, ProxyUObjType>;
	};

	class FModulatorLFOProxy : public TModulatorProxyRefType<FLFOId, FModulatorLFOProxy, USoundBusModulatorLFO>
	{
	public:
		FModulatorLFOProxy();
		FModulatorLFOProxy(const USoundBusModulatorLFO& InLFO);

		FModulatorLFOProxy& operator =(const USoundBusModulatorLFO& InLFO);

		float GetValue() const;
		bool IsBypassed() const;
		void Update(float InElapsed);

	private:
		void Init(const USoundBusModulatorLFO& InLFO);

		Audio::FLFO LFO;
		float Offset;
		float Value;
		uint8 bBypass : 1;
	};

	class FControlBusProxy : public TModulatorProxyRefType<FBusId, FControlBusProxy, USoundControlBusBase>
	{
	public:
		FControlBusProxy();
		FControlBusProxy(const USoundControlBusBase& Bus);

		FControlBusProxy& operator =(const USoundControlBusBase& InLFO);

		float GetDefaultValue() const;
		const TArray<FLFOHandle>& GetLFOHandles() const;
		float GetLFOValue() const;
		float GetMixValue() const;
		FVector2D GetRange() const;
		float GetValue() const;
		void InitLFOs(const USoundControlBusBase& InBus, FLFOProxyMap& OutActiveLFOs);
		bool IsBypassed() const;
		void MixIn(const float InValue);
		void MixLFO();
		void Reset();

	private:
		void Init(const USoundControlBusBase& InBus);
		float Mix(float ValueA) const;
		float Mix(float ValueA, float ValueB) const;

		float DefaultValue;

		// Cached values
		float LFOValue;
		float MixValue;

		bool bBypass;

		TArray<FLFOHandle> LFOHandles;
		ESoundModulatorOperator Operator;
		FVector2D Range;
	};

	using FBusProxyMap = TMap<FBusId, FControlBusProxy>;
	using FBusHandle = TProxyHandle<FBusId, FControlBusProxy, USoundControlBusBase>;

	class FModulatorBusMixProxy : public TModulatorProxyRefType<FBusMixId, FModulatorBusMixProxy, USoundControlBusMix>
	{
	public:
		enum class BusMixStatus : uint8
		{
			Enabled,
			Stopping,
			Stopped
		};

		FModulatorBusMixProxy(const USoundControlBusMix& Mix);

		FModulatorBusMixProxy& operator =(const USoundControlBusMix& InBusMix);

		bool CanDestroy() const;

		void SetEnabled();
		void SetMix(const TArray<FSoundControlBusMixChannel>& InChannels);
		void SetMixByFilter(const FString& InAddressFilter, uint32 InFilterClassId, const FSoundModulationValue& InValue);
		void SetStopping();

		void Update(const float Elapsed, FBusProxyMap& ProxyMap);

		TMap<FBusId, FModulatorBusMixChannelProxy> Channels;

	private:
		void Init(const USoundControlBusMix& InBusMix);
		BusMixStatus Status;
	};

	using FBusMixProxyMap = TMap<FBusMixId, FModulatorBusMixProxy>;
	using FBusMixHandle = TProxyHandle<FBusMixId, FModulatorBusMixProxy, USoundControlBusMix>;

	/** Modulation input instance */
	struct FModulationInputProxy
	{
		FModulationInputProxy();
		FModulationInputProxy(const FSoundModulationInputBase& Patch, FReferencedProxies& OutRefProxies);

		FBusHandle BusHandle;
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
		FModulationPatchProxy(const FSoundModulationPatchBase& Patch, FReferencedProxies& OutRefProxies);

		/** Default value of patch (Value mixed when inputs are provided or not, regardless of active state)*/
		float DefaultInputValue;

		/** Bypasses the patch and doesn't update modulation value */
		uint8 bBypass : 1;

		/** Optional modulation inputs */
		TArray<FModulationInputProxy> InputProxies;

		/** Final output modulation post input combination */
		FModulationOutputProxy OutputProxy;
	};

	struct FModulationSettingsProxy : public TModulatorProxyBase<uint32>
	{
		FModulationSettingsProxy();
		FModulationSettingsProxy(const USoundModulationSettings& Settings, FReferencedProxies& OutRefProxies);

		FModulationPatchProxy Volume;
		FModulationPatchProxy Pitch;
		FModulationPatchProxy Lowpass;
		FModulationPatchProxy Highpass;

		TMap<FName, FModulationPatchProxy> Controls;

		TArray<FBusMixHandle> Mixes;
	};

	struct FReferencedProxies
	{
		FBusMixProxyMap BusMixes;
		FBusProxyMap    Buses;
		FLFOProxyMap    LFOs;
	};
} // namespace AudioModulation