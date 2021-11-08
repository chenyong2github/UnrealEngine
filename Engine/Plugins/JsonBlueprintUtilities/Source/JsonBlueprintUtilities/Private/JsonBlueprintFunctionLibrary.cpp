// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonBlueprintFunctionLibrary.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "JsonBlueprintFunctionLibrary"

bool UJsonBlueprintFunctionLibrary::FromString(
	UObject* WorldContextObject,
	const FString& JsonString,
	FJsonObjectWrapper& OutJsonObject)
{
	return OutJsonObject.JsonObjectFromString(JsonString);
}

bool UJsonBlueprintFunctionLibrary::FromFile(
	UObject* WorldContextObject,	
	const FFilePath& File,
	FJsonObjectWrapper& OutJsonObject)
{
	if(!FPaths::FileExists(File.FilePath))
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("File not found: %s"), *File.FilePath), ELogVerbosity::Error);
		return false;
	}
	
	FString JsonString;
	if(!FFileHelper::LoadFileToString(JsonString, *File.FilePath))
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Error loading file: %s"), *File.FilePath), ELogVerbosity::Error);
		return false;
	}
	
	return FromString(WorldContextObject, JsonString, OutJsonObject);
}

DEFINE_FUNCTION(UJsonBlueprintFunctionLibrary::execGetField)
{
	P_GET_STRUCT_REF(FJsonObjectWrapper, JsonObject);
	P_GET_PROPERTY(FStrProperty, FieldName);

	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* ValueProp = Stack.MostRecentProperty;
	void* ValuePtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	if (!ValueProp || !ValuePtr)
	{
		const FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AccessViolation,
			LOCTEXT("GetField_MissingOutputProperty", "Failed to resolve the output parameter for GetField.")
		);
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}

	bool bResult;
	if(FieldName.IsEmpty())
	{
		bResult = false;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
		return;
	}

	P_NATIVE_BEGIN
	bResult = JsonFieldToProperty(FieldName, JsonObject, ValueProp, ValuePtr);
	P_NATIVE_END

	*static_cast<bool*>(RESULT_PARAM) = bResult;
}

DEFINE_FUNCTION(UJsonBlueprintFunctionLibrary::execSetField)
{
	P_GET_STRUCT_REF(FJsonObjectWrapper, JsonObject);
	P_GET_PROPERTY(FStrProperty, FieldName);

	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* SourceProperty = Stack.MostRecentProperty;
	void* SourceValuePtr = Stack.MostRecentPropertyAddress;

	P_FINISH;

	if (!SourceProperty || !SourceValuePtr)
	{
		const FBlueprintExceptionInfo ExceptionInfo(
			EBlueprintExceptionType::AccessViolation,
			LOCTEXT("SetField_MissingInputProperty", "Failed to resolve the input parameter for SetField.")
		);
		FBlueprintCoreDelegates::ThrowScriptException(P_THIS, Stack, ExceptionInfo);
	}

	bool bResult;
	if(FieldName.IsEmpty())
	{
		bResult = false;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
		return;
	}

	P_NATIVE_BEGIN
	bResult = PropertyToJsonField(FieldName, SourceProperty, SourceValuePtr, JsonObject);
	// If successful, refresh the stored JsonString
	if(bResult)
	{
		if(!FJsonSerializer::Serialize(JsonObject.JsonObject.ToSharedRef(), TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonObject.JsonString)))
		{
			FFrame::KismetExecutionMessage(TEXT("Error serializing Json Object."), ELogVerbosity::Error);
			bResult = false;
		}
	}
	P_NATIVE_END

	*static_cast<bool*>(RESULT_PARAM) = bResult;
}

bool UJsonBlueprintFunctionLibrary::JsonFieldToProperty(
	const FString& FieldName,
	const FJsonObjectWrapper& SourceObject,
	FProperty* TargetProperty,
	void* TargetValuePtr)
{
	check(SourceObject.JsonObject.IsValid());
	check(TargetProperty && TargetValuePtr);

	// Check that field with name exists
	const TSharedPtr<FJsonValue> JsonValue = SourceObject.JsonObject->TryGetField(FieldName);
	if(!JsonValue.IsValid())
	{
		FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Field '%s' was not found on the provided Json object."), *FieldName), ELogVerbosity::Warning);
		return false;
	}
	
	return FJsonObjectConverter::JsonValueToUProperty(JsonValue, TargetProperty, TargetValuePtr);
}

bool UJsonBlueprintFunctionLibrary::PropertyToJsonField(
	const FString& FieldName,
	FProperty* SourceProperty,
	const void* SourceValuePtr,
	const FJsonObjectWrapper& TargetObject)
{
	check(SourceProperty && SourceValuePtr);
	check(TargetObject.JsonObject.IsValid());

	TargetObject.JsonObject->SetField(FieldName, FJsonObjectConverter::UPropertyToJsonValue(SourceProperty, SourceValuePtr));
	return true;
}

#undef LOCTEXT_NAMESPACE
