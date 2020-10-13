// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIODisplayExtensionWrapper.h"

void UOpenColorIODisplayExtensionWrapper::CreateDisplayExtensionIfNotExists()
{
	if (!DisplayExtension.IsValid())
	{
		// Null viewport should ensure it doesn't run anywhere yet (unless explicitly gathered)
		DisplayExtension = FSceneViewExtensions::NewExtension<FOpenColorIODisplayExtension>(nullptr);
	}
	check(DisplayExtension.IsValid());
}

void UOpenColorIODisplayExtensionWrapper::SetOpenColorIOConfiguration(FOpenColorIODisplayConfiguration InDisplayConfiguration)
{
	if (!DisplayExtension.IsValid())
	{
		return;
	}

	DisplayExtension->SetDisplayConfiguration(InDisplayConfiguration);
}

void UOpenColorIODisplayExtensionWrapper::SetSceneExtensionIsActiveFunction(const FSceneViewExtensionIsActiveFunctor& IsActiveFunction)
{
	if (!DisplayExtension.IsValid())
	{
		return;
	}

	DisplayExtension->IsActiveThisFrameFunctions.Reset(1);
	DisplayExtension->IsActiveThisFrameFunctions.Add(IsActiveFunction);
}

void UOpenColorIODisplayExtensionWrapper::SetSceneExtensionIsActiveFunctions(const TArray<FSceneViewExtensionIsActiveFunctor>& IsActiveFunctions)
{
	if (!DisplayExtension.IsValid())
	{
		return;
	}

	DisplayExtension->IsActiveThisFrameFunctions = IsActiveFunctions;
}

void UOpenColorIODisplayExtensionWrapper::RemoveSceneExtension()
{
	DisplayExtension.Reset();
}

UOpenColorIODisplayExtensionWrapper* UOpenColorIODisplayExtensionWrapper::CreateOpenColorIODisplayExtension(
	FOpenColorIODisplayConfiguration InDisplayConfiguration,
	const FSceneViewExtensionIsActiveFunctor& IsActiveFunction)
{
	// Create OCIO Scene View Extension and configure it.

	UOpenColorIODisplayExtensionWrapper* OutExtension = NewObject<UOpenColorIODisplayExtensionWrapper>();

	OutExtension->CreateDisplayExtensionIfNotExists();
	OutExtension->SetOpenColorIOConfiguration(InDisplayConfiguration);
	OutExtension->SetSceneExtensionIsActiveFunction(IsActiveFunction);

	return OutExtension;
}