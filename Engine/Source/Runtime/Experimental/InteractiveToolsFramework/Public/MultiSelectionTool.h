// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "ComponentSourceInterfaces.h"
#include "ToolTargets/ToolTarget.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#include "MultiSelectionTool.generated.h"

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UMultiSelectionTool : public UInteractiveTool
{
GENERATED_BODY()
public:
	void SetTargets(TArray<TObjectPtr<UToolTarget>> TargetsIn)
	{
		Targets = MoveTemp(TargetsIn);
	}

	/**
	 * @return true if all ComponentTargets of this tool are still valid
	 */
	virtual bool AreAllTargetsValid() const
	{
		for (const TObjectPtr<UToolTarget>& Target : Targets)
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
	UPROPERTY()
	TArray<TObjectPtr<UToolTarget>> Targets{};

	/**
	 * Helper to find which targets share source data.
	 * Requires UAssetBackedTarget as a tool target requirement.
	 *
	 * @return Array of indices, 1:1 with Targets, indicating the first index where a component target sharing the same source data appeared.
	 */
	bool GetMapToSharedSourceData(TArray<int32>& MapToFirstOccurrences);

	/**
	 * Template helper to retrieve an interface from a ToolTarget.
	 */
	template <class T>
	T* TargetInterface(int32 TargetIdx) const
	{
		T* Interface = Cast<T>(Targets[TargetIdx]);
		check(Interface);
		return Interface;
	}

	/**
	 * Helper to cast a Target into the IPrimitiveComponentBackedTarget interface.
	 */
	IPrimitiveComponentBackedTarget* TargetComponentInterface(int32 ComponentIdx) const;

	/**
	 * Helper to cast a Target into the IMeshDescriptionCommitter interface.
	 */
	IMeshDescriptionCommitter* TargetMeshCommitterInterface(int32 ComponentIdx) const;

	/**
	 * Helper to cast a Target into the IMeshDescriptionProvider interface.
	 */
	IMeshDescriptionProvider* TargetMeshProviderInterface(int32 ComponentIdx) const;

	/**
	 * Helper to cast a Target into the IMaterialProvider interface.
	 */
	IMaterialProvider* TargetMaterialInterface(int32 ComponentIdx) const;
};

inline IPrimitiveComponentBackedTarget* UMultiSelectionTool::TargetComponentInterface(int32 TargetIdx) const
{
	return TargetInterface<IPrimitiveComponentBackedTarget>(TargetIdx);
}

inline IMeshDescriptionCommitter* UMultiSelectionTool::TargetMeshCommitterInterface(int32 TargetIdx) const
{
	return TargetInterface<IMeshDescriptionCommitter>(TargetIdx);
}

inline IMeshDescriptionProvider* UMultiSelectionTool::TargetMeshProviderInterface(int32 TargetIdx) const
{
	return TargetInterface<IMeshDescriptionProvider>(TargetIdx);
}

inline IMaterialProvider* UMultiSelectionTool::TargetMaterialInterface(int32 TargetIdx) const
{
	return TargetInterface<IMaterialProvider>(TargetIdx);
}

