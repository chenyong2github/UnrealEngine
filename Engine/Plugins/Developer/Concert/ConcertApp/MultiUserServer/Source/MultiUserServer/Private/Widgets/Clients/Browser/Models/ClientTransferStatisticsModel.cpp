// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClientTransferStatisticsModel.h"

#include "INetworkMessagingExtension.h"
#include "Containers/Ticker.h"
#include "Features/IModularFeatures.h"

namespace UE::MultiUserServer::Private::ClientTransferStatisticsModel
{
	static INetworkMessagingExtension* GetMessagingStatistics()
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (IsInGameThread())
		{
			if (ModularFeatures.IsModularFeatureAvailable(INetworkMessagingExtension::ModularFeatureName))
			{
				return &ModularFeatures.GetModularFeature<INetworkMessagingExtension>(INetworkMessagingExtension::ModularFeatureName);
			}
		}
		else
		{
			ModularFeatures.LockModularFeatureList();
			ON_SCOPE_EXIT
			{
				ModularFeatures.UnlockModularFeatureList();
			};
			
			if (ModularFeatures.IsModularFeatureAvailable(INetworkMessagingExtension::ModularFeatureName))
			{
				return &ModularFeatures.GetModularFeature<INetworkMessagingExtension>(INetworkMessagingExtension::ModularFeatureName);
			}
		}
		
		ensureMsgf(false, TEXT("Feature %s is unavailable"), *INetworkMessagingExtension::ModularFeatureName.ToString());
		return nullptr;
	}
}

UE::MultiUserServer::FClientTransferStatisticsModel::FClientTransferStatisticsModel(const FMessageAddress& MessageAddress)
	: MessageAddress(MessageAddress)
{
	if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
	{
		Statistics->OnTransferUpdatedFromThread().AddRaw(this, &FClientTransferStatisticsModel::OnTransferUpdatedFromThread);
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FClientTransferStatisticsModel::Tick));
	}
}

UE::MultiUserServer::FClientTransferStatisticsModel::~FClientTransferStatisticsModel()
{
	if (INetworkMessagingExtension* Statistics = Private::ClientTransferStatisticsModel::GetMessagingStatistics())
	{
		Statistics->OnTransferUpdatedFromThread().RemoveAll(this);
	}
	
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
}

void UE::MultiUserServer::FClientTransferStatisticsModel::OnTransferUpdatedFromThread(FTransferStatistics TransferStatistics)
{
	AsyncStatQueue.Enqueue(TransferStatistics);
}

bool UE::MultiUserServer::FClientTransferStatisticsModel::Tick(float DeltaTime)
{
	bool bAnyElements = false;
	while (const TOptional<FTransferStatistics> TransferStatistics = AsyncStatQueue.Dequeue())
	{
		bAnyElements = true;
		
		const TSharedPtr<FTransferStatistics> NewValue = MakeShared<FTransferStatistics>(*TransferStatistics);
		auto Pos = Algo::UpperBound(Stats, NewValue, [](const TSharedPtr<FTransferStatistics>& Value, const TSharedPtr<FTransferStatistics>& Check)
		{
			return Value->MessageId > Check->MessageId;
		});
		if (Stats.Num() > 0 && Pos < Stats.Num() && Stats[Pos] && Stats[Pos]->MessageId == NewValue->MessageId)
		{
			Stats[Pos] = NewValue;
		}
		else
		{
			Stats.Insert(NewValue, Pos);
		}
	}

	if (bAnyElements)
	{
		OnUpdatedDelegate.Broadcast();
	}

	return true;
}
