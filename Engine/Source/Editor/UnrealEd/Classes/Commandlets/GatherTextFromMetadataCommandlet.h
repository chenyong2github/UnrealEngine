// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Commandlets/GatherTextCommandletBase.h"
#include "GatherTextFromMetadataCommandlet.generated.h"

/**
 *	UGatherTextFromMetaDataCommandlet: Localization commandlet that collects all text to be localized from generated metadata.
 */
UCLASS()
class UGatherTextFromMetaDataCommandlet : public UGatherTextCommandletBase
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

	struct FGatherParameters
	{
		TArray<FString> InputKeys;
		TArray<FString> OutputNamespaces;
		TArray<FText> OutputKeys;
	};

private:
	void GatherTextFromUObjects(const TArray<FString>& IncludePaths, const TArray<FString>& ExcludePaths, const FGatherParameters& Arguments);
	void GatherTextFromUObject(UField* const Field, const FGatherParameters& Arguments, const FName InPlatformName);
	void GatherTextFromField(FField* const Field, const FGatherParameters& Arguments, const FName InPlatformName);
	bool ShouldGatherFromField(const UField* Field);

private:
	bool ShouldGatherFromEditorOnlyData;

	/** Array of field types (eg, UProperty, UFunction, UScriptStruct, etc) that should be included or excluded in the current gather */
	TArray<const UClass*> FieldTypesToInclude;
	TArray<const UClass*> FieldTypesToExclude;

	/** Array of field owner types (eg, UMyClass, FMyStruct, etc) that should have fields within them included or excluded in the current gather */
	TArray<const UStruct*> FieldOwnerTypesToInclude;
	TArray<const UStruct*> FieldOwnerTypesToExclude;
};
