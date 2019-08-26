// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundModulatorBus.h"
#include "SoundModulationValue.h"

#include "SoundModulatorBusMix.generated.h"


USTRUCT(BlueprintType)
struct FSoundModulatorBusMixChannel
{
	GENERATED_USTRUCT_BODY()

	FSoundModulatorBusMixChannel();
	FSoundModulatorBusMixChannel(USoundModulatorBusBase* InBus, const float TargetValue);

	/* Bus controlled by channel. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadOnly)
	USoundModulatorBusBase* Bus;

	/* Value mix is set to. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationValue Value;
};

UCLASS(config = Engine, autoexpandcategories = (Channel, Mix), editinlinenew, BlueprintType, MinimalAPI)
class USoundModulatorBusMix : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	virtual void BeginDestroy() override;

	/* Array of channels controlled by mix. */
	UPROPERTY(EditAnywhere, Category = Mix, BlueprintReadOnly)
	TArray<FSoundModulatorBusMixChannel> Channels;
};

namespace AudioModulation
{
	struct FModulatorBusMixChannelProxy
	{
		FModulatorBusMixChannelProxy(const FSoundModulatorBusMixChannel& Channel);

		BusId BusId;
		FSoundModulationValue Value;
		
#if !UE_BUILD_SHIPPING
		FString Name;
#endif // !UE_BUILD_SHIPPING
	};

	struct FModulatorBusMixProxy
	{
		enum class BusMixStatus : uint8
		{
			Enabled,
			Stopping,
			Stopped
		};

		FModulatorBusMixProxy(const USoundModulatorBusMix& Mix);

		bool CanDestroy() const;
		bool GetAutoActivate() const;
		void SetEnabled();
		void SetStopping();
		void Update(const float Elapsed, BusProxyMap& ProxyMap);

#if !UE_BUILD_SHIPPING
		const FString& GetName() const;
#endif // !UE_BUILD_SHIPPING

		int32 IncRefSound();
		int32 DecRefSound();

		TMap<BusId, FModulatorBusMixChannelProxy> Channels;

	private:
		BusMixStatus Status;
		int32 SoundRefCount;
		uint8 bAutoActivate : 1;

#if !UE_BUILD_SHIPPING
		FString Name;
#endif // !UE_BUILD_SHIPPING
	};
	using BusMixProxyMap = TMap<BusMixId, FModulatorBusMixProxy>;
} // namespace AudioModulation
