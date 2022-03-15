// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebAPISwaggerFactory.h"

#include "WebAPIOpenAPILog.h"
#include "WebAPIDefinition.h"
#include "Dom/JsonObject.h"
#include "Dom/WebAPIOperation.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "WebAPISwaggerFactory"

struct FWebAPIOperationResponseKeyFuncs : public TDefaultMapKeyFuncs<uint32, TObjectPtr<UWebAPIOperationResponse>, false>
{
	static FORCEINLINE uint32 GetSetKey(TObjectPtr<UWebAPIOperationResponse> const& Element) { return Element->Code; }
	static FORCEINLINE uint32 GetKeyHash(uint32 const& Key) { return Key; }
	static FORCEINLINE bool Matches(uint32 const& A, uint32 const& B) { return (A == B); }
};

UWebAPISwaggerFactory::UWebAPISwaggerFactory()
	: Provider(MakeShared<FWebAPISwaggerProvider>())
{
}

bool UWebAPISwaggerFactory::CanImportWebAPI(const FString& InFileName, const FString& InFileContents)
{
	TSharedPtr<FJsonObject> JsonObject;
	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(InFileContents), JsonObject))
	{
		return false;
	}

	return JsonObject->HasField(TEXT("swagger"));
}

TFuture<bool> UWebAPISwaggerFactory::ImportWebAPI(UWebAPIDefinition* InDefinition, const FString& InFileName, const FString& InFileContents)
{
	TSharedPtr<FJsonObject> JsonObject;
	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(InFileContents), JsonObject))
	{
		UE_LOG(LogWebAPIOpenAPI, Error, TEXT("Couldn't deserialize file contents as Json"));
		return MakeFulfilledPromise<bool>(false).GetFuture();
	}

	// Write raw spec contents
	UWebAPISwaggerAssetData* ImportData = InDefinition->AddOrGetImportedDataCache<UWebAPISwaggerAssetData>("Swagger");
	ImportData->FileContents = InFileContents;

	// Parse spec
	// @todo: move call to ConvertToWebAPISchema outside of Factory implementation (to provide Pre and Post extensibility)
	return Provider->ConvertToWebAPISchema(InDefinition)
	.Next([](EWebAPIConversionResult bInConversionResult)
	{
		// @todo: UI notification
		//InDefinition->SetWebAPISchema(InWebAPISchema);

		return bInConversionResult == EWebAPIConversionResult::Succeeded;
	});
}

#undef LOCTEXT_NAMESPACE
