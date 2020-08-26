// Copyright Epic Games, Inc. All Rights Reserved.
#include "Utilities/Analytics.h"
#include "UI/MSSettings.h"
#include "Interfaces/IHttpRequest.h"

#include "Runtime/Json/Public/Dom/JsonObject.h"
#include "Runtime/Json/Public/Serialization/JsonSerializer.h"
#include "HttpModule.h"
#include "Misc/EngineVersion.h"

TSharedPtr<FAnalytics> FAnalytics::AnalyticsInst;

TSharedPtr<FAnalytics> FAnalytics::Get()
{
	if (!AnalyticsInst.IsValid())
	{
		AnalyticsInst = MakeShareable(new FAnalytics);
	}
	return AnalyticsInst;
}

FString FAnalytics::GetUEVersion()
{
	FString EngineVersion = TEXT("None");
#if ENGINE_MINOR_VERSION == 23 
	EngineVersion = TEXT("4.23");
#elif ENGINE_MINOR_VERSION == 24
	EngineVersion = TEXT("4.24");
#elif ENGINE_MINOR_VERSION == 25
	EngineVersion = TEXT("4.25");
#elif ENGINE_MINOR_VERSION == 26
	EngineVersion = TEXT("4.26");
#elif ENGINE_MINOR_VERSION == 27
	EngineVersion = TEXT("4.27");
#elif ENGINE_MINOR_VERSION == 28
	EngineVersion = TEXT("4.28");
#endif
	return EngineVersion;
}

void FAnalytics::SendAnalytics(TSharedPtr<FJsonObject> JsonObject)
{
	//http://localhost:28241/analytics
	FString UrlAddressAsString = TEXT("https://stats.quixel.com/v1/message");	
    FString OutputString;
    TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
    //TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("User-Agent"), "X-UnrealEngine-Agent");
    HttpRequest->SetURL(*FString::Printf(TEXT("%s"), *UrlAddressAsString));
    HttpRequest->SetContentAsString(OutputString);
	//UE_LOG(LogTemp, Error, TEXT("Selected asset type is : %s"), *OutputString);
    //HttpRequest->OnProcessRequestComplete().BindUObject(this, &FAnalytics::ResponseReceived);
	
    HttpRequest->ProcessRequest();
}

TSharedPtr<FJsonObject> FAnalytics::GenerateAnalyticsJson()
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> AnalyticsJson = MakeShareable(new FJsonObject);	

    const UMegascansSettings* MegascansSettings = GetDefault<UMegascansSettings>();
	const UMaterialPresetsSettings* MatOverrideSettings = GetDefault< UMaterialPresetsSettings>();

	JsonObject->SetStringField(TEXT("UEVersion"), GetUEVersion());
    JsonObject->SetStringField(TEXT("CreateFoliage"), MegascansSettings->bCreateFoliage ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("EnableLODS"), MegascansSettings->bEnableLods ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("BatchPrompt"), MegascansSettings->bBatchImportPrompt ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("EnableDisplacement"), MegascansSettings->bEnableDisplacement ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("ApplyToSelection"), MegascansSettings->bApplyToSelection ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("FilterMaps"), MegascansSettings->bFilterMasterMaterialMaps ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));    
    JsonObject->SetStringField(TEXT("MaterialOverride_3D"), ( MatOverrideSettings->MasterMaterial3d != nullptr) ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("MaterialOverride_Surface"), ( MatOverrideSettings->MasterMaterialSurface != nullptr) ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
    JsonObject->SetStringField(TEXT("MaterialOverride_3DPlant"), ( MatOverrideSettings->MasterMaterialPlant != nullptr) ? *FString::Printf(TEXT("True")) : *FString::Printf(TEXT("False")));
	
	AnalyticsJson->SetObjectField(TEXT("data"), JsonObject);
	AnalyticsJson->SetStringField(TEXT("event"), TEXT("Unreal_Plugin_Analytics"));
	return AnalyticsJson;
}

TSharedPtr<FJsonObject> FAnalytics::GenerateBlendAnalyticsJson()
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> AnalyticsJson = MakeShareable(new FJsonObject);
    const UMaterialBlendSettings* BlendSettings = GetDefault<UMaterialBlendSettings>();

	JsonObject->SetStringField(TEXT("UEVersion"), GetUEVersion());
    JsonObject->SetStringField(TEXT("BlendMaterialUsed"), *FString::Printf(TEXT("True")) );

	AnalyticsJson->SetObjectField(TEXT("data"), JsonObject);
	AnalyticsJson->SetStringField(TEXT("event"), TEXT("Unreal_Plugin_Analytics"));
	return AnalyticsJson;
    
    
}