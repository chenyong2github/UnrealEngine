// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPlayerInput.h"
#include "Components/InputComponent.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterUtils/DisplayClusterTypesConverter.h"
#include "DisplayClusterHelpers.h"

#include "Engine/World.h"


UDisplayClusterPlayerInput::UDisplayClusterPlayerInput()
	: Super()
{
}

void UDisplayClusterPlayerInput::ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused)
{
	UE_LOG(LogDisplayClusterGame, Verbose, TEXT("Processing input stack..."));

	static IPDisplayClusterClusterManager* const ClusterMgr = GDisplayCluster->GetPrivateClusterMgr();

	if (ClusterMgr && ClusterMgr->IsCluster())
	{
		TMap<FString, FString> KeyStates;

		if (ClusterMgr->IsMaster())
		{
			// Export key states data
			SerializeKeyStateMap(KeyStates);
			ClusterMgr->ProvideNativeInputData(KeyStates);
		}
		else
		{
			// Import key states data
			ClusterMgr->SyncNativeInput(KeyStates);
			DeserializeKeyStateMap(KeyStates);
		}
	}

	Super::ProcessInputStack(InputComponentStack, DeltaTime, bGamePaused);
}

bool UDisplayClusterPlayerInput::SerializeKeyStateMap(TMap<FString, FString>& OutKeyStateMap)
{
	TMap<FKey, FKeyState>& StateMap = GetKeyStateMap();
	for (auto it = StateMap.CreateConstIterator(); it; ++it)
	{
		const FString KeyName = it->Key.ToString();

		FString StrKeyState;
		StrKeyState.Reserve(2048);

		float SendValue = GetWorld()->GetRealTimeSeconds() - it->Value.LastUpDownTransitionTime;
		check(SendValue > 0.f);

		StrKeyState = FString::Printf(TEXT("%s;%s;%s;%s;%s;%s;%s;%s;"),
			*FDisplayClusterTypesConverter::template ToHexString(it->Value.RawValue),
			*FDisplayClusterTypesConverter::template ToHexString(it->Value.Value),
			*FDisplayClusterTypesConverter::template ToHexString(it->Value.LastUpDownTransitionTime),
			*DisplayClusterHelpers::str::BoolToStr(it->Value.bDown ? true : false, false),
			*DisplayClusterHelpers::str::BoolToStr(it->Value.bDownPrevious ? true : false, false),
			*DisplayClusterHelpers::str::BoolToStr(it->Value.bConsumed ? true : false, false),
			*FDisplayClusterTypesConverter::template ToString(it->Value.SampleCountAccumulator),
			*FDisplayClusterTypesConverter::template ToHexString(it->Value.RawValueAccumulator));

		for (int i = 0; i < IE_MAX; ++i)
		{
			StrKeyState += FString::Printf(TEXT("%s;"), *DisplayClusterHelpers::str::ArrayToStr(it->Value.EventCounts[i], FString(","), false));
		}

		for (int i = 0; i < IE_MAX; ++i)
		{
			StrKeyState += FString::Printf(TEXT("%s;"), *DisplayClusterHelpers::str::ArrayToStr(it->Value.EventAccumulator[i], FString(","), false));
		}

		OutKeyStateMap.Emplace(KeyName, StrKeyState);
	}

	return true;
}

bool UDisplayClusterPlayerInput::DeserializeKeyStateMap(const TMap<FString, FString>& InKeyStateMap)
{
	TMap<FKey, FKeyState>& StateMap = GetKeyStateMap();
	
	// Reset local key state map
	StateMap.Reset();

	int idx = 0;
	for (auto it = InKeyStateMap.CreateConstIterator(); it; ++it)
	{
		UE_LOG(LogDisplayClusterGame, VeryVerbose, TEXT("Input data [%d]: %s = %s"), idx, *it->Key, *it->Value);
		++idx;

		FKey Key(*it->Key);
		FKeyState KeyState;

		TArray<FString> Fields = DisplayClusterHelpers::str::StrToArray<FString>(it->Value, FString(";"), false);

		KeyState.RawValue                 = FDisplayClusterTypesConverter::template FromHexString<FVector>(Fields[0]);
		KeyState.Value                    = FDisplayClusterTypesConverter::template FromHexString<FVector>(Fields[1]);
		KeyState.LastUpDownTransitionTime = FDisplayClusterTypesConverter::template FromHexString<float>(Fields[2]);
		KeyState.bDown                    = FDisplayClusterTypesConverter::template FromString<bool>(Fields[3]) ? 1 : 0;
		KeyState.bDownPrevious            = FDisplayClusterTypesConverter::template FromString<bool>(Fields[4]) ? 1 : 0;
		KeyState.bConsumed                = FDisplayClusterTypesConverter::template FromString<bool>(Fields[5]) ? 1 : 0;
		KeyState.SampleCountAccumulator   = FDisplayClusterTypesConverter::template FromString<uint8>(Fields[6]);
		KeyState.RawValueAccumulator      = FDisplayClusterTypesConverter::template FromHexString<FVector>(Fields[7]);

		for (int i = 0; i < IE_MAX; ++i)
		{
			const FString& EventCountsStr = Fields[8 + i];
			if (!EventCountsStr.IsEmpty())
			{
				KeyState.EventCounts[i] = DisplayClusterHelpers::str::StrToArray<uint32>(EventCountsStr, FString(","));
			}

			const FString& EventAccumulatorStr = Fields[8 + IE_MAX + i];
			if (!EventAccumulatorStr.IsEmpty())
			{
				KeyState.EventAccumulator[i] = DisplayClusterHelpers::str::StrToArray<uint32>(Fields[8 + IE_MAX + i], FString(","));
			}
		}

		// Add incoming data to the local map
		StateMap.Add(Key, KeyState);
	}

	return true;
}
