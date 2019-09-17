// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"

#include "SoundModulatorBase.generated.h"

namespace AudioModulation
{
	class FAudioModulationImpl;
}

/**
 * Base class for all modulators
 */
UCLASS(hideCategories = Object, abstract, MinimalAPI)
class USoundModulatorBase : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * When set, automatically activates/deactivates when sounds referencing asset begin/end playing respectively.
	 */
	UPROPERTY(EditAnywhere, Category = General, BlueprintReadWrite)
	uint8 bAutoActivate : 1;

	bool CanAutoActivate(const ISoundModulatable* InSound) const
	{
		if (bAutoActivate)
		{
			if (InSound)
			{
				return true;
			}
		}
		else
		{
			if (!InSound || InSound->IsPreviewSound())
			{
				return true;
			}
		}
		
		return false;
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};

/**
 * Base class for modulators that manipulate control bus values
 */
UCLASS(hideCategories = Object, abstract, MinimalAPI)
class USoundBusModulatorBase : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()
};

namespace AudioModulation
{
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

		TModulatorProxyBase(const FString& InName, const uint32 InObjectId)
			: Id(static_cast<IdType>(InObjectId))
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

		virtual void OnUpdateProxy(const USoundModulatorBase& InModulatorArchetype) { }

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
			bAutoActivate |= Sound.IsPreviewSound();

			if (bAutoActivate)
			{
				RefSounds.AddUnique(&Sound);
			}

			return RefSounds.Num();
		}

	private:
		int8 bAutoActivate : 1;
		TArray<const ISoundModulatable*> RefSounds;
	};
} // AudioModulation