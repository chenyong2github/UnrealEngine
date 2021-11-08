// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "JsonObjectWrapper.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "JsonBlueprintFunctionLibrary.generated.h"

/** */
UCLASS(BlueprintType)
class JSONBLUEPRINTUTILITIES_API UJsonBlueprintFunctionLibrary final : public UBlueprintFunctionLibrary
{ 
	GENERATED_BODY()

public:
	/** Creates a JsonObject from the provided Json string. */
	UFUNCTION(BlueprintCallable, Category="Json", meta = (WorldContext="WorldContextObject",  HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName="Load Json from String"))
	static UPARAM(DisplayName="Success") bool FromString(UObject* WorldContextObject, const FString& JsonString, UPARAM(DisplayName="JsonObject") FJsonObjectWrapper& OutJsonObject);

	/** Creates a JsonObject from the provided Json file. */
	UFUNCTION(BlueprintCallable, Category="Json", meta = (WorldContext="WorldContextObject",  HidePin = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName="Load Json from File"))
	static UPARAM(DisplayName="Success") bool FromFile(UObject* WorldContextObject, const FFilePath& File, UPARAM(DisplayName="JsonObject") FJsonObjectWrapper& OutJsonObject);

	/** Gets the value of the specified field. */
	UFUNCTION(BlueprintCallable, CustomThunk, BlueprintInternalUseOnly, Category="Json", meta = (CustomStructureParam = "OutValue", AutoCreateRefTerm = "OutValue"))
	static UPARAM(DisplayName="Success") bool GetField(const FJsonObjectWrapper& JsonObject, const FString& FieldName, UPARAM(DisplayName="Value") int32& OutValue);
	DECLARE_FUNCTION(execGetField);

	/** Adds (new) or sets (existing) the value of the specified field. */
	UFUNCTION(BlueprintCallable, CustomThunk, BlueprintInternalUseOnly, Category="Json", meta = (CustomStructureParam = "Value", AutoCreateRefTerm = "Value"))
	static UPARAM(DisplayName="Success") bool SetField(const FJsonObjectWrapper& JsonObject, const FString& FieldName, const int32& Value);
	DECLARE_FUNCTION(execSetField);

private:
	/** FieldName only used for logging, SrcValue should be the resolved field. */
	static bool JsonFieldToProperty(const FString& FieldName, const FJsonObjectWrapper& SourceObject, FProperty* TargetProperty, void* TargetValuePtr);

	// If FieldName empty, Object created as root, or created with name "Value" otherwise. 
	static bool PropertyToJsonField(const FString& FieldName, FProperty* SourceProperty, const void* SourceValuePtr, const FJsonObjectWrapper& TargetObject);
};
