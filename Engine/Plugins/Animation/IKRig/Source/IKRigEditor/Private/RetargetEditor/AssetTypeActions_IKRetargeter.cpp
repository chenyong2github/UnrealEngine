// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/AssetTypeActions_IKRetargeter.h"
#include "Retargeter/IKRetargeter.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_IKRetargeter::GetSupportedClass() const
{
	return UIKRetargeter::StaticClass();
}

void FAssetTypeActions_IKRetargeter::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	FAssetTypeActions_Base::GetActions(InObjects, Section);
}

#undef LOCTEXT_NAMESPACE
