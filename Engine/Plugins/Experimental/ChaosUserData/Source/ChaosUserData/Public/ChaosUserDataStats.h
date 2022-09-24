// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("ChaosUserData"), STATGROUP_ChaosUserData, STATCAT_Advanced);

namespace Chaos
{
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ChaosUserData::SetData_External"), STAT_SetData_External, STATGROUP_ChaosUserData, CHAOSUSERDATA_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ChaosUserData::RemoveData_External"), STAT_RemoveData_External, STATGROUP_ChaosUserData, CHAOSUSERDATA_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ChaosUserData::GetData_Internal"), STAT_GetData_Internal, STATGROUP_ChaosUserData, CHAOSUSERDATA_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ChaosUserData::OnPreSimulate_Internal::UpdateData"), STAT_Tick_UpdateData, STATGROUP_ChaosUserData, CHAOSUSERDATA_API);
	DECLARE_CYCLE_STAT_EXTERN(TEXT("ChaosUserData::OnPreSimulate_Internal::RemoveData"), STAT_Tick_RemoveData, STATGROUP_ChaosUserData, CHAOSUSERDATA_API);
}

