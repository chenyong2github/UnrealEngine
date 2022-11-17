// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnsavedAssetsAutoCheckout.h"

#include "ISourceControlModule.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "SourceControlOperations.h"
#include "UnsavedAssetsTrackerModule.h"

FUnsavedAssetsAutoCheckout::FUnsavedAssetsAutoCheckout(FUnsavedAssetsTrackerModule* Module)
{
	Module->OnUnsavedAssetAdded.AddRaw(this, &FUnsavedAssetsAutoCheckout::AsyncCheckout);
	AsyncCheckoutComplete.BindRaw(this, &FUnsavedAssetsAutoCheckout::OnAsyncCheckoutComplete);
}

FUnsavedAssetsAutoCheckout::~FUnsavedAssetsAutoCheckout()
{
	FUnsavedAssetsTrackerModule::Get().OnUnsavedAssetAdded.RemoveAll(this);
}

void FUnsavedAssetsAutoCheckout::AsyncCheckout(const FString& AbsoluteAssetFilepath)
{
	const UEditorLoadingSavingSettings* Settings = GetDefault<UEditorLoadingSavingSettings>();
	if (!Settings->bAutomaticallyCheckoutOnAssetModification)
	{
		return;
	}
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	FSourceControlOperationRef CheckOutOperation = ISourceControlOperation::Create<FCheckOut>();

	OperationToPath.Add(&CheckOutOperation.Get()) = AbsoluteAssetFilepath;
	
	FUnsavedAssetsTrackerModule::Get().PreUnsavedAssetAutoCheckout.Broadcast(AbsoluteAssetFilepath, CheckOutOperation);
	
	SourceControlProvider.Execute(CheckOutOperation, AbsoluteAssetFilepath, EConcurrency::Asynchronous, AsyncCheckoutComplete);
}

void FUnsavedAssetsAutoCheckout::OnAsyncCheckoutComplete(const FSourceControlOperationRef& CheckOutOperation, ECommandResult::Type Result)
{
	FString const& AbsoluteAssetFilepath = OperationToPath.FindAndRemoveChecked(&CheckOutOperation.Get());
	if (Result == ECommandResult::Succeeded)
	{
		FUnsavedAssetsTrackerModule::Get().PostUnsavedAssetAutoCheckout.Broadcast(AbsoluteAssetFilepath, CheckOutOperation);	
	}
	else if (Result == ECommandResult::Failed)
	{
		FUnsavedAssetsTrackerModule::Get().PostUnsavedAssetAutoCheckoutFailure.Broadcast(AbsoluteAssetFilepath, CheckOutOperation);
	}
}
