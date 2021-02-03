// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UnrealTypeTraits.h"

/**
 * Interfaces for Audio Proxy Objects 
 * These are used to spawn thread safe instances of UObjects that may be garbage collected on the game thread.
 * In shipping builds, these are effectively abstract pointers, but CHECK_AUDIOPROXY_TYPES can optionally be used
 * to check downcasts.
 */

// This can be set to 1 to double check proxies.
#define CHECK_AUDIOPROXY_TYPES !(UE_BUILD_SHIPPING || UE_BUILD_TEST || UE_BUILD_DEVELOPMENT)

#define  IMPL_AUDIOPROXY_CLASS(FClassName) \
	static FName GetAudioProxyTypeName() \
	{ \
		static FName MyClassName = #FClassName; \
		return MyClassName; \
	} \
	static constexpr bool bWasAudioProxyClassImplemented = true; \
	friend class ::Audio::IProxyData; \
	friend class ::Audio::TProxyData<FClassName>;


namespace Audio
{
	// Forward Declarations
	class IProxyData;

	constexpr bool bShouldCheckAudioProxyTypes = CHECK_AUDIOPROXY_TYPES;

	using IProxyDataPtr = TUniquePtr<IProxyData>;

	/*
	 * Base class that allows us to typecheck proxy data before downcasting it in debug builds.
	*/
	class IProxyData
	{
	private:
		FName ProxyTypeName;
	public:
		virtual ~IProxyData() = default;

		template<typename ProxyType>
		bool CheckTypeCast() const
		{
			if (bShouldCheckAudioProxyTypes)
			{
				FName DestinationTypeName = ProxyType::GetAudioProxyTypeName();
				return ensureAlwaysMsgf(ProxyTypeName == DestinationTypeName, TEXT("Tried to downcast type %s to %s!"), *ProxyTypeName.ToString(), *DestinationTypeName.ToString());
			}
			else
			{
				return true;
			}
		}

		FName GetProxyTypeName() const
		{
			return ProxyTypeName;
		}

		template<typename ProxyType>
		ProxyType& GetAs()
		{
			static_assert(TIsDerivedFrom<ProxyType, IProxyData>::Value, "Tried to downcast IProxyInitData to an unrelated type!");
			if (CheckTypeCast<ProxyType>())
			{
				return static_cast<ProxyType&>(*this);
			}
			else
			{
				// This is an illegal cast, and is considered a fatal error.
				checkNoEntry();
				return *((ProxyType*)0x1);
			}
		}

		template<typename ProxyType>
		const ProxyType& GetAs() const
		{
			static_assert(TIsDerivedFrom<ProxyType, IProxyData>::Value, "Tried to downcast IProxyInitData to an unrelated type!");
			if (CheckTypeCast<ProxyType>())
			{
				return static_cast<const ProxyType&>(*this);
			}
			else
			{
				// This is an illegal cast, and is considered a fatal error.
				checkNoEntry();
				return *((ProxyType*)0x1);
			}
		}

		IProxyData(FName InProxyTypeName)
			: ProxyTypeName(InProxyTypeName)
		{}

		virtual IProxyDataPtr Clone() const = 0;
	};

	/**
	 * This class can be implemented to create a custom, threadsafe instance of a given UObject.
	 * This is a CRTP class, and should always be subclassed with the name of the subclass.
	 */
	template <typename Type>
	class TProxyData : public IProxyData
	{
	protected:
		static constexpr bool bWasAudioProxyClassImplemented = false;

	public:
		TProxyData()
			: IProxyData(Type::GetAudioProxyTypeName())
		{
			static_assert(Type::bWasAudioProxyClassImplemented, "Make sure to include IMPL_AUDIOPROXY_CLASS(ClassName) in your implementation of TProxyData.");
		}
	};


	struct FProxyDataInitParams
	{
		FName NameOfFeatureRequestingProxy;
	};
}

/*
* This can be subclassed to make a UClass an audio proxy factory.
*/
class IAudioProxyDataFactory
{
public:
	virtual TUniquePtr<Audio::IProxyData> CreateNewProxyData(const Audio::FProxyDataInitParams& InitParams) = 0;
};
