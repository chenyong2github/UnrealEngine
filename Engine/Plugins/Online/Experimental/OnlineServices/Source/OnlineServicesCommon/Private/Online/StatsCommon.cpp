// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/StatsCommon.h"

#include "Online/Auth.h"
#include "Online/OnlineServicesCommon.h"

namespace UE::Online {

FStatsCommon::FStatsCommon(FOnlineServicesCommon& InServices)
	: TOnlineComponent(TEXT("Stats"), InServices)
{
}

void FStatsCommon::Initialize()
{
	TOnlineComponent<IStats>::Initialize();
}

void FStatsCommon::RegisterCommands()
{
	TOnlineComponent<IStats>::RegisterCommands();

	RegisterCommand(&FStatsCommon::UpdateStats);
	RegisterCommand(&FStatsCommon::QueryStats);
	RegisterCommand(&FStatsCommon::BatchQueryStats);
#if !UE_BUILD_SHIPPING
	RegisterCommand(&FStatsCommon::ResetStats);
#endif // !UE_BUILD_SHIPPING
}

TOnlineAsyncOpHandle<FUpdateStats> FStatsCommon::UpdateStats(FUpdateStats::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdateStats> Operation = GetOp<FUpdateStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FQueryStats> FStatsCommon::QueryStats(FQueryStats::Params&& Params)
{
	TOnlineAsyncOpRef<FQueryStats> Operation = GetOp<FQueryStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

TOnlineAsyncOpHandle<FBatchQueryStats> FStatsCommon::BatchQueryStats(FBatchQueryStats::Params&& Params)
{
	TOnlineAsyncOpRef<FBatchQueryStats> Operation = GetOp<FBatchQueryStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}

#if !UE_BUILD_SHIPPING
TOnlineAsyncOpHandle<FResetStats> FStatsCommon::ResetStats(FResetStats::Params&& Params)
{
	TOnlineAsyncOpRef<FResetStats> Operation = GetOp<FResetStats>(MoveTemp(Params));
	Operation->SetError(Errors::NotImplemented());
	return Operation->GetHandle();
}
#endif // !UE_BUILD_SHIPPING

/* UE::Online */ }
