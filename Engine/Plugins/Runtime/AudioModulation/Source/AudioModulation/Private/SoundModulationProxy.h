// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioModulationLogging.h"
#include "CoreMinimal.h"
#include "SoundModulationPatch.h"


namespace AudioModulation
{
	// Forward Declarations
	class FAudioModulationSystem;

	/*
	 * Handle for all ref-counted proxy types, to be used only on the audio thread (not thread safe). 
	 */
	template<typename IdType, typename ProxyType, typename ProxiedObject>
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

		friend FORCEINLINE uint32 GetTypeHash(const TProxyHandle<IdType, ProxyType, ProxiedObject>& InHandle)
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
			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.IncRef();
			}
		}

		TProxyHandle(const TProxyHandle& InHandle)
			: Id(InHandle.Id)
			, ProxyMap(InHandle.ProxyMap)
		{
			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.IncRef();
			}
		}

		TProxyHandle(TProxyHandle&& InHandle)
			: Id(InHandle.Id)
			, ProxyMap(InHandle.ProxyMap)
		{
			// No Inc/DecRef required due to transfer of ownership
			InHandle.Id = static_cast<IdType>(INDEX_NONE);
			InHandle.ProxyMap = nullptr;
		}

		~TProxyHandle()
		{
			if (!ProxyMap || !IsValid())
			{
				return;
			}

			ProxyType& Proxy = ProxyMap->FindChecked(Id);
			const uint32 RefCount = Proxy.DecRef();
			if (RefCount == 0)
			{
#if UE_BUILD_SHIPPING
				UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy released: Id '%u'."), Id);
#else // UE_BUILD_SHIPPING
				UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy '%s' released: Id '%u'."), *Proxy.GetName(), Id);
#endif // !UE_BUILD_SHIPPING
				ProxyMap->Remove(Id);
			}
		}

		/*
		 * Creates a handle to a proxy modulation object tracked in the provided InProxyMap if it exists, otherwise returns invalid handle.
		 */
		static TProxyHandle<IdType, ProxyType, ProxiedObject> Get(const IdType ObjectId, TMap<IdType, ProxyType>& InProxyMap)
		{
			if (ProxyType* Proxy = InProxyMap.Find(ObjectId))
			{
				check(Proxy->ModSystem);
				return TProxyHandle<IdType, ProxyType, ProxiedObject>(ObjectId, InProxyMap);
			} 

			return TProxyHandle<IdType, ProxyType, ProxiedObject>();
		}

		/*
		 * Creates a handle to a proxy modulation object tracked in the provided InProxyMap if it exists, otherwise returns invalid handle.
		 */
		static TProxyHandle<IdType, ProxyType, ProxiedObject> Get(const ProxiedObject& InObject, TMap<IdType, ProxyType>& InProxyMap)
		{
			const IdType ObjectId = static_cast<IdType>(InObject.GetUniqueID());
			return Get(ObjectId, InProxyMap);
		}

		/*
		 * Creates a handle to a proxy modulation object tracked in the provided InProxyMap.  Creates new proxy if it doesn't exist.
		 */
		static TProxyHandle<IdType, ProxyType, ProxiedObject> Create(const ProxiedObject& InObject, TMap<IdType, ProxyType>& InProxyMap, FAudioModulationSystem& InModSystem)
		{
			const IdType ObjectId = static_cast<IdType>(InObject.GetUniqueID());

			TProxyHandle<IdType, ProxyType, ProxiedObject> NewHandle = Get(InObject, InProxyMap);
			if (!NewHandle.IsValid())
			{
				UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy created: Id '%u' for object '%s'."), NewHandle.Id, *InObject.GetName());
				InProxyMap.Add(ObjectId, ProxyType(InObject, InModSystem));
				NewHandle = TProxyHandle<IdType, ProxyType, ProxiedObject>(ObjectId, InProxyMap);
			}

			return NewHandle;
		}

		static TProxyHandle<IdType, ProxyType, ProxiedObject> Create(const ProxiedObject& InObject, TMap<IdType, ProxyType>& InProxyMap, FAudioModulationSystem& InModSystem, TFunction<void(ProxyType&)> OnCreateProxy)
		{
			const IdType ObjectId = static_cast<IdType>(InObject.GetUniqueID());
			TProxyHandle<IdType, ProxyType, ProxiedObject> NewHandle = Get(InObject, InProxyMap);
			if (!NewHandle.IsValid())
			{
				UE_LOG(LogAudioModulation, Verbose, TEXT("Proxy created: Id '%u' for object '%s'."), NewHandle.Id, *InObject.GetName());
				InProxyMap.Add(ObjectId, ProxyType(InObject, InModSystem));
				NewHandle = TProxyHandle<IdType, ProxyType, ProxiedObject>(ObjectId, InProxyMap);
				OnCreateProxy(NewHandle.FindProxy());
			}

			return NewHandle;
		}

		FORCEINLINE bool operator ==(const TProxyHandle<IdType, ProxyType, ProxiedObject>& InHandle) const
		{
			return InHandle.Id == Id;
		}

		FORCEINLINE TProxyHandle<IdType, ProxyType, ProxiedObject>& operator =(const TProxyHandle<IdType, ProxyType, ProxiedObject>& InHandle)
		{
			// 1. If local proxy valid prior to move, cache to DecRef
			ProxyType* ProxyToDecRef = nullptr;
			if (IsValid())
			{
				ProxyToDecRef = &ProxyMap->FindChecked(Id);
			}

			// 2. Move internal state
			Id = InHandle.Id;
			ProxyMap = InHandle.ProxyMap;

			// 3. IncRef new data
			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.IncRef();
			}

			// 4. DecRef post IncRef to avoid premature destruction if handling same proxy
			if (ProxyToDecRef)
			{
				ProxyToDecRef->DecRef();
			}

			return *this;
		}

		FORCEINLINE TProxyHandle<IdType, ProxyType, ProxiedObject>& operator =(TProxyHandle<IdType, ProxyType, ProxiedObject>&& InHandle)
		{
			// 1. If local proxy valid prior to move, cache to DecRef
			ProxyType* ProxyToDecRef = nullptr;
			if (IsValid())
			{
				ProxyToDecRef = &ProxyMap->FindChecked(Id);
			}

			// 2. Move internal state
			Id = InHandle.Id;
			ProxyMap = InHandle.ProxyMap;

			// 3. IncRef new data
			if (IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(Id);
				Proxy.IncRef();
			}

			// 4. DecRef post IncRef to avoid premature destruction if handling same proxy
			if (ProxyToDecRef)
			{
				ProxyToDecRef->DecRef();
			}

			// 5. DecRef handle to move and clear state
			if (InHandle.IsValid())
			{
				ProxyType& Proxy = ProxyMap->FindChecked(InHandle.Id);
				Proxy.DecRef();
			}
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

		virtual ~TModulatorProxyBase() = default;

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

	template<typename IdType, typename ProxyType, typename ProxiedObject>
	class TModulatorProxyRefType : public TModulatorProxyBase<IdType>
	{
	protected:
		uint32 RefCount;
		FAudioModulationSystem* ModSystem;

	public:
		TModulatorProxyRefType()
			: TModulatorProxyBase<IdType>()
			, RefCount(0)
			, ModSystem(nullptr)
		{
		}

		TModulatorProxyRefType(const FString& InName, const uint32 InId, FAudioModulationSystem& InModSystem)
			: TModulatorProxyBase<IdType>(InName, InId)
			, RefCount(0)
			, ModSystem(&InModSystem)
		{
		}

		TModulatorProxyRefType(const TModulatorProxyRefType& InProxyRef)
			: TModulatorProxyBase<IdType>(InProxyRef.GetName(), InProxyRef.GetId())
			, RefCount(InProxyRef.RefCount)
			, ModSystem(InProxyRef.ModSystem)
		{
		}

		TModulatorProxyRefType(TModulatorProxyRefType&& InProxyRef)
			: TModulatorProxyBase<IdType>(InProxyRef.GetName(), InProxyRef.GetId())
			, RefCount(InProxyRef.RefCount)
			, ModSystem(InProxyRef.ModSystem)
		{
			InProxyRef.RefCount = 0;
			InProxyRef.ModSystem = nullptr;
		}

		TModulatorProxyRefType& operator=(const TModulatorProxyRefType& InOther)
		{
			RefCount = InOther.RefCount;
			ModSystem = InOther.ModSystem;
			return *this;
		}

		TModulatorProxyRefType& operator=(TModulatorProxyRefType&& InOther)
		{
			RefCount = InOther.RefCount;
			ModSystem = InOther.ModSystem;

			InOther.RefCount = 0;
			InOther.ModSystem = nullptr;

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
		friend class TProxyHandle<IdType, ProxyType, ProxiedObject>;
	};
} // namespace AudioModulation