// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelFromTextAssetActions.h"

#include "ComputeFramework/ComputeKernelFromText.h"

FAssetTypeActions_ComputeKernelFromText::FAssetTypeActions_ComputeKernelFromText(EAssetTypeCategories::Type InAssetCategoryBit)
	: AssetCategoryBit(InAssetCategoryBit)
{
}

FText FAssetTypeActions_ComputeKernelFromText::GetName() const
{
	return NSLOCTEXT("ComputeFramework", "ComputeKernelFromTextName", "Compute Kernel (Text)");
}

FColor FAssetTypeActions_ComputeKernelFromText::GetTypeColor() const
{
	return FColor::Turquoise;
}

UClass* FAssetTypeActions_ComputeKernelFromText::GetSupportedClass() const
{
	return UComputeKernelFromText::StaticClass();
}

uint32 FAssetTypeActions_ComputeKernelFromText::GetCategories()
{
	return AssetCategoryBit;
}
