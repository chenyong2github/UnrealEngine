// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebRemoteControl.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "IRemoteControlModule.h"
#include "WebRemoteControlUtils.h"
#include "RemoteControlRoute.h"
#include "WebRemoteControlSettings.h"
#include "RemoteControlPreset.h"
#include "WebSocketMessageHandler.h"

#if WITH_EDITOR
// Settings
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

// Serialization
#include "Backends/JsonStructDeserializerBackend.h"
#include "Backends/JsonStructSerializerBackend.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// Http server
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpRequestHandler.h"
#include "HttpServerConstants.h"

// Commands
#include "HAL/IConsoleManager.h"
#include "UObject/StructOnScope.h"

// Requests, Models, Responses
#include "RemoteControlRequest.h"
#include "RemoteControlResponse.h"
#include "RemoteControlModels.h"
#include "HttpServerHttpVersion.h"

// Asset registry
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"

// Miscelleanous
#include "Misc/App.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

#define LOCTEXT_NAMESPACE "WebRemoteControl"


// Boot the server on startup flag
static TAutoConsoleVariable<int32> CVarWebControlStartOnBoot(TEXT("WebControl.EnableServerOnStartup"), 0, TEXT("Enable the Web Control servers (web and websocket) on startup."));

// Enable experimental remote routes
static TAutoConsoleVariable<int32> CVarWebControlEnableExperimentalRoutes(TEXT("WebControl.EnableExperimentalRoutes"), 0, TEXT("Enable the Web Control server experimental routes."));

void FWebRemoteControlModule::StartupModule()
{
#if WITH_EDITOR
	RegisterSettings();
#endif

	WebSocketRouter = MakeShared<FWebsocketMessageRouter>();

	HttpServerPort = GetDefault<UWebRemoteControlSettings>()->RemoteControlHttpServerPort;
	WebSocketServerPort = GetDefault<UWebRemoteControlSettings>()->RemoteControlWebSocketServerPort;

	WebSocketHandler = MakeUnique<FWebSocketMessageHandler>(&WebSocketServer);

	RegisterConsoleCommands();
	RegisterRoutes();

	const bool bIsHeadless = !FApp::CanEverRender();

	if ((!bIsHeadless && GetDefault<UWebRemoteControlSettings>()->bAutoStartWebServer) || CVarWebControlStartOnBoot.GetValueOnAnyThread() > 0)
	{
		StartHttpServer();
	}

	if ((!bIsHeadless && GetDefault<UWebRemoteControlSettings>()->bAutoStartWebSocketServer) || CVarWebControlStartOnBoot.GetValueOnAnyThread() > 0)
	{
		StartWebSocketServer();
	}
}

void FWebRemoteControlModule::ShutdownModule()
{
	EditorRoutes.UnregisterRoutes(this);
	WebSocketHandler->UnregisterRoutes(this);
	StopHttpServer();
	StopWebSocketServer();
	UnregisterConsoleCommands();
#if WITH_EDITOR
	UnregisterSettings();
#endif
}

void FWebRemoteControlModule::RegisterRoute(const FRemoteControlRoute& Route)
{
	RegisteredHttpRoutes.Add(Route);

	// If the route is registered after the server is already started.
	if (HttpRouter)
	{
		StartRoute(Route);
	}
}

void FWebRemoteControlModule::UnregisterRoute(const FRemoteControlRoute& Route)
{
	RegisteredHttpRoutes.Remove(Route);
	const uint32 RouteHash = GetTypeHash(Route);
	if (FHttpRouteHandle* Handle = ActiveRouteHandles.Find(RouteHash))
	{
		if (HttpRouter)
		{
			HttpRouter->UnbindRoute(*Handle);
		}
		ActiveRouteHandles.Remove(RouteHash);
	}
}

void FWebRemoteControlModule::RegisterWebsocketRoute(const FRemoteControlWebsocketRoute& Route)
{
	WebSocketRouter->BindRoute(Route.MessageName, Route.Delegate);
}

void FWebRemoteControlModule::UnregisterWebsocketRoute(const FRemoteControlWebsocketRoute& Route)
{
	WebSocketRouter->UnbindRoute(Route.MessageName);
}

void FWebRemoteControlModule::StartHttpServer()
{
	if (!HttpRouter)
	{
		HttpRouter = FHttpServerModule::Get().GetHttpRouter(HttpServerPort);
		if (!HttpRouter)
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Call server couldn't be started on port %d"), HttpServerPort);
			return;
		}

		for (FRemoteControlRoute& Route : RegisteredHttpRoutes)
		{
			StartRoute(Route);
		}

		FHttpServerModule::Get().StartAllListeners();

		OnHttpServerStartedDelegate.Broadcast(HttpServerPort);
	}
}

void FWebRemoteControlModule::StopHttpServer()
{
	if (FHttpServerModule::IsAvailable())
	{
		FHttpServerModule::Get().StopAllListeners();
	}

	if (HttpRouter)
	{
		for (const TPair<uint32, FHttpRouteHandle>& Tuple : ActiveRouteHandles)
		{
			if (Tuple.Key)
			{
				HttpRouter->UnbindRoute(Tuple.Value);
			}
		}

		ActiveRouteHandles.Reset();
	}

	HttpRouter.Reset();
	OnHttpServerStoppedDelegate.Broadcast();
}

void FWebRemoteControlModule::StartWebSocketServer()
{
	if (!WebSocketServer.IsRunning())
	{
		if (!WebSocketServer.Start(WebSocketServerPort, WebSocketRouter))
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Call WebSocket server couldn't be started on port %d"), WebSocketServerPort);
#if WITH_EDITOR
			FNotificationInfo Info{FText::Format(LOCTEXT("FailedStartRemoteControlServer", "Web Remote Call WebSocket server couldn't be started on port {0}"), WebSocketServerPort)};
			Info.bFireAndForget = true;
			Info.ExpireDuration =  3.0f;
			FSlateNotificationManager::Get().AddNotification(MoveTemp(Info));
#endif /*WITH_EDITOR*/

			return;
		}

		UE_LOG(LogRemoteControl, Log, TEXT("Web Remote Control WebSocket server started on port %d"), WebSocketServerPort);
		OnWebSocketServerStartedDelegate.Broadcast(WebSocketServerPort);
	}
}

void FWebRemoteControlModule::StopWebSocketServer()
{
	WebSocketServer.Stop();
	OnWebSocketServerStoppedDelegate.Broadcast();
}

void FWebRemoteControlModule::StartRoute(const FRemoteControlRoute& Route)
{
	// The handler is wrapped in a lambda since HttpRouter::BindRoute only accepts TFunctions
	ActiveRouteHandles.Add(GetTypeHash(Route), HttpRouter->BindRoute(Route.Path, Route.Verb, [this, Handler = Route.Handler](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) { return Handler.Execute(Request, OnComplete); }));
}

void FWebRemoteControlModule::RegisterRoutes()
{
	// Misc
	RegisterRoute({
		TEXT("Get information about different routes available on this API."),
		FHttpPath(TEXT("/remote/info")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleInfoRoute)
		});

	RegisterRoute({
		TEXT("Allows cross-origin http requests to the API."),
		FHttpPath(TEXT("/remote")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleOptionsRoute)
		});

	// Raw API
	RegisterRoute({
		TEXT("Allows batching multiple calls into one request."),
		FHttpPath(TEXT("/remote/batch")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleBatchRequest)
		});

	RegisterRoute({
		TEXT("Call a function on a remote object."),
		FHttpPath(TEXT("/remote/object/call")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleObjectCallRoute)
		});

	RegisterRoute({
		TEXT("Read or write a property on a remote object."),
		FHttpPath(TEXT("/remote/object/property")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleObjectPropertyRoute)
		});

	RegisterRoute({
		TEXT("Describe an object."),
		FHttpPath(TEXT("/remote/object/describe")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleDescribeObjectRoute)
		});

	// Preset API
	RegisterRoute({
		TEXT("Get a remote control preset's content."),
		FHttpPath(TEXT("/remote/preset/:preset")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleGetPresetRoute)
		});

	RegisterRoute({
		TEXT("Get a list of available remote control presets."),
		FHttpPath(TEXT("/remote/presets")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleGetPresetsRoute)
		});

	RegisterRoute({
		TEXT("Call a function on a preset."),
		FHttpPath(TEXT("/remote/preset/:preset/function/:functionname")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandlePresetCallFunctionRoute)
		});

	RegisterRoute({
		TEXT("Set a property on a preset."),
		FHttpPath(TEXT("/remote/preset/:preset/property/:propertyname")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandlePresetSetPropertyRoute)
		});

	RegisterRoute({
		TEXT("Get a property on a preset."),
		FHttpPath(TEXT("/remote/preset/:preset/property/:propertyname")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandlePresetGetPropertyRoute)
		});

	RegisterRoute({
		TEXT("Get a preset"),
		FHttpPath(TEXT("/remote/preset/:preset")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleGetPresetRoute)
		});
	
	// Search
	RegisterRoute({
		TEXT("Search for assets"),
		FHttpPath(TEXT("/remote/search/assets")),
		EHttpServerRequestVerbs::VERB_PUT,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleSearchAssetRoute)
		});

	// Metadata
	RegisterRoute({
		TEXT("Get a preset's metadata"),
		FHttpPath(TEXT("/remote/preset/:preset/metadata")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleGetMetadataRoute)
		});

	RegisterRoute({
		TEXT("Get/Set/Delete a preset metadata field"),
		FHttpPath(TEXT("/remote/preset/:preset/metadata/:metadatafield")),
		EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_PUT | EHttpServerRequestVerbs::VERB_DELETE,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleMetadataFieldOperationsRoute)
		});

	//**************************************
	// Special websocket route just using http request
	RegisterWebsocketRoute({
		TEXT("Route a message that targets a http route."),
		TEXT("http"),
		FWebSocketMessageDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleWebSocketHttpMessage)
		});

	WebSocketHandler->RegisterRoutes(this);

	EditorRoutes.RegisterRoutes(this);
}

void FWebRemoteControlModule::RegisterConsoleCommands()
{
	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("WebControl.StartServer"),
		TEXT("Start the http remote control web server"),
		FConsoleCommandDelegate::CreateRaw(this, &FWebRemoteControlModule::StartHttpServer)
		));

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("WebControl.StopServer"),
		TEXT("Stop the http remote control web server"),
		FConsoleCommandDelegate::CreateRaw(this, &FWebRemoteControlModule::StopHttpServer)
		));

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("WebControl.StartWebSocketServer"),
		TEXT("Start the WebSocket remote control web server"),
		FConsoleCommandDelegate::CreateRaw(this, &FWebRemoteControlModule::StartWebSocketServer)
		));

	ConsoleCommands.Add(MakeUnique<FAutoConsoleCommand>(
		TEXT("WebControl.StopWebSocketServer"),
		TEXT("Stop the WebSocket remote control web server"),
		FConsoleCommandDelegate::CreateRaw(this, &FWebRemoteControlModule::StopWebSocketServer)
		));
}

void FWebRemoteControlModule::UnregisterConsoleCommands()
{
	for (TUniquePtr<FAutoConsoleCommand>& Command : ConsoleCommands)
	{
		Command.Reset();
	}
}

bool FWebRemoteControlModule::HandleInfoRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes::Ok);

	TArray<uint8> Buffer;
	FMemoryWriter Writer{ Buffer };
	FJsonStructSerializerBackend SerializerBackend(Writer, EStructSerializerBackendFlags::Default);

	TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteIdentifierPrefix(TEXT("HttpRoutes"));
	JsonWriter->WriteArrayStart();
	for (const FRemoteControlRoute& Route : RegisteredHttpRoutes)
	{
		FRemoteControlRouteDescription Description{ Route };
		FStructSerializer::Serialize((const void*)&Description, *FRemoteControlRouteDescription::StaticStruct(), SerializerBackend, FStructSerializerPolicies());
	}
	JsonWriter->WriteArrayEnd();
	JsonWriter->WriteObjectEnd();
	WebRemoteControlUtils::ConvertToUTF8(Buffer, Response->Body);

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleBatchRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	if (!WebRemoteControlUtils::ValidateContentType(Request, TEXT("application/json"), OnComplete))
	{
		return true;
	}

	Response->Code = EHttpServerResponseCodes::Ok;

	FRCBatchRequest BatchRequest;
	if (!WebRemoteControlUtils::DeserializeRequest(Request, &OnComplete, BatchRequest))
	{
		return true;
	}

	FMemoryWriter Writer(Response->Body);
	TSharedPtr<TJsonWriter<ANSICHAR>> JsonWriter = TJsonWriter<ANSICHAR>::Create(&Writer);

	JsonWriter->WriteObjectStart();
	JsonWriter->WriteIdentifierPrefix("Responses");
	JsonWriter->WriteArrayStart();

	for (FRCRequestWrapper& Wrapper : BatchRequest.Requests)
	{
		// This makes sure the Json writer is in a good state before writing raw data.
		JsonWriter->WriteRawJSONValue(TEXT(""));
		InvokeWrappedRequest(Wrapper, Writer, &Request);
	}

	JsonWriter->WriteArrayEnd();
	JsonWriter->WriteObjectEnd();

	OnComplete(MoveTemp(Response));

	return true;
}

bool FWebRemoteControlModule::HandleOptionsRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	OnComplete(WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes::Ok));
	return true;
}

bool FWebRemoteControlModule::HandleObjectCallRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FString ErrorText;
	if (!WebRemoteControlUtils::ValidateContentType(Request, TEXT("application/json"), OnComplete))
	{
		return true;
	}

	FRCCall Call;
	if (!RemotePayloadSerializer::DeserializeCall(Request, Call, OnComplete))
	{
		return true;
	}

	// if we haven't resolved the object or function, return not found
	if (!Call.IsValid())
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("The object or function was not found."), Response->Body);
		Response->Code = EHttpServerResponseCodes::NotFound;
		OnComplete(MoveTemp(Response));
		return true;
	}

	IRemoteControlModule::Get().InvokeCall(Call);

	TArray<uint8> WorkingBuffer;
	WorkingBuffer.Empty();
	if (!RemotePayloadSerializer::SerializeCall(Call, WorkingBuffer, true))
	{
		Response->Code = EHttpServerResponseCodes::ServerError;
	}
	else
	{
		WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Response->Body);
		Response->Code = EHttpServerResponseCodes::Ok;
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleObjectPropertyRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	if (!WebRemoteControlUtils::ValidateContentType(Request, TEXT("application/json"), OnComplete))
	{
		return true;
	}

	FRCObjectReference ObjectRef;
	bool bResetToDefault = false;
	FString ErrorText;
	FRCObjectRequest DeserializedRequest;

	if (!RemotePayloadSerializer::DeserializeObjectRef(Request, ObjectRef, DeserializedRequest, OnComplete))
	{
		return true;
	}

	// If we haven't found the object, return a not found error code
	if (!ObjectRef.IsValid())
	{
		Response->Code = EHttpServerResponseCodes::NotFound;
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Unable to find the object."), Response->Body);
		OnComplete(MoveTemp(Response));
		return true;
	}

	switch (ObjectRef.Access)
	{
	case ERCAccess::READ_ACCESS:
	{
		TArray<uint8> WorkingBuffer;
		FMemoryWriter Writer(WorkingBuffer);
		FJsonStructSerializerBackend SerializerBackend(Writer, EStructSerializerBackendFlags::Default);
		if (IRemoteControlModule::Get().GetObjectProperties(ObjectRef, SerializerBackend))
		{
			Response->Code = EHttpServerResponseCodes::Ok;
			WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Response->Body);
		}
	}
	break;
	case ERCAccess::WRITE_ACCESS:
	case ERCAccess::WRITE_TRANSACTION_ACCESS:
	{
		const FBlockDelimiters& PropertyValueDelimiters = DeserializedRequest.GetStructParameters().FindChecked(FRCObjectRequest::PropertyValueLabel());

		if (bResetToDefault)
		{
			if (IRemoteControlModule::Get().ResetObjectProperties(ObjectRef))
			{
				Response->Code = EHttpServerResponseCodes::Ok;
			}
		}
		else if (PropertyValueDelimiters.BlockStart > 0)
		{
			FMemoryReader Reader(DeserializedRequest.TCHARBody);
			Reader.Seek(PropertyValueDelimiters.BlockStart);
			Reader.SetLimitSize(PropertyValueDelimiters.BlockEnd + 1);
			FJsonStructDeserializerBackend DeserializerBackend(Reader);
			if (IRemoteControlModule::Get().SetObjectProperties(ObjectRef, DeserializerBackend))
			{
				Response->Code = EHttpServerResponseCodes::Ok;
			}
		}
	}
	break;
	default:
		// Bad request
		break;
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandlePresetCallFunctionRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FResolvePresetFieldArgs Args;
	Args.PresetName = Request.PathParams.FindChecked(TEXT("preset"));
	Args.FieldLabel = Request.PathParams.FindChecked(TEXT("functionname"));

	TOptional<FExposedFunction> ExposedFunction = IRemoteControlModule::Get().ResolvePresetFunction(Args);

	if (!ExposedFunction)
	{
		Response->Code = EHttpServerResponseCodes::NotFound;
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Unable to resolve the preset field."), Response->Body);
		OnComplete(MoveTemp(Response));
		return true;
	}

	FRCPresetCallRequest CallRequest;
	if (!WebRemoteControlUtils::DeserializeRequest(Request, &OnComplete, CallRequest))
	{
		return true;
	}

	FBlockDelimiters Delimiters = CallRequest.GetParameterDelimiters(FRCPresetCallRequest::ParametersLabel());

	FMemoryReader Reader{ CallRequest.TCHARBody };
	FJsonStructDeserializerBackend ReaderBackend{ Reader };

	TArray<uint8> OutputBuffer;
	FMemoryWriter Writer{ OutputBuffer };
	TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);
	FJsonStructSerializerBackend WriterBackend{ Writer, EStructSerializerBackendFlags::Default };

	JsonWriter->WriteObjectStart();
	JsonWriter->WriteIdentifierPrefix("ReturnedValues");
	JsonWriter->WriteArrayStart();

	bool bSuccess = false;

	if (Delimiters.BlockStart != Delimiters.BlockEnd)
	{
		Reader.Seek(Delimiters.BlockStart);
		Reader.SetLimitSize(Delimiters.BlockEnd + 1);

		// Copy the default arguments.
		FStructOnScope FunctionArgs{ ExposedFunction->Function };
		for (TFieldIterator<FProperty> It(ExposedFunction->Function); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				It->CopyCompleteValue_InContainer(FunctionArgs.GetStructMemory(), ExposedFunction->DefaultParameters->GetStructMemory());
			}
		}

		// Deserialize the arguments passed from the user onto the copy of default arguments.
		if (FStructDeserializer::Deserialize((void*)FunctionArgs.GetStructMemory(), *const_cast<UStruct*>(FunctionArgs.GetStruct()), ReaderBackend, FStructDeserializerPolicies()))
		{
			bSuccess = true;
			for (UObject* Object : ExposedFunction->OwnerObjects)
			{
				FRCCallReference CallRef;
				CallRef.Object = Object;
				CallRef.Function = ExposedFunction->Function;

				FRCCall Call;
				Call.CallRef = MoveTemp(CallRef);
				Call.bGenerateTransaction = CallRequest.GenerateTransaction;
				Call.ParamStruct = FStructOnScope(FunctionArgs.GetStruct(), FunctionArgs.GetStructMemory());

				bSuccess &= IRemoteControlModule::Get().InvokeCall(Call);
				if (bSuccess)
				{
					FStructOnScope ReturnedStruct{ FunctionArgs.GetStruct() };
					TSet<FProperty*> OutProperties;

					// Only copy the out/return parameters from the StructOnScope resulting from the call.
					for (TFieldIterator<FProperty> It(ExposedFunction->Function); It; ++It)
					{
						if (It->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
						{
							OutProperties.Add(*It);
							// Copy the out/return values into the returned struct.
							It->CopyCompleteValue_InContainer(ReturnedStruct.GetStructMemory(), FunctionArgs.GetStructMemory());

							// Then clear the out/return values.
							It->ClearValue_InContainer(FunctionArgs.GetStructMemory());
						}
					}

					FStructSerializerPolicies Policies;
					Policies.PropertyFilter = [&OutProperties](const FProperty* CurrentProp, const FProperty* ParentProp) { return OutProperties.Contains(CurrentProp); };
					FStructSerializer::Serialize((void*)ReturnedStruct.GetStructMemory(), *const_cast<UStruct*>(ReturnedStruct.GetStruct()), WriterBackend, Policies);
				}
			}
		}
	}

	JsonWriter->WriteArrayEnd();
	JsonWriter->WriteObjectEnd();

	if (bSuccess)
	{
		Response->Code = EHttpServerResponseCodes::Ok;
		WebRemoteControlUtils::ConvertToUTF8(OutputBuffer, Response->Body);
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Error while trying to call function %s."), *Args.FieldLabel), Response->Body);
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandlePresetSetPropertyRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	if (!WebRemoteControlUtils::ValidateContentType(Request, TEXT("application/json"), OnComplete))
	{
		return true;
	}

	FRCPresetSetPropertyRequest SetPropertyRequest;
	if (!WebRemoteControlUtils::DeserializeRequest(Request, &OnComplete, SetPropertyRequest))
	{
		return true;
	}

	FResolvePresetFieldArgs Args;
	Args.PresetName = Request.PathParams.FindChecked(TEXT("preset"));
	Args.FieldLabel = Request.PathParams.FindChecked(TEXT("propertyname"));

	URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(*Args.PresetName);
	if (Preset == nullptr)
	{
		Response->Code = EHttpServerResponseCodes::NotFound;
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Unable to resolve the preset."), Response->Body);
		OnComplete(MoveTemp(Response));
		return true;
	}

	TOptional<FExposedProperty> ExposedProperty = Preset->ResolveExposedProperty(*Args.FieldLabel);
	TOptional<FRemoteControlProperty> RemoteControlProperty = Preset->GetProperty(*Args.FieldLabel);

	if (!ExposedProperty.IsSet() || !RemoteControlProperty.IsSet())
	{
		Response->Code = EHttpServerResponseCodes::NotFound;
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Unable to resolve the preset field."), Response->Body);
		OnComplete(MoveTemp(Response));
		return true;
	}

	FRCObjectReference ObjectRef;

	// Replace PropertyValue with the underlying property name.
	TArray<uint8> NewPayload;
	RemotePayloadSerializer::ReplaceFirstOccurence(SetPropertyRequest.TCHARBody, TEXT("PropertyValue"), ExposedProperty->Property->GetName(), NewPayload);

	// Then deserialize the payload onto all the bound objects.
	FMemoryReader NewPayloadReader(NewPayload);
	FJsonStructDeserializerBackend Backend(NewPayloadReader);

	ObjectRef.Property = ExposedProperty->Property;
	ObjectRef.Access = SetPropertyRequest.GenerateTransaction ? ERCAccess::WRITE_TRANSACTION_ACCESS : ERCAccess::WRITE_ACCESS;

	bool bSuccess = true;

	for (UObject* Object : ExposedProperty->OwnerObjects)
	{
		IRemoteControlModule::Get().ResolveObjectProperty(ObjectRef.Access, Object, RemoteControlProperty->FieldPathInfo.ToString(), ObjectRef);

		if (SetPropertyRequest.ResetToDefault)
		{
			bSuccess &= IRemoteControlModule::Get().ResetObjectProperties(ObjectRef);
		}
		else
		{
			NewPayloadReader.Seek(0);
			bSuccess &= IRemoteControlModule::Get().SetObjectProperties(ObjectRef, Backend);
		}
	}

	if (bSuccess)
	{
		Response->Code = EHttpServerResponseCodes::Ok;
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Error while trying to modify property %s."), *Args.FieldLabel), Response->Body);
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandlePresetGetPropertyRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FResolvePresetFieldArgs Args;
	Args.PresetName = Request.PathParams.FindChecked(TEXT("preset"));
	Args.FieldLabel = Request.PathParams.FindChecked(TEXT("propertyname"));
	
	URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(*Args.PresetName);
	if (Preset == nullptr)
	{
		Response->Code = EHttpServerResponseCodes::NotFound;
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Unable to resolve the preset."), Response->Body);
		OnComplete(MoveTemp(Response));
		return true;
	}

	TOptional<FExposedProperty> ExposedProperty = Preset->ResolveExposedProperty(*Args.FieldLabel);
	TOptional<FRemoteControlProperty> RemoteControlProperty = Preset->GetProperty(*Args.FieldLabel);

	if (!ExposedProperty.IsSet() || !RemoteControlProperty.IsSet())
	{
		Response->Code = EHttpServerResponseCodes::NotFound;
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Unable to resolve the preset field."), Response->Body);
		OnComplete(MoveTemp(Response));
		return true;
	}

	TArray<uint8> WorkingBuffer;
	FMemoryWriter Writer(WorkingBuffer);
	TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);

	FRCObjectReference ObjectRef;

	bool bSuccess = true;

	JsonWriter->WriteObjectStart();
	JsonWriter->WriteIdentifierPrefix("PropertyValues");
	JsonWriter->WriteArrayStart();

	for (UObject* Object : ExposedProperty->OwnerObjects)
	{
		IRemoteControlModule::Get().ResolveObjectProperty(ERCAccess::READ_ACCESS, Object, RemoteControlProperty->FieldPathInfo.ToString(), ObjectRef);

		JsonWriter->WriteObjectStart();
		JsonWriter->WriteValue(TEXT("ObjectPath"), Object->GetPathName());
		JsonWriter->WriteIdentifierPrefix(TEXT("PropertyValue"));

		bSuccess &= RemotePayloadSerializer::SerializePartial(
			[&ObjectRef] (FJsonStructSerializerBackend& SerializerBackend)
			{
				return IRemoteControlModule::Get().GetObjectProperties(ObjectRef, SerializerBackend);
			}
			, Writer);

		JsonWriter->WriteObjectEnd();
	}

	JsonWriter->WriteArrayEnd();
	JsonWriter->WriteObjectEnd();

	if (bSuccess)
	{
		Response->Code = EHttpServerResponseCodes::Ok;
		WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Response->Body);
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Error while trying to read property %s."), *Args.FieldLabel), Response->Body);
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleGetPresetRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FString PresetName = Request.PathParams.FindChecked(TEXT("preset"));

	if (URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(FName(*PresetName)))
	{
		WebRemoteControlUtils::SerializeResponse(FGetPresetResponse{ Preset }, Response->Body);
		Response->Code = EHttpServerResponseCodes::Ok;
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Preset %s could not be found."), *PresetName), Response->Body);
		Response->Code = EHttpServerResponseCodes::NotFound;
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleGetPresetsRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes::Ok);

	TArray<TSoftObjectPtr<URemoteControlPreset>> Presets;
	IRemoteControlModule::Get().GetPresets(Presets);

	WebRemoteControlUtils::SerializeResponse(FListPresetsResponse{ Presets }, Response->Body);

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleDescribeObjectRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FDescribeObjectRequest DescribeRequest;
	if (!WebRemoteControlUtils::DeserializeRequest(Request, &OnComplete, DescribeRequest))
	{
		return true;
	}

	FRCObjectReference Ref;
	FString ErrorText;
	if (IRemoteControlModule::Get().ResolveObject(ERCAccess::READ_ACCESS, DescribeRequest.ObjectPath, TEXT(""), Ref, &ErrorText) && Ref.Object.IsValid())
	{
		WebRemoteControlUtils::SerializeResponse(FDescribeObjectResponse{ Ref.Object.Get() }, Response->Body);
		Response->Code = EHttpServerResponseCodes::Ok;
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(*ErrorText, Response->Body);
		Response->Code = EHttpServerResponseCodes::NotFound;
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleSearchActorRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes::NotSupported);
	WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Route not implemented."), Response->Body);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleSearchAssetRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();
	FSearchAssetRequest SearchAssetRequest;
	if (!WebRemoteControlUtils::DeserializeRequest(Request, &OnComplete, SearchAssetRequest))
	{
		return true;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	FARFilter Filter = SearchAssetRequest.Filter.ToARFilter();

	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	TArrayView<FAssetData> AssetsView{Assets};
	int32 ArrayEnd = FMath::Min(SearchAssetRequest.Limit, Assets.Num());

	TArray<FAssetData> FilteredAssets;
	FilteredAssets.Reserve(SearchAssetRequest.Limit);

	for (const FAssetData& AssetData : Assets)
	{	
		if (!SearchAssetRequest.Query.IsEmpty())
		{
			if (AssetData.AssetName.ToString().Contains(*SearchAssetRequest.Query))
			{
				FilteredAssets.Add(AssetData);
			}
		}
		else
		{
			FilteredAssets.Add(AssetData);
		}

		if (FilteredAssets.Num() >= SearchAssetRequest.Limit)
		{
			break;
		}
	}

	WebRemoteControlUtils::SerializeResponse(FSearchAssetResponse{ MoveTemp(FilteredAssets) }, Response->Body);
	Response->Code = EHttpServerResponseCodes::Ok;

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleGetMetadataRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FString PresetName = Request.PathParams.FindChecked(TEXT("preset"));

	if (URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(FName(*PresetName)))
	{
		WebRemoteControlUtils::SerializeResponse(FGetMetadataResponse{Preset->Metadata}, Response->Body);
		Response->Code = EHttpServerResponseCodes::Ok;
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Preset %s could not be found."), *PresetName), Response->Body);
		Response->Code = EHttpServerResponseCodes::NotFound;
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleMetadataFieldOperationsRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();

	FString PresetName = Request.PathParams.FindChecked(TEXT("preset"));
	FString MetadataField = Request.PathParams.FindChecked(TEXT("metadatafield"));

	if (URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(FName(*PresetName)))
	{
		if (Request.Verb == EHttpServerRequestVerbs::VERB_GET)
		{
			if (FString* MetadataValue = Preset->Metadata.Find(MetadataField))
			{
				WebRemoteControlUtils::SerializeResponse(FGetMetadataFieldResponse{ *MetadataValue }, Response->Body);
			}
			else
			{
				WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Metadata field %s could not be found."), *MetadataField), Response->Body);
				Response->Code = EHttpServerResponseCodes::NotFound;
			}
		}
		else if (Request.Verb == EHttpServerRequestVerbs::VERB_PUT)
		{
			FSetPresetMetadataRequest SetMetadataRequest;
			if (!WebRemoteControlUtils::DeserializeRequest(Request, &OnComplete, SetMetadataRequest))
			{
				return true;
			}
			
			FString& MetadataValue = Preset->Metadata.FindOrAdd(MoveTemp(MetadataField));
			MetadataValue = MoveTemp(SetMetadataRequest.Value);
		}
		else if (Request.Verb == EHttpServerRequestVerbs::VERB_DELETE)
		{
			Preset->Metadata.Remove(MoveTemp(MetadataField));
		}

		Response->Code = EHttpServerResponseCodes::Ok;
	}
	else
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Preset %s could not be found."), *PresetName), Response->Body);
		Response->Code = EHttpServerResponseCodes::NotFound;
	}

	OnComplete(MoveTemp(Response));
	return true;
}

bool FWebRemoteControlModule::HandleSearchObjectRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes::NotSupported);
	WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("Route not implemented."), Response->Body);
	OnComplete(MoveTemp(Response));
	return true;
}

void FWebRemoteControlModule::HandleWebSocketHttpMessage(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	TArray<uint8> UTF8Response;

	//Early failure is http server not started
	if (HttpRouter == nullptr)
	{
		WebRemoteControlUtils::CreateUTF8ErrorMessage(TEXT("HTTP server not started."), UTF8Response);
		WebSocketServer.Send(WebSocketMessage.ClientId, MoveTemp(UTF8Response));
		return;
	}

	FRCRequestWrapper Wrapper;
	if (!WebRemoteControlUtils::DeserializeWrappedRequestPayload(WebSocketMessage.RequestPayload, nullptr, Wrapper))
	{
		return;
	}

	FMemoryWriter Writer(UTF8Response);
	InvokeWrappedRequest(Wrapper, Writer);

	WebSocketServer.Send(WebSocketMessage.ClientId, MoveTemp(UTF8Response));
}

void FWebRemoteControlModule::InvokeWrappedRequest(const FRCRequestWrapper& Wrapper, FMemoryWriter& OutUTF8PayloadWriter, const FHttpServerRequest* TemplateRequest)
{
	TSharedRef<FHttpServerRequest> UnwrappedRequest = RemotePayloadSerializer::UnwrapHttpRequest(Wrapper, TemplateRequest);

	auto ResponseLambda = [this, &OutUTF8PayloadWriter, &Wrapper](TUniquePtr<FHttpServerResponse> Response) {
		RemotePayloadSerializer::SerializeWrappedCallResponse(Wrapper.RequestId, MoveTemp(Response), OutUTF8PayloadWriter);
	};

	if (!HttpRouter->Query(UnwrappedRequest, ResponseLambda))
	{
		TUniquePtr<FHttpServerResponse> InnerRouteResponse = WebRemoteControlUtils::CreateHttpResponse(EHttpServerResponseCodes::NotFound);
		WebRemoteControlUtils::CreateUTF8ErrorMessage(FString::Printf(TEXT("Route \"%s\" could not be found."), *Wrapper.URL), InnerRouteResponse->Body);
		ResponseLambda(MoveTemp(InnerRouteResponse));
	}
}

#if WITH_EDITOR
void FWebRemoteControlModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		TSharedPtr<ISettingsSection> SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "WebRemoteControl",
			LOCTEXT("RemoteControlSettingsName", "Web Remote Control"),
			LOCTEXT("RemoteControlSettingsDescription", "Configure the Web Remote Control settings."),
			GetMutableDefault<UWebRemoteControlSettings>());

		SettingsSection->OnModified().BindRaw(this, &FWebRemoteControlModule::OnSettingsModified);
	}
}

void FWebRemoteControlModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "WebRemoteControl");
	}
}

bool FWebRemoteControlModule::OnSettingsModified()
{
	const UWebRemoteControlSettings* Settings = GetDefault<UWebRemoteControlSettings>();
	const bool bIsWebServerStarted = HttpRouter.IsValid();
	const bool bIsWebSocketServerStarted = WebSocketServer.IsRunning();
	const bool bRestartHttpServer = Settings->RemoteControlHttpServerPort != HttpServerPort;
	const bool bRestartWebSocketServer = Settings->RemoteControlWebSocketServerPort != WebSocketServerPort;

	if ((bIsWebServerStarted && bRestartHttpServer)
		|| (!bIsWebServerStarted && Settings->bAutoStartWebServer))
	{
		HttpServerPort = Settings->RemoteControlHttpServerPort;
		StopHttpServer();
		StartHttpServer();
	}

	if ((bIsWebSocketServerStarted && bRestartWebSocketServer)
		|| (!bIsWebSocketServerStarted && Settings->bAutoStartWebSocketServer))
	{
		WebSocketServerPort = Settings->RemoteControlWebSocketServerPort;
		StopWebSocketServer();
		StartWebSocketServer();
	}

	return true;
}

#endif

#undef LOCTEXT_NAMESPACE /* WebRemoteControl */

IMPLEMENT_MODULE(FWebRemoteControlModule, WebRemoteControl);
