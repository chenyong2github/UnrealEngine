// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/ObjectMacros.h"

#include "FieldNotificationId.h"

#include "MVVMDebugViewModel.generated.h"


USTRUCT()
struct FMVVMViewModelFieldBoundDebugEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FString ObjectName;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FFieldNotificationId FieldId;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FName FunctionName;

	TWeakObjectPtr<const UObject> LiveInstanceObject;
};


USTRUCT()
struct FMVVMViewModelDebugEntry
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FString FullName;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FAssetData ViewModelAsset;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	TArray<FMVVMViewModelFieldBoundDebugEntry> FieldBound;

	//UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	//FInstancedPropertyBag PropertyBag;

	UPROPERTY(VisibleAnywhere, Category = "Viewmodel")
	FGuid ViewModelDebugId;

	TWeakObjectPtr<UObject> LiveViewModel;
};