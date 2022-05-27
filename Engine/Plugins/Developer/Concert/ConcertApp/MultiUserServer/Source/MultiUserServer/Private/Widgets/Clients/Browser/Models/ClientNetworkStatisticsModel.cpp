// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClientNetworkStatisticsModel.h"

#include "Containers/Ticker.h"
#include "Features/IModularFeatures.h"
#include "INetworkMessagingExtension.h"
#include "Misc/ScopeExit.h"

namespace UE::MultiUserServer::Private
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

UE::MultiUserServer::FClientNetworkStatisticsModel::FClientNetworkStatisticsModel()
{
	if (INetworkMessagingExtension* Statistics = Private::GetMessagingStatistics())
	{
		Statistics->OnTransferUpdatedFromThread().AddRaw(this, &FClientNetworkStatisticsModel::OnTransferUpdatedFromThread);
	}
}

UE::MultiUserServer::FClientNetworkStatisticsModel::~FClientNetworkStatisticsModel()
{
	if (INetworkMessagingExtension* Statistics = Private::GetMessagingStatistics())
	{
		Statistics->OnTransferUpdatedFromThread().RemoveAll(this);
	}
}

TOptional<FMessageTransportStatistics> UE::MultiUserServer::FClientNetworkStatisticsModel::GetLatestNetworkStatistics(const FMessageAddress& ClientAddress) const
{
	if (INetworkMessagingExtension* Statistics = Private::GetMessagingStatistics())
	{
		const FGuid NodeId = Statistics->GetNodeIdFromAddress(ClientAddress);
		return NodeId.IsValid() ? Statistics->GetLatestNetworkStatistics(NodeId) : TOptional<FMessageTransportStatistics>();
	}
	return {};
}

void UE::MultiUserServer::FClientNetworkStatisticsModel::RegisterOnTransferUpdatedFromThread(const FMessageAddress& ClientAddress, FOnMessageTransportStatisticsUpdated StatisticUpdateCallback)
{
	INetworkMessagingExtension* Statistics = Private::GetMessagingStatistics();
	if (!Statistics || !ensure(IsInGameThread()))
	{
		return;
	}
	if (const FGuid NodeId = Statistics->GetNodeIdFromAddress(ClientAddress); NodeId.IsValid())
	{
		StatisticUpdateCallbacks.Add(NodeId, StatisticUpdateCallback);
	}
}

void UE::MultiUserServer::FClientNetworkStatisticsModel::UnregisterOnTransferUpdatedFromThread(const FMessageAddress& ClientAddress)
{
	INetworkMessagingExtension* Statistics = Private::GetMessagingStatistics();
	if (!Statistics || !ensure(IsInGameThread()))
	{
		return;
	}
	if (const FGuid NodeId = Statistics->GetNodeIdFromAddress(ClientAddress); NodeId.IsValid())
	{
		StatisticUpdateCallbacks.Remove(NodeId);
	}
}

void UE::MultiUserServer::FClientNetworkStatisticsModel::OnTransferUpdatedFromThread(FTransferStatistics Stats) const
{
	if (const FOnMessageTransportStatisticsUpdated* Callback = StatisticUpdateCallbacks.Find(Stats.DestinationId))
	{
		Callback->Execute(Stats);
	}
}