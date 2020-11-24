// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "ComponentSourceInterfaces.h"
#include "MultiSelectionTool.generated.h"

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UMultiSelectionTool : public UInteractiveTool
{
GENERATED_BODY()
public:
	void SetSelection(TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargetsIn)
    {
		ComponentTargets = MoveTemp(ComponentTargetsIn);
	}

	/**
	 * @return true if all ComponentTargets of this tool are still valid
	 */
	virtual bool AreAllTargetsValid() const
	{
		for (const TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
		{
			if (Target->IsValid() == false)
			{
				return false;
			}
		}
		return true;
	}


public:
	virtual bool CanAccept() const override
	{
		return AreAllTargetsValid();
	}

protected:
	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets{};

	/**
	 * Helper to find which component targets share source data
	 *
	 * @return Array of indices, 1:1 with ComponentTargets, indicating the first index where a component target sharing the same source data appeared.
	 */
	bool GetMapToFirstComponentsSharingSourceData(TArray<int32>& MapToFirstOccurrences)
	{
		bool bSharesSources = false;
		MapToFirstOccurrences.SetNumUninitialized(ComponentTargets.Num());
		for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
		{
			MapToFirstOccurrences[ComponentIdx] = -1;
		}
		for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
		{
			if (MapToFirstOccurrences[ComponentIdx] >= 0) // already mapped
			{
				continue;
			}

			MapToFirstOccurrences[ComponentIdx] = ComponentIdx;

			FPrimitiveComponentTarget* ComponentTarget = ComponentTargets[ComponentIdx].Get();
			for (int32 VsIdx = ComponentIdx + 1; VsIdx < ComponentTargets.Num(); VsIdx++)
			{
				if (ComponentTarget->HasSameSourceData(*ComponentTargets[VsIdx]))
				{
					bSharesSources = true;
					MapToFirstOccurrences[VsIdx] = ComponentIdx;
				}
			}
		}
		return bSharesSources;
	}
};
