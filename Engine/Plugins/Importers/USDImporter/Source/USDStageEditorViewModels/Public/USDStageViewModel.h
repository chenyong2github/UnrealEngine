// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AUsdStageActor;
class UPackage;

class USDSTAGEEDITORVIEWMODELS_API FUsdStageViewModel
{
public:
	void NewStage( const TCHAR* FilePath );
	void OpenStage( const TCHAR* FilePath );
	void ReloadStage();
	void CloseStage();
	void SaveStage();
	void ImportStage();

public:
	TWeakObjectPtr< AUsdStageActor > UsdStageActor;
};
