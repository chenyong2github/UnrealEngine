// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/Commandlet.h"
#if WITH_EDITOR
#include "ISourceControlProvider.h"
#endif
#include "ConvertWaterBodyActorsCommandlet.generated.h"

UCLASS(MinimalAPI)
class UConvertWaterBodyActorsCommandlet : public UCommandlet
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
public:
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

protected:
	ULevel* LoadLevel(const FString& LevelToLoad) const;
	void GetSubLevelsToConvert(ULevel* MainLevel, TSet<ULevel*>& SubLevels, bool bRecursive);

	bool UseSourceControl() const { return SourceControlProvider != nullptr; }
	ISourceControlProvider& GetSourceControlProvider() { check(UseSourceControl()); return *SourceControlProvider; }

protected:
	ISourceControlProvider* SourceControlProvider;
#endif
};