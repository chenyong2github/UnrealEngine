// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RCBehaviour.h"
#include "RCVirtualProperty.h"

#include "RCSetAssetByPathBehaviour.generated.h"

class URCVirtualPropertyContainerBase;
class URCUserDefinedStruct;

namespace SetAssetByPathBehaviourHelpers
{
	const FString ContentFolder = FString(TEXT("/Game/"));
	const FName TargetProperty = FName(TEXT("Target Property"));
	const FName DefaultProperty = FName(TEXT("Default Property"));
}

/** Struct to help generate Widgts for the DetailsPanel of the Bahviour */
USTRUCT()
struct FRCSetAssetPath
{
	GENERATED_BODY()

	FRCSetAssetPath() = default;
	
	/** An Array of Strings holding the Path of an Asset, seperated in several String. Will concated back together later. */
	UPROPERTY()
	TArray<FString> PathArray;
};

/**
 * Custom behaviour for Set Asset By Path
 */
UCLASS()
class REMOTECONTROLLOGIC_API URCSetAssetByPathBehaviour : public URCBehaviour
{
	GENERATED_BODY()
public:
	URCSetAssetByPathBehaviour();

	//~ Begin URCBehaviour interface
	virtual void Initialize() override;
	//~ End URCBehaviour interface

	bool SetAssetByPath(const FString& AssetPath, const FString& TargetPropertyString, const FString& DefaultString);

	TArray<UClass*> GetSupportedClasses() const;
public:
	/** Pointer to property container */
	UPROPERTY()
	TObjectPtr<URCVirtualPropertyContainerBase> PropertyInContainer;

	UPROPERTY()
	TObjectPtr<UClass> AssetClass;

	UPROPERTY()
	FRCSetAssetPath PathStruct;

private:
	bool SetAsset(UObject* SetterObject, const FString& PropertyString);
};
