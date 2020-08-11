// Copyright Epic Games, Inc. All Rights Reserved.

#include "OSCModulationMixingStatics.h"

#include "OSCClient.h"
#include "OSCManager.h"
#include "OSCModulationMixing.h"
#include "OSCServer.h"

#include "HAL/IConsoleManager.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "UObject/WeakObjectPtrTemplates.h"


namespace OSCModulation
{
	namespace Addresses
	{
		static const FOSCAddress MixLoad	 = FString(TEXT("/Mix/Load"));
		static const FOSCAddress ProfileLoad = FString(TEXT("/Mix/Profile/Load"));
		static const FOSCAddress ProfileSave = FString(TEXT("/Mix/Profile/Save"));
	} // namespace Addresses
} // namespace OSCModulation

UOSCModulationMixingStatics::UOSCModulationMixingStatics(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void UOSCModulationMixingStatics::CopyChannelsToOSCBundle(UObject* WorldContextObject, const FOSCAddress& InPathAddress, const TArray<FSoundControlBusMixChannel>& InChannels, FOSCBundle& OutBundle)
{
	FOSCMessage RequestMessage;
	RequestMessage.SetAddress(OSCModulation::Addresses::MixLoad);
	UOSCManager::AddMessageToBundle(RequestMessage, OutBundle);

	FOSCMessage Message;
	Message.SetAddress(InPathAddress);
	UOSCManager::AddMessageToBundle(Message, OutBundle);

	for (const FSoundControlBusMixChannel& Channel : InChannels)
	{
		if (Channel.Bus)
		{
			FOSCMessage ChannelMessage;
			ChannelMessage.SetAddress(UOSCManager::OSCAddressFromObjectPath(Channel.Bus));

			UOSCManager::AddFloat(ChannelMessage, Channel.Value.AttackTime);
			UOSCManager::AddFloat(ChannelMessage, Channel.Value.ReleaseTime);
			UOSCManager::AddFloat(ChannelMessage, Channel.Value.TargetValue);

			UClass* BusClass = Channel.Bus->GetClass();
			const FString ClassName = BusClass ? BusClass->GetName() : FString();
			UOSCManager::AddString(ChannelMessage, ClassName);

			UOSCManager::AddMessageToBundle(ChannelMessage, OutBundle);
		}
	}
}

void UOSCModulationMixingStatics::CopyMixToOSCBundle(UObject* WorldContextObject, USoundControlBusMix* InMix, UPARAM(ref) FOSCBundle& OutBundle)
{
	if (!InMix)
	{
		return;
	}

	CopyChannelsToOSCBundle(WorldContextObject, UOSCManager::OSCAddressFromObjectPath(InMix), InMix->Channels, OutBundle);
}

FOSCAddress UOSCModulationMixingStatics::GetProfileLoadPath()
{
	return OSCModulation::Addresses::ProfileLoad;
}

FOSCAddress UOSCModulationMixingStatics::GetProfileSavePath()
{
	return OSCModulation::Addresses::ProfileSave;
}

FOSCAddress UOSCModulationMixingStatics::GetMixLoadPattern()
{
	return FOSCAddress(OSCModulation::Addresses::MixLoad.GetFullPath() / TEXT("*"));
}

EOSCModulationBundle UOSCModulationMixingStatics::GetOSCBundleType(const FOSCBundle& InBundle)
{
	TArray<FOSCMessage> Messages = UOSCManager::GetMessagesFromBundle(InBundle);
	if (Messages.Num() == 0)
	{
		return EOSCModulationBundle::Invalid;
	}

	FOSCAddress BundleAddress = UOSCManager::GetOSCMessageAddress(Messages[0]);
	if (BundleAddress == OSCModulation::Addresses::MixLoad)
	{
		return EOSCModulationBundle::LoadMix;
	}
	static_assert(static_cast<int32>(EOSCModulationBundle::Count) == 2, "Possible missing bundle case coverage");
	// Add additional bundle types here

	return EOSCModulationBundle::Invalid;
}

void UOSCModulationMixingStatics::RequestMix(UObject* WorldContextObject, UOSCClient* InClient, const FOSCAddress& InMixPath)
{
	if (InClient)
	{
		FOSCMessage Message;

		const FOSCAddress MixAddress = UOSCManager::OSCAddressFromObjectPathString(InMixPath.GetFullPath());
		Message.SetAddress(OSCModulation::Addresses::MixLoad / MixAddress);

		InClient->SendOSCMessage(Message);
	}
}

TArray<FSoundModulationValue> UOSCModulationMixingStatics::OSCBundleToChannelValues(UObject* WorldContextObject, const FOSCBundle& InBundle, FOSCAddress& OutMixPath, TArray<FOSCAddress>& OutBusPaths, TArray<FString>& OutBusClassNames)
{
	TArray<FSoundModulationValue> ChannelArray;
	OutBusPaths.Reset();

	const TArray<FOSCMessage> Messages = UOSCManager::GetMessagesFromBundle(InBundle);
	if (Messages.Num() > 1)
	{
		if (Messages[0].GetAddress() == OSCModulation::Addresses::MixLoad)
		{
			OutMixPath = Messages[1].GetAddress();

			for (int32 i = 2; i < Messages.Num(); ++i)
			{
				FSoundModulationValue Value;
				UOSCManager::GetFloat(Messages[i], 0, Value.AttackTime);
				UOSCManager::GetFloat(Messages[i], 1, Value.ReleaseTime);
				UOSCManager::GetFloat(Messages[i], 2, Value.TargetValue);

				FString BusClass;
				UOSCManager::GetString(Messages[i], 3, BusClass);
				OutBusClassNames.Add(BusClass);

				OutBusPaths.Add(UOSCManager::GetOSCMessageAddress(Messages[i]));
				ChannelArray.Add(Value);
			}
			return ChannelArray;
		}
	}

	OutMixPath = FOSCAddress();
	return ChannelArray;
}
