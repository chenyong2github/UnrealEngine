// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WebAPIEditorSettings.h"
#include "Dom/WebAPIParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "V2/WebAPISwaggerConverter.h"
#include "V2/WebAPISwaggerConverter.inl"
#include "V2/WebAPISwaggerFactory.h"
#include "V2/WebAPISwaggerSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UE::WebAPI::OpenAPI;

BEGIN_DEFINE_SPEC(FWebAPISwaggerSpec,
				TEXT("Plugin.WebAPI.Swagger"),
				EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::ApplicationContextMask)

	TSharedPtr<UE::WebAPI::OpenAPI::V2::FSwagger> InputDefinition;
	TStrongObjectPtr<UWebAPIDefinition> OutputDefinition;
	TSharedPtr<UE::WebAPI::Swagger::FWebAPISwaggerSchemaConverter> Converter;
	TStrongObjectPtr<UWebAPIDefinitionFactory> Factory;

	FString GetSampleFile(const FString& InName) const
	{
		FString FilePath = FPaths::Combine(FPaths::ProjectPluginsDir(),
			TEXT("WebAPI"), TEXT("Source"), TEXT("WebAPIOpenAPI"),
			TEXT("Private"), TEXT("Tests"), TEXT("Samples"), TEXT("V2"), InName + TEXT(".json"));
		ensure(FPaths::FileExists(FilePath));
		return FilePath;
	}

	FString GetAPISample() const
	{
		return GetSampleFile(TEXT("petstore_V2.json"));
	}

	FString GetDigitalTwinsManagementAPISample() const
	{
		return GetSampleFile(TEXT("digitaltwins"));
	}

	FString GetDigitalTwinsAPISample() const
	{
		return GetSampleFile(TEXT("digitaltwins_data"));
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& InFile) const
	{
		FString FileContents;
		FFileHelper::LoadFileToString(FileContents, *InFile);
	
		TSharedPtr<FJsonObject> JsonObject;
		FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(FileContents), JsonObject);
		return JsonObject;
	}

END_DEFINE_SPEC(FWebAPISwaggerSpec)

// @todo: remove when done: -ExecCmds="Automation RunTests Plugin.WebAPI.Parser.Swagger2" -testexit="Automation Test Queue Empty" -unattended  -nopause
void FWebAPISwaggerSpec::Define()
{
	BeforeEach([this]
	{
		const FString FilePath = GetDigitalTwinsManagementAPISample();
		const TSharedPtr<FJsonObject> JsonObject = LoadJson(FilePath);

		InputDefinition = MakeShared<UE::WebAPI::OpenAPI::V2::FSwagger>();
		InputDefinition->FromJson(JsonObject.ToSharedRef());
					
		OutputDefinition = TStrongObjectPtr(NewObject<UWebAPIDefinition>());

		Converter = MakeShared<UE::WebAPI::Swagger::FWebAPISwaggerSchemaConverter>(
			InputDefinition,
			OutputDefinition->GetWebAPISchema(),
			OutputDefinition->GetMessageLog().ToSharedRef(),
			OutputDefinition->GetProviderSettings());

		Factory = TStrongObjectPtr(NewObject<UWebAPISwaggerFactory>());
	});
	
	Describe("PetStore", [this]
	{
		It("Parses", [this]
		{
			const FString FilePath = GetAPISample();
			const TSharedPtr<FJsonObject> JsonObject = LoadJson(FilePath);

			UE::WebAPI::OpenAPI::V2::FSwagger Object;
			Object.FromJson(JsonObject.ToSharedRef());

			TestEqual("Swagger Version", Object.Swagger, "2.0");
			TestEqual("Info Version", Object.Info->Version, "1.0.5");
			if(TestTrue("Host Set", Object.Host.IsSet()))
			{
				TestEqual("Host", Object.Host.GetValue(), "petstore.swagger.io");
			}

			if(TestTrue("BasePath Set", Object.BasePath.IsSet()))
			{
				TestEqual("BasePath", Object.BasePath.GetValue(), "/v2");	
			}

			if(TestTrue("Tags Set", Object.Tags.IsSet()))
			{
				TestEqual("Tag Num", Object.Tags->Num(), 3);	
			}

			if(TestTrue("Schemes Set", Object.Schemes.IsSet()))
			{
				TestEqual("Scheme Num", Object.Schemes->Num(), 2);
			}

			TestEqual("Paths Num", Object.Paths.Num(), 14);
			
			if(TestTrue("Security Definitions Set", Object.SecurityDefinitions.IsSet()))
			{
				TestEqual("Security Definitions Num", Object.SecurityDefinitions->Num(), 2);
			}
			
			if(TestTrue("Definitions Set", Object.Definitions.IsSet()))
			{
				TestEqual("Definitions Num", Object.Definitions->Num(), 6);
			}

			if(TestTrue("External Docs Set", Object.ExternalDocs.IsSet()))
			{
				TestEqual("External Docs URL", Object.ExternalDocs.GetValue()->Url, TEXT("http://swagger.io"));
			}			
		});
	});

	Describe("DigitalTwinsManagement", [this]
	{
		It("Parses", [this]
		{
			const FString FilePath = GetDigitalTwinsManagementAPISample();
			const TSharedPtr<FJsonObject> JsonObject = LoadJson(FilePath);

			UE::WebAPI::OpenAPI::V2::FSwagger Object;
			Object.FromJson(JsonObject.ToSharedRef());

			TestEqual("Swagger Version", Object.Swagger, "2.0");
			TestEqual("Info Version", Object.Info->Version, "2020-12-01");
			if(TestTrue("Host Set", Object.Host.IsSet()))
			{
				TestEqual("Host", Object.Host.GetValue(), "management.azure.com");
			}

			if(TestTrue("Schemes Set", Object.Schemes.IsSet()))
			{
				TestEqual("Scheme Num", Object.Schemes->Num(), 1);
			}

			TestEqual("Paths Num", Object.Paths.Num(), 11);
				
			if(TestTrue("Security Definitions Set", Object.SecurityDefinitions.IsSet()))
			{
				TestEqual("Security Definitions Num", Object.SecurityDefinitions->Num(), 1);
			}
				
			if(TestTrue("Definitions Set", Object.Definitions.IsSet()))
			{
				TestEqual("Definitions Num", Object.Definitions->Num(), 30);
			}
		});
	});

	Describe("Model Composition", [this]
	{
		// @todo: meaningful name
		It("All-Of", [this]
		{
			const TSharedPtr<UE::WebAPI::OpenAPI::V2::FSchema>* InputModel = InputDefinition->Definitions->Find(TEXT("ServiceBus"));

			const TObjectPtr<UWebAPIModel> OutputModel = Converter->ConvertModel(*InputModel);
			if(TestFalse("Converted Model", OutputModel.IsNull()))
			{
				if(TestTrue("All-Of is set", (*InputModel)->AllOf.IsSet()))
				{
					for(UE::Json::TJsonReference<UE::WebAPI::OpenAPI::V2::FSchema>& SubType : (*InputModel)->AllOf.GetValue())
					{
					
					}
				}
			}
		});
	});

	Describe("Type Conversion", [this]
	{
		BeforeEach([this]
		{
			const FString FilePath = GetDigitalTwinsAPISample();
			const TSharedPtr<FJsonObject> JsonObject = LoadJson(FilePath);

			InputDefinition = MakeShared<UE::WebAPI::OpenAPI::V2::FSwagger>();
			InputDefinition->FromJson(JsonObject.ToSharedRef());
					
			OutputDefinition = TStrongObjectPtr(NewObject<UWebAPIDefinition>());

			Converter = MakeShared<UE::WebAPI::Swagger::FWebAPISwaggerSchemaConverter>(
				InputDefinition,
				OutputDefinition->GetWebAPISchema(),
				OutputDefinition->GetMessageLog().ToSharedRef(),
				OutputDefinition->GetProviderSettings());

			Factory = TStrongObjectPtr(NewObject<UWebAPISwaggerFactory>());
		});
		// // @todo: meaningful name
		// It("Test", [this]()
		// {
		// 	const TSharedPtr<UE::WebAPI::OpenAPI::V2::FSchema>* DigitalTwinsProperties = InputDefinition->Definitions->Find(TEXT("DigitalTwinsProperties"));
		//
		// 	const TObjectPtr<UWebAPITypeInfo> Type = Converter->ResolveType(*DigitalTwinsProperties, TEXT("DigitalTwinsProperties"));
		//
		// 	TestTrue("TypeInfo is not null", Type != nullptr);
		// });

		It("Enum that's non-optional and has a single value should default to that value", [this]
		{
			check(Converter->Convert());
			Factory->PostImportWebAPI(OutputDefinition.Get()).Wait();
			
			const FString EnumNameToTest = TEXT("ApiVersion");

			TObjectPtr<UWebAPIModelBase>* FoundModel = OutputDefinition->GetWebAPISchema()->Models.FindByPredicate([&](const TObjectPtr<UWebAPIModelBase>& InModel)
			{
				return InModel->IsA(UWebAPIEnum::StaticClass()) && InModel->GetName().Contains(EnumNameToTest);
			});

			if(!TestNotNull("Found Enum", FoundModel)
				|| !TestTrue("Found Enum not nullptr", !FoundModel->IsNull()))
			{
				return false;				
			}

			const TObjectPtr<UWebAPIEnum> FoundEnum = Cast<UWebAPIEnum>(*FoundModel);
			TestEqual("Single value", FoundEnum->Values.Num(), 1);
			TestTrue("Default value is the first value", FoundEnum->GetDefaultValue().Contains(FoundEnum->Values[0]->Name.ToMemberName(TEXT("EV"))));

 			return true;
		});

		It("String Array that's optional should have an empty array as a default value", [this]
		{
			check(Converter->Convert());
			Factory->PostImportWebAPI(OutputDefinition.Get()).Wait();
			
			const FString ObjectNameToTest = TEXT("IncomingRelationshipCollection");
			const FString PropertyNameToTest = TEXT("Value");

			TObjectPtr<UWebAPIModelBase>* FoundModelBase = OutputDefinition->GetWebAPISchema()->Models.FindByPredicate([&](const TObjectPtr<UWebAPIModelBase>& InModel)
			{
				return InModel->IsA(UWebAPIModel::StaticClass()) && InModel->GetName().Contains(ObjectNameToTest);
			});

			if(!TestNotNull("Found Model", FoundModelBase)
				|| !TestTrue("Found Model not nullptr", !FoundModelBase->IsNull()))
			{
				return false;				
			}

			const TObjectPtr<UWebAPIModel> FoundModel = Cast<UWebAPIModel>(*FoundModelBase);

			const TObjectPtr<UWebAPIProperty>* StringArrayProperty = FoundModel->Properties.FindByPredicate([&](const TObjectPtr<UWebAPIProperty>& InProperty)
			{
				return InProperty->Name.ToString(true) == PropertyNameToTest;				
			});

			const FString DefaultValue = (*StringArrayProperty)->GetDefaultValue();
			TestTrue("Empty Array default value", DefaultValue.Len() <= 3);

			return true;
		});
		
		It("Default value should be set in metadata", [this]
		{

		});

		It("Properties marked as required in the root of a definition are required in the output object(s)", [this]
		{
			check(Converter->Convert());
			Factory->PostImportWebAPI(OutputDefinition.Get()).Wait();
					
			const FString EnumNameToTest = TEXT("ApiVersion");

			TObjectPtr<UWebAPIModelBase>* FoundModel = OutputDefinition->GetWebAPISchema()->Models.FindByPredicate([&](const TObjectPtr<UWebAPIModelBase>& InModel)
			{
				return InModel->GetName().Contains(EnumNameToTest);
			});

			if(!TestNotNull("Found Enum", FoundModel)
				|| TestTrue("Found Enum not nullptr", !FoundModel->IsNull()))
			{
				return false;				
			}

			const TObjectPtr<UWebAPIEnum> FoundEnum = Cast<UWebAPIEnum>(*FoundModel);
			const TObjectPtr<UWebAPIEnumValue>* FoundEnumValue = FoundEnum->Values.FindByPredicate([&](const TObjectPtr<UWebAPIEnumValue>& InEnumValue)
			{
				return InEnumValue->Name == OutputDefinition->GetProviderSettings().GetUnsetEnumValueName();
			});

			TestNull("Unset value not found", FoundEnumValue);

			return true;
		});
	});

	Describe("Parameter", [this]
	{
		It("Resolves schema-less, built-in type", [this]
		{
			const TSharedPtr<UE::WebAPI::OpenAPI::V2::FParameter>* InputParameter = InputDefinition->Parameters->Find(TEXT("resourceName"));

			const TObjectPtr<UWebAPIParameter> OutputParameter = Converter->ConvertParameter(*InputParameter);
			if(TestFalse("Converted Parameter", OutputParameter.IsNull()))
			{
				TestEqual("Type is String", OutputParameter->Type.ToString(true), TEXT("String"));
				TestTrue("Object is named correctly", OutputParameter->GetFName().ToString().StartsWith(TEXT("WebAPIParameter_ResourceName")));
			}
		});

		It("Resolves schema-less, generated enum type", [this]
		{
			const TSharedPtr<UE::WebAPI::OpenAPI::V2::FParameter>* InputParameter = InputDefinition->Parameters->Find(TEXT("api-version"));

			const TObjectPtr<UWebAPIParameter> OutputParameter = Converter->ConvertParameter(*InputParameter);
			if(TestFalse("Converted Parameter", OutputParameter.IsNull()))
			{
				TestTrue("Type is Enum", OutputParameter->Type.TypeInfo->IsEnum());

				UWebAPIEnum* Model = Cast<UWebAPIEnum>(OutputParameter->Type.TypeInfo->Model.Get());
				if(TestTrue("Underlying Enum", Model != nullptr))
				{
					// TestTrue("Type is Enum", OutputParameter->Type.TypeInfo->IsEnum());
				}
			}
		});
	});
		
	Describe("Operation", [this]
	{
		It("Resolves referenced parameters", [this]
		{
			const FString Url = TEXT("/subscriptions/{subscriptionId}/resourceGroups/{resourceGroupName}/providers/Microsoft.DigitalTwins/digitalTwinsInstances/{resourceName}/privateEndpointConnections/{privateEndpointConnectionName}");

			const TSharedPtr<V2::FPath, ESPMode::ThreadSafe>* InputService = InputDefinition->Paths.Find(Url);
			const FString ServiceTag = TEXT("PrivateEndpoints");
			const TObjectPtr<UWebAPIService> OutputService = Converter->ConvertService(ServiceTag);
			
			const TSharedPtr<UE::WebAPI::OpenAPI::V2::FOperation>& InputOperation = InputDefinition->Paths
				.Find(Url)
				->Get()->Get;

			const TObjectPtr<UWebAPIOperation> OutputOperation = Converter->ConvertOperation(Url, TEXT("Get"), InputOperation);
			const FString OperationName = OutputOperation->Name.ToString(true);
			
			if(TestFalse("Converted Operation", OutputOperation.IsNull()))
			{
				TestTrue("Object is named correctly", OutputOperation->GetFName().ToString().StartsWith(TEXT("WebAPIOperation_PrivateEndpoints_") + OperationName));
			}
		});

	});
}

#endif
#endif
