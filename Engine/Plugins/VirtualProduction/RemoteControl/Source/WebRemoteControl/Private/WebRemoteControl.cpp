// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebRemoteControl.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "IRemoteControlModule.h"
#include "WebRemoteControlUtils.h"
#include "RemoteControlRoute.h"
#include "WebRemoteControlSettings.h"
#include "RemoteControlRequest.h"

#if WITH_EDITOR
// Settings
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif

#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

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

#define LOCTEXT_NAMESPACE "WebRemoteControl"

namespace RemotePayloadSerializer
{
	typedef UCS2CHAR PayloadCharType;
	const FName ReturnValuePropName(TEXT("ReturnValue"));

	bool DeserializeCall(const FHttpServerRequest& InRequest, FRCCall& OutCall, const FHttpResultCallback& InCompleteCallback)
	{
		// Create Json reader to read the payload, the payload will already be validated as being in TCHAR
		TArray<uint8> WorkingBuffer;
		WebRemoteControlUtils::ConvertToTCHAR(InRequest.Body, WorkingBuffer);

		FRCCallRequest CallRequest;
		if (!WebRemoteControlUtils::DeserializeRequest(InRequest, &InCompleteCallback, CallRequest))
		{
			return false;
		}

		FString ErrorText;
		bool bSuccess = true;
		if (IRemoteControlModule::Get().ResolveCall(CallRequest.ObjectPath, CallRequest.FunctionName, OutCall.CallRef, &ErrorText))
		{
			// Initialize the param struct with default parameters
			OutCall.bGenerateTransaction = CallRequest.GenerateTransaction;
			OutCall.ParamStruct = FStructOnScope(OutCall.CallRef.Function.Get());

			// If some parameters were provided, deserialize them
			const FBlockDelimiters& ParametersDelimiters = CallRequest.GetStructParameters().FindChecked(FRCCallRequest::ParametersLabel());
			if (ParametersDelimiters.BlockStart > 0)
			{
				FMemoryReader Reader(WorkingBuffer);
				Reader.Seek(ParametersDelimiters.BlockStart);
				Reader.SetLimitSize(ParametersDelimiters.BlockEnd + 1);

				FJsonStructDeserializerBackend Backend(Reader);
				if (!FStructDeserializer::Deserialize((void*)OutCall.ParamStruct.GetStructMemory(), *const_cast<UStruct*>(OutCall.ParamStruct.GetStruct()), Backend, FStructDeserializerPolicies()))
				{
					ErrorText = TEXT("Parameters object improperly formatted.");
					bSuccess = false;
				}
			}
		}

		if (!ErrorText.IsEmpty())
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Call deserialization error: %s"), *ErrorText);
			TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();
			WebRemoteControlUtils::CreateUTF8ErrorMessage(ErrorText, Response->Body);
			InCompleteCallback(MoveTemp(Response));
		}

		return bSuccess;
	}

	bool SerializeCall(const FRCCall& InCall, TArray<uint8>& OutPayload, bool bOnlyReturn = false)
	{
		FMemoryWriter Writer(OutPayload);
		TSharedPtr<TJsonWriter<PayloadCharType>> JsonWriter;

		if (!bOnlyReturn)
		{
			// Create Json writer to write the payload, if we do not just write the return value back.
			JsonWriter = TJsonWriter<PayloadCharType>::Create(&Writer);

			JsonWriter->WriteObjectStart();
			JsonWriter->WriteValue(TEXT("ObjectPath"), InCall.CallRef.Object->GetPathName());
			JsonWriter->WriteValue(TEXT("FunctionName"), InCall.CallRef.Function->GetFName().ToString());
			JsonWriter->WriteIdentifierPrefix(TEXT("Parameters"));
		}

		// write the param struct
		FJsonStructSerializerBackend Backend(Writer, EStructSerializerBackendFlags::Default);
		FStructSerializerPolicies Policies;

		if (bOnlyReturn)
		{
			Policies.PropertyFilter = [](const FProperty* CurrentProp, const FProperty* ParentProperty) -> bool
			{
				return CurrentProp->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm) || ParentProperty != nullptr;
			};
		}

		FStructSerializer::Serialize((void*)InCall.ParamStruct.GetStructMemory(), *const_cast<UStruct*>(InCall.ParamStruct.GetStruct()), Backend, Policies);

		if (!bOnlyReturn)
		{
			JsonWriter->WriteObjectEnd();
		}

		return true;
	}

	bool DeserializeObjectRef(const FHttpServerRequest& InRequest, FRCObjectReference& OutObjectRef, FRCObjectRequest& OutDeserializedRequest, const FHttpResultCallback& InCompleteCallback)
	{
		TArray<uint8> WorkingBuffer;
		WebRemoteControlUtils::ConvertToTCHAR(InRequest.Body, WorkingBuffer);

		if (!WebRemoteControlUtils::DeserializeRequest(InRequest, &InCompleteCallback, OutDeserializedRequest))
		{
			return false;
		}

		FString ErrorText;

		// If we properly identified the object path, property name and access type as well as identified the starting / end position for property value
		// resolve the object reference
		IRemoteControlModule::Get().ResolveObject(OutDeserializedRequest.GetAccessValue(), OutDeserializedRequest.ObjectPath, OutDeserializedRequest.PropertyName, OutObjectRef, &ErrorText);

		if (!ErrorText.IsEmpty())
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Object Access error: %s"), *ErrorText);
			TUniquePtr<FHttpServerResponse> Response = WebRemoteControlUtils::CreateHttpResponse();
			WebRemoteControlUtils::CreateUTF8ErrorMessage(ErrorText, Response->Body);
			InCompleteCallback(MoveTemp(Response));
			return false;
		}

		return true;
	}
}

// Boot the server on startup flag
static TAutoConsoleVariable<int32> CVarWebControlStartOnBoot(TEXT("WebControl.EnableServerOnStartup"), 0, TEXT("Enable the Web Control server on startup."));

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

	RegisterConsoleCommands();
	RegisterRoutes();

	if (CVarWebControlStartOnBoot.GetValueOnAnyThread() > 0)
	{
		StartHttpServer();
	}
}

void FWebRemoteControlModule::ShutdownModule()
{
	EditorRoutes.UnregisterRoutes(this);
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
}

void FWebRemoteControlModule::StartWebSocketServer()
{
	if (!WebSocketServer.IsRunning())
	{
		if (!WebSocketServer.Start(WebSocketServerPort, WebSocketRouter))
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Call WebSocket server couldn't be started on port %d"), WebSocketServerPort);
			return;
		}
	}
}

void FWebRemoteControlModule::StopWebSocketServer()
{
	WebSocketServer.Stop();
}

void FWebRemoteControlModule::StartRoute(const FRemoteControlRoute& Route)
{
	// The handler is wrapped in a lambda since HttpRouter::BindRoute only accepts TFunctions
	ActiveRouteHandles.Add(GetTypeHash(Route), HttpRouter->BindRoute(Route.Path, Route.Verb, [this, Handler = Route.Handler](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) { return Handler.Execute(Request, OnComplete); }));
}

void FWebRemoteControlModule::RegisterRoutes()
{
	RegisterRoute({
		TEXT("Get information about different routes available on this API."),
		FHttpPath(TEXT("/remote/info")),
		EHttpServerRequestVerbs::VERB_GET,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleInfoRoute)
		});

	RegisterRoute({
		TEXT("Route used to allow cross-origin http requests to the API."),
		FHttpPath(TEXT("/remote")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FRequestHandlerDelegate::CreateRaw(this, &FWebRemoteControlModule::HandleOptionsRoute)
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

	TArray<uint8> WorkingBuffer;
	WebRemoteControlUtils::ConvertToTCHAR(Request.Body, WorkingBuffer);

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
		WorkingBuffer.Empty();
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
			FMemoryReader Reader(WorkingBuffer);
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
	if (Settings->RemoteControlHttpServerPort != HttpServerPort)
	{
		HttpServerPort = Settings->RemoteControlHttpServerPort;
		StopHttpServer();
		StartHttpServer();
	}

	if (Settings->RemoteControlWebSocketServerPort != WebSocketServerPort)
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
