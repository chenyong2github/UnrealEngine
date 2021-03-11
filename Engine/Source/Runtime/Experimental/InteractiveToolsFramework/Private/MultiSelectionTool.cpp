// Copyright Epic Games, Inc. All Rights Reserved.
#include "MultiSelectionTool.h"
#include "TargetInterfaces/AssetBackedTarget.h"

bool UMultiSelectionTool::GetMapToSharedSourceData(TArray<int32>& MapToFirstOccurrences)
{
	bool bSharesSources = false;
	MapToFirstOccurrences.SetNumUninitialized(Targets.Num());
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		MapToFirstOccurrences[ComponentIdx] = -1;
	}
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		if (MapToFirstOccurrences[ComponentIdx] >= 0) // already mapped
		{
			continue;
		}

		MapToFirstOccurrences[ComponentIdx] = ComponentIdx;

		IAssetBackedTarget* Target = Cast<IAssetBackedTarget>(Targets[ComponentIdx]);
		if (!Target)
		{
			continue;
		}
		for (int32 VsIdx = ComponentIdx + 1; VsIdx < Targets.Num(); VsIdx++)
		{
			IAssetBackedTarget* OtherTarget = Cast<IAssetBackedTarget>(Targets[VsIdx]);
			if (OtherTarget && OtherTarget->GetSourceData() == Target->GetSourceData())
			{
				bSharesSources = true;
				MapToFirstOccurrences[VsIdx] = ComponentIdx;
			}
		}
	}
	return bSharesSources;
}

