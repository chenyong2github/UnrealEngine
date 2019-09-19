// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "SoundControlBus.h"
#include "SoundModulationValue.h"

#include "SoundControlBusMix.generated.h"


USTRUCT(BlueprintType)
struct FSoundControlBusMixChannel
{
	GENERATED_USTRUCT_BODY()

	FSoundControlBusMixChannel();
	FSoundControlBusMixChannel(USoundControlBusBase* InBus, const float TargetValue);

	/* Bus controlled by channel. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadOnly)
	USoundControlBusBase* Bus;

	/* Value mix is set to. */
	UPROPERTY(EditAnywhere, Category = Channel, BlueprintReadWrite, meta = (ShowOnlyInnerProperties))
	FSoundModulationValue Value;
};

UCLASS(config = Engine, autoexpandcategories = (Channel, Mix), editinlinenew, BlueprintType, MinimalAPI)
class USoundControlBusMix : public USoundModulatorBase
{
	GENERATED_UCLASS_BODY()

public:

	virtual void BeginDestroy() override;

	/* Array of channels controlled by mix. */
	UPROPERTY(EditAnywhere, Category = Mix, BlueprintReadOnly)
	TArray<FSoundControlBusMixChannel> Channels;
};

namespace AudioModulation
{
	struct FModulatorBusMixChannelProxy : public TModulatorProxyBase<FBusId>
	{
		FModulatorBusMixChannelProxy(const FSoundControlBusMixChannel& Channel);
		FSoundModulationValue Value;
	};

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
		virtual void OnUpdateProxy(const USoundModulatorBase& InModulatorArchetype) override;

		void SetEnabled();
		void SetStopping();
		void Update(const float Elapsed, BusProxyMap& ProxyMap);

		TMap<FBusId, FModulatorBusMixChannelProxy> Channels;

	private:
		BusMixStatus Status;
	};

	using BusMixProxyMap = TMap<FBusMixId, FModulatorBusMixProxy>;
} // namespace AudioModulation
