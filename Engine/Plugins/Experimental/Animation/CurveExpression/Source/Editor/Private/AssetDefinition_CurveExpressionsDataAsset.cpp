// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_CurveExpressionsDataAsset.h"

#include "SDetailsDiff.h"


EAssetCommandResult UAssetDefinition_CurveExpressionsDataAsset::PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
{
	if (DiffArgs.OldAsset == nullptr && DiffArgs.NewAsset == nullptr)
	{
		return EAssetCommandResult::Unhandled;
	}
	
	SDetailsDiff::CreateDiffWindow(DiffArgs.OldAsset, DiffArgs.NewAsset, DiffArgs.OldRevision, DiffArgs.NewRevision, UCurveExpressionsDataAsset::StaticClass());
	return EAssetCommandResult::Handled;
}
