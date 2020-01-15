// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "IRemoteControlModule.h"
#include "RemoteControlMessages.h"

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
#include "HttpServerResponse.h"
#include "HttpRequestHandler.h"
#include "HttpServerConstants.h"

// Commands
#include "HAL/IConsoleManager.h"


namespace RemotePayloadSerializer
{
	typedef UCS2CHAR PayloadCharType;
	const FName ReturnValuePropName(TEXT("ReturnValue"));

	void ConvertToTCHAR(const TArray<uint8>& InUTF8Payload, TArray<uint8>& OutTCHARPayload)
	{
		int32 StartIndex = OutTCHARPayload.Num();
		OutTCHARPayload.AddUninitialized(FUTF8ToTCHAR_Convert::ConvertedLength((ANSICHAR*)InUTF8Payload.GetData(), InUTF8Payload.Num() / sizeof(ANSICHAR)) * sizeof(TCHAR));
		FUTF8ToTCHAR_Convert::Convert((TCHAR*)(OutTCHARPayload.GetData() + StartIndex), (OutTCHARPayload.Num() - StartIndex) / sizeof(TCHAR), (ANSICHAR*)InUTF8Payload.GetData(), InUTF8Payload.Num() / sizeof(ANSICHAR));
	}

	void ConvertToUTF8(const TArray<uint8>& InTCHARPayload, TArray<uint8>& OutUTF8Payload)
	{
		int32 StartIndex = OutUTF8Payload.Num();
		OutUTF8Payload.AddUninitialized(FTCHARToUTF8_Convert::ConvertedLength((TCHAR*)InTCHARPayload.GetData(), InTCHARPayload.Num() / sizeof(TCHAR)) * sizeof(ANSICHAR));
		FTCHARToUTF8_Convert::Convert((ANSICHAR*)(OutUTF8Payload.GetData() + StartIndex), (OutUTF8Payload.Num() - StartIndex) / sizeof(ANSICHAR), (TCHAR*)InTCHARPayload.GetData(), InTCHARPayload.Num() / sizeof(TCHAR));
	}

	bool DeserializeCall(const TArray<uint8>& InTCHARPayload, FRCCall& OutCall)
	{
		// Create Json reader to read the payload, the payload will already be validated as being in TCHAR
		FMemoryReader Reader(InTCHARPayload);
		TSharedRef<TJsonReader<PayloadCharType>> JsonReader = TJsonReader<PayloadCharType>::Create(&Reader);

		FString ErrorText;
		EJsonNotation Notation;

		// The payload should be an object
		JsonReader->ReadNext(Notation);
		if (Notation != EJsonNotation::ObjectStart)
		{
			ErrorText = TEXT("Expected json object.");
			return false;
		}

		// Mark the start/end of the param object in the payload
		int64 ParamBlockStart = -1;
		int64 ParamBlockEnd = -1;
		FString ObjectPath;
		FString FunctionName;
		bool bGenerateTransaction = false;

		while (JsonReader->ReadNext(Notation) && ErrorText.IsEmpty())
		{
			switch (Notation)
			{
				// this should mean we reached the parameters field, record the start and ending offset
				case EJsonNotation::ObjectStart:
					if (JsonReader->GetIdentifier() == TEXT("Parameters"))
					{
						ParamBlockStart = Reader.Tell() - sizeof(PayloadCharType);
						if (JsonReader->SkipObject())
						{
							ParamBlockEnd = Reader.Tell();
						}
						else
						{
							ErrorText = TEXT("parameters object improperly formatted.");
						}
					}
					else
					{
						ErrorText = TEXT("unexpected object field.");
					}
					break;
				// this means we should be done with the request object
				case EJsonNotation::ObjectEnd:
					break;
				// boolean property to wrap the function in a transaction
				case EJsonNotation::Boolean:
					if (JsonReader->GetIdentifier() == TEXT("GenerateTransaction"))
					{
						bGenerateTransaction = JsonReader->GetValueAsBoolean();
					}
					else
					{
						ErrorText = TEXT("unexpected boolean field.");
					}
					break;
				// any other request field should be string properties
				case EJsonNotation::String:
					if (JsonReader->GetIdentifier() == TEXT("ObjectPath"))
					{
						ObjectPath = JsonReader->GetValueAsString();
					}
					else if (JsonReader->GetIdentifier() == TEXT("FunctionName"))
					{
						FunctionName = JsonReader->GetValueAsString();
					}
					else
					{
						ErrorText = TEXT("unexpected string field.");
					}
					break;
				// if we encounter any parse error we abort
				case EJsonNotation::Error:
					ErrorText = JsonReader->GetErrorMessage();
					break;
				// we ignore any other fields
				default:
					ErrorText = TEXT("unexpected field.");
					break;
			}
		}

		bool bSuccess = ErrorText.IsEmpty();
		// if we properly identified the object path and function name and properly resolved the call
		if (bSuccess && IRemoteControlModule::Get().ResolveCall(ObjectPath, FunctionName, OutCall.CallRef, &ErrorText))
		{
			// Initialize the param struct with default parameters
			OutCall.bGenerateTransaction = bGenerateTransaction;
			OutCall.ParamStruct = FStructOnScope(OutCall.CallRef.Function.Get());

			// if some parameters were provided, deserialize them
			if (ParamBlockStart > 0)
			{
				Reader.Seek(ParamBlockStart);
				Reader.SetLimitSize(ParamBlockEnd + 1);

				FJsonStructDeserializerBackend Backend(Reader);
				if (!FStructDeserializer::Deserialize((void*)OutCall.ParamStruct.GetStructMemory(), *const_cast<UStruct*>(OutCall.ParamStruct.GetStruct()), Backend, FStructDeserializerPolicies()))
				{
					ErrorText = TEXT("Parameters object improperly formatted.");
					bSuccess = false;
				}
			}
		}

		// Print deserialization or resolving error, having resolving error is still considered successful 
		if (!ErrorText.IsEmpty())
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Call deserialization error: %s"), *ErrorText);
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
				return CurrentProp->HasAnyPropertyFlags(CPF_ReturnParm|CPF_OutParm) || ParentProperty != nullptr;
			};
		}

		FStructSerializer::Serialize((void*)InCall.ParamStruct.GetStructMemory(), *const_cast<UStruct*>(InCall.ParamStruct.GetStruct()), Backend, Policies);
		
		if (!bOnlyReturn)
		{
			JsonWriter->WriteObjectEnd();
		}
		
		return true;
	}

	bool DeserializeObjectRef(TArray<uint8>& InTCHARPayload, FRCObjectReference& OutObjectRef, bool& OutResetToDefault, TPair<int32, int32>& OutPropertyBlockRange)
	{
		FMemoryReader Reader(InTCHARPayload);
		TSharedRef<TJsonReader<PayloadCharType>> JsonReader = TJsonReader<PayloadCharType>::Create(&Reader);

		FString ErrorText;
		EJsonNotation Notation;

		// The payload should be an object
		JsonReader->ReadNext(Notation);
		if (Notation != EJsonNotation::ObjectStart)
		{
			ErrorText = TEXT("Expected json object.");
			return false;
		}

		// Mark the start/end of the param object in the payload
		FString ObjectPath;
		FString PropertyName;
		ERCAccess AccessValue = ERCAccess::NO_ACCESS;
		auto GetAccessValue = [](const FString& AccessStr)
		{
			ERCAccess Access = ERCAccess::NO_ACCESS;
			if (FCString::Stricmp(*AccessStr, TEXT("READ_ACCESS")) == 0)
			{
				Access = ERCAccess::READ_ACCESS;
			}
			else if (FCString::Stricmp(*AccessStr, TEXT("WRITE_ACCESS")) == 0)
			{
				Access = ERCAccess::WRITE_ACCESS;
			}
			else if (FCString::Stricmp(*AccessStr, TEXT("WRITE_TRANSACTION_ACCESS")) == 0)
			{
				Access = ERCAccess::WRITE_TRANSACTION_ACCESS;
			}
			return Access;
		};

		while (JsonReader->ReadNext(Notation) && ErrorText.IsEmpty())
		{
			switch (Notation)
			{
				// this should mean we reached the parameters field, record the start and ending offset
			case EJsonNotation::ObjectStart:
				if (JsonReader->GetIdentifier() == TEXT("PropertyValue"))
				{
					OutPropertyBlockRange.Key = Reader.Tell() - sizeof(PayloadCharType);
					if (JsonReader->SkipObject())
					{
						OutPropertyBlockRange.Value = Reader.Tell();
					}
					else
					{
						ErrorText = TEXT("property value object improperly formatted.");
					}
				}
				else
				{
					ErrorText = TEXT("unexpected object field.");
				}
				break;
				// this means we should be done with the request object
			case EJsonNotation::ObjectEnd:
				break;
				// read the reset to default property
			case EJsonNotation::Boolean:
				if (JsonReader->GetIdentifier() == TEXT("ResetToDefault"))
				{
					OutResetToDefault = JsonReader->GetValueAsBoolean();
				}
				else
				{
					ErrorText = TEXT("unexpected boolean field.");
				}
				break;
				// any other request field should be string properties
			case EJsonNotation::String:
				if (JsonReader->GetIdentifier() == TEXT("ObjectPath"))
				{
					ObjectPath = JsonReader->GetValueAsString();
				}
				else if (JsonReader->GetIdentifier() == TEXT("PropertyName"))
				{
					PropertyName = JsonReader->GetValueAsString();
				}
				else if (JsonReader->GetIdentifier() == TEXT("Access"))
				{
					AccessValue = GetAccessValue(JsonReader->GetValueAsString());
				}
				else
				{
					ErrorText = TEXT("unexpected string field.");
				}
				break;
				// if we encounter any parse error we abort
			case EJsonNotation::Error:
				ErrorText = JsonReader->GetErrorMessage();
				break;
				// we ignore any other fields
			default:
				ErrorText = TEXT("unexpected field.");
				break;
			}
		}

		bool bSuccess = ErrorText.IsEmpty();
		if (bSuccess)
		{
			// if we properly identified the object path, property name and access type as well as identified the starting / end position for property value
			// resolve the object reference
			IRemoteControlModule::Get().ResolveObject(AccessValue, ObjectPath, PropertyName, OutObjectRef, &ErrorText);
		}

		// Print deserialization or resolving error, having resolving error is still considered successful 
		if (!ErrorText.IsEmpty())
		{
			UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Object Access deserialization error: %s"), *ErrorText);
		}

		return bSuccess;
	}
}

// Boot the server on startup flag
static TAutoConsoleVariable<int32> CVarWebControlStartOnBoot(TEXT("WebControl.EnableServerOnStartup"), 0, TEXT("Enable the Web Control server on startup."));

// Enable experimental remote routes
static TAutoConsoleVariable<int32> CVarWebControlEnableExperimentalRoutes(TEXT("WebControl.EnableExperimentalRoutes"), 0, TEXT("Enable the Web Control server experimental routes."));

/**
 * A Remote Control module that expose remote function calls through http
 */
class FWebRemoteControlModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Setup console command
		StartServerCommand = MakeUnique<FAutoConsoleCommand>(
			TEXT("WebControl.StartServer"),
			TEXT("Start the remote control web server"),
			FConsoleCommandDelegate::CreateRaw(this, &FWebRemoteControlModule::StartServer)
			);
		StopServerCommand = MakeUnique<FAutoConsoleCommand>(
			TEXT("WebControl.StopServer"),
			TEXT("Stop the remote control web server"),
			FConsoleCommandDelegate::CreateRaw(this, &FWebRemoteControlModule::StopServer)
			);

		if (CVarWebControlStartOnBoot.GetValueOnAnyThread() > 0)
		{
			StartServer();
		}

		EventDispatchers.AddDefaulted((int32)ERemoteControlEvent::EventCount);
	}

	virtual void ShutdownModule() override
	{
		StopServer();
		StartServerCommand.Reset();
		StopServerCommand.Reset();
	}

	/** 
	 * Start the web control server 
	 */
	void StartServer()
	{
		if (!HttpRouter)
		{
			// Start a route
			HttpRouter = FHttpServerModule::Get().GetHttpRouter(8080);
			if (!HttpRouter)
			{
				UE_LOG(LogRemoteControl, Error, TEXT("Web Remote Call server couldn't be started on port %d"), 8080);
			}

			RemoteRouteOptionsHandle = HttpRouter->BindRoute(FHttpPath(TEXT("/remote")), EHttpServerRequestVerbs::VERB_OPTIONS,
				[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
				Response->Code = EHttpServerResponseCodes::Ok;
				AddCORSHeaders(Response.Get());
				OnComplete(MoveTemp(Response));
				return true;
			});

			RemoteCallRouteHandle = HttpRouter->BindRoute(FHttpPath(TEXT("/remote/object/call")), EHttpServerRequestVerbs::VERB_PUT,
				[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
				AddCORSHeaders(Response.Get());
				AddContentTypeHeaders(Response.Get(), TEXT("application/json"));

				// Initialize the request as a bad request
				Response->Code = EHttpServerResponseCodes::BadRequest;

				if (!IsRequestContentType(Request, TEXT("application/json")))
				{
					OnComplete(MoveTemp(Response));
					return true;
				}
		
				TArray<uint8> WorkingBuffer;
				RemotePayloadSerializer::ConvertToTCHAR(Request.Body, WorkingBuffer);

				FRCCall Call;
				if (!RemotePayloadSerializer::DeserializeCall(WorkingBuffer, Call))
				{
					OnComplete(MoveTemp(Response));
					return true;
				}

				// if we haven't resolve the object or function, return not found
				if (!Call.IsValid())
				{
					Response->Code = EHttpServerResponseCodes::NotFound;
					OnComplete(MoveTemp(Response));
					return true;
				}

				IRemoteControlModule::Get().InvokeCall(Call);

				WorkingBuffer.Empty();
				if (!RemotePayloadSerializer::SerializeCall(Call, WorkingBuffer, true))
				{
					Response->Code = EHttpServerResponseCodes::ServerError;
				}
				else
				{
					RemotePayloadSerializer::ConvertToUTF8(WorkingBuffer, Response->Body);
					Response->Code = EHttpServerResponseCodes::Ok;
				}

				OnComplete(MoveTemp(Response));
				return true;
			});

			RemotePropertyRouteHandle = HttpRouter->BindRoute(FHttpPath(TEXT("/remote/object/property")), EHttpServerRequestVerbs::VERB_PUT,
				[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
				AddCORSHeaders(Response.Get());
				AddContentTypeHeaders(Response.Get(), TEXT("application/json"));

				// Initialize the request as a bad request
				Response->Code = EHttpServerResponseCodes::BadRequest;

				if (!IsRequestContentType(Request, TEXT("application/json")))
				{
					OnComplete(MoveTemp(Response));
					return true;
				}

				TArray<uint8> WorkingBuffer;
				RemotePayloadSerializer::ConvertToTCHAR(Request.Body, WorkingBuffer);

				
				FRCObjectReference ObjectRef;
				bool bResetToDefault = false;
				TPair<int32, int32> PropertyBlockRange;
				if (!RemotePayloadSerializer::DeserializeObjectRef(WorkingBuffer, ObjectRef, bResetToDefault, PropertyBlockRange))
				{
					OnComplete(MoveTemp(Response));
					return true;
				}

				// if we haven't found the object, return a not found error code
				if (!ObjectRef.IsValid())
				{
					Response->Code = EHttpServerResponseCodes::NotFound;
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
						RemotePayloadSerializer::ConvertToUTF8(WorkingBuffer, Response->Body);
					}
				}
					break;
				case ERCAccess::WRITE_ACCESS:
				case ERCAccess::WRITE_TRANSACTION_ACCESS:
				{
					if (bResetToDefault)
					{
						if (IRemoteControlModule::Get().ResetObjectProperties(ObjectRef))
						{
							Response->Code = EHttpServerResponseCodes::Ok;
						}
					}
					else if (PropertyBlockRange.Key > 0)
					{
						FMemoryReader Reader(WorkingBuffer);
						Reader.Seek(PropertyBlockRange.Key);
						Reader.SetLimitSize(PropertyBlockRange.Value + 1);
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
			});
			auto EventRouteLambda = [this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				TUniquePtr<FHttpServerResponse> Response = MakeUnique<FHttpServerResponse>();
				AddCORSHeaders(Response.Get());
				AddContentTypeHeaders(Response.Get(), TEXT("application/json"));

				if (!IsRequestContentType(Request, TEXT("application/json")))
				{
					Response->Code = EHttpServerResponseCodes::BadRequest;
					OnComplete(MoveTemp(Response));
					return true;
				}

				TArray<uint8> WorkingBuffer;
				RemotePayloadSerializer::ConvertToTCHAR(Request.Body, WorkingBuffer);

				// deserialize event request
				FRemoteControlObjectEventHookRequest EventRequest;
				FMemoryReader Reader(WorkingBuffer);
				FJsonStructDeserializerBackend DeserializerBackend(Reader);
				if (!FStructDeserializer::Deserialize((void*)&EventRequest, *FRemoteControlObjectEventHookRequest::StaticStruct(), DeserializerBackend, FStructDeserializerPolicies()))
				{
					Response->Code = EHttpServerResponseCodes::BadRequest;
					OnComplete(MoveTemp(Response));
					return true;
				}

				// if we have a valid event hook request, queue it and complete the event when it triggers
				AddPendingEvent(MoveTemp(EventRequest), MoveTemp(Response), OnComplete);
				return true;
			};

			// Only expose the event route if we have experimental routes enabled.
			if (CVarWebControlEnableExperimentalRoutes.GetValueOnAnyThread() > 0)
			{
				RemoteEventRouteHandle = HttpRouter->BindRoute(FHttpPath(TEXT("/remote/object/event")), EHttpServerRequestVerbs::VERB_PUT, EventRouteLambda);
			}
			
			FHttpServerModule::Get().StartAllListeners();
		}
	}

	/** 
	 * Stop the web control server.
	 */
	void StopServer()
	{
		if (HttpRouter)
		{
			if (RemoteEventRouteHandle)
			{
				HttpRouter->UnbindRoute(RemoteEventRouteHandle);
				RemoteEventRouteHandle.Reset();
			}
			if (RemotePropertyRouteHandle)
			{
				HttpRouter->UnbindRoute(RemotePropertyRouteHandle);
				RemotePropertyRouteHandle.Reset();
			}
			if (RemoteCallRouteHandle)
			{
				HttpRouter->UnbindRoute(RemoteCallRouteHandle);
				RemoteCallRouteHandle.Reset();
			}
			if (RemoteRouteOptionsHandle)
			{
				HttpRouter->UnbindRoute(RemoteRouteOptionsHandle);
				RemoteRouteOptionsHandle.Reset();
			}
			
		}
		HttpRouter.Reset();

		if (FHttpServerModule::IsAvailable())
		{
			FHttpServerModule::Get().StopAllListeners();
		}
	}

private:
	bool IsRequestContentType(const FHttpServerRequest& Request, const FString& ContentType)
	{
		if (const TArray<FString>* ContentTypeHeaders = Request.Headers.Find(TEXT("Content-Type")))
		{
			return ContentTypeHeaders->Num() > 0 && (*ContentTypeHeaders)[0] == ContentType;
		}
		return false;
	}

	void AddContentTypeHeaders(FHttpServerResponse* Response, FString ContentType)
	{
		Response->Headers.Add(TEXT("content-type"), { MoveTemp(ContentType) });
	}

	void AddCORSHeaders(FHttpServerResponse* Response)
	{
		check(Response != nullptr);
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("PUT, POST, GET, OPTIONS") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Origin, X-Requested-With, Content-Type, Accept") });
		Response->Headers.Add(TEXT("Access-Control-Max-Age"), { TEXT("600") });
	}

	struct FRemoteEventHook
	{
		FRemoteEventHook(FRCObjectReference InObjectRef, TUniquePtr<FHttpServerResponse> InResponse, FHttpResultCallback InCompleteCallback)
			: ObjectRef(MoveTemp(InObjectRef))
			, Response(MoveTemp(InResponse))
			, CompleteCallback(MoveTemp(InCompleteCallback))
		{}

		FRCObjectReference ObjectRef;
		TUniquePtr<FHttpServerResponse> Response;
		FHttpResultCallback CompleteCallback;
	};

	struct FRemoteEventDispatcher
	{
		FRemoteEventDispatcher()
			: DispatcherType(ERemoteControlEvent::EventCount)
		{}

		~FRemoteEventDispatcher()
		{
			Reset();
		}

		bool IsValid() const
		{
			return DelegateHandle.IsValid();
		}

		void Initialize(ERemoteControlEvent Type)
		{
			Reset();
			DispatcherType = Type;
			switch (DispatcherType)
			{
			case ERemoteControlEvent::PreObjectPropertyChanged:
				DelegateHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda([this](UObject* InObject, const FEditPropertyChain& PropChain)
				{
					if (PropChain.GetActiveNode() && PropChain.GetActiveNode()->GetValue())
					{
						Dispatch(InObject, PropChain.GetActiveNode()->GetValue());
					}

				});
				break;
			case ERemoteControlEvent::ObjectPropertyChanged:
				DelegateHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda([this](UObject* InObject, const FPropertyChangedEvent& PropEvent)
				{
					if (PropEvent.MemberProperty)
					{
						Dispatch(InObject, PropEvent.MemberProperty);
					}

				});
				break;
			default:
				break;
			}
		}

		void Reset()
		{
			if (DelegateHandle.IsValid())
			{
				switch (DispatcherType)
				{
				case ERemoteControlEvent::PreObjectPropertyChanged:
					FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(DelegateHandle);
					break;
				case ERemoteControlEvent::ObjectPropertyChanged:
					FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(DelegateHandle);
					break;
				default:
					UE_LOG(LogRemoteControl, Fatal, TEXT("Unsupported remote event hook."));
					break;
				}
				DelegateHandle.Reset();
			}
		}

		void Dispatch(UObject* InObject, FProperty* InProperty)
		{
			for (auto It = PendingEvents.CreateIterator(); It; ++It)
			{
				if (It->ObjectRef.Object == InObject && It->ObjectRef.Property == InProperty)
				{
					SendResponse(*It);
					It.RemoveCurrent();
				}
			}
			if (PendingEvents.Num() == 0)
			{
				Reset();
			}
		}

		void SendResponse(FRemoteEventHook& EventHook)
		{
			TArray<uint8> WorkingBuffer;
			FMemoryWriter Writer(WorkingBuffer);
			FJsonStructSerializerBackend SerializerBackend(Writer, EStructSerializerBackendFlags::Default);

			if (IRemoteControlModule::Get().GetObjectProperties(EventHook.ObjectRef, SerializerBackend))
			{
				EventHook.Response->Code = EHttpServerResponseCodes::Ok;
				RemotePayloadSerializer::ConvertToUTF8(WorkingBuffer, EventHook.Response->Body);
			}
			else
			{
				EventHook.Response->Code = EHttpServerResponseCodes::NoContent;
			}
			
			EventHook.CompleteCallback(MoveTemp(EventHook.Response));
		}

		ERemoteControlEvent DispatcherType;
		FDelegateHandle DelegateHandle;
		TArray<FRemoteEventHook> PendingEvents;
	};

	void AddPendingEvent(FRemoteControlObjectEventHookRequest InRequest, TUniquePtr<FHttpServerResponse> InResponse, FHttpResultCallback OnComplete)
	{
		FRCObjectReference ObjectRef;
		if (IRemoteControlModule::Get().ResolveObject(ERCAccess::READ_ACCESS, InRequest.ObjectPath, InRequest.PropertyName, ObjectRef))
		{
			FRemoteEventDispatcher& EventDispatcher = EventDispatchers[(int32)InRequest.EventType];
			if (!EventDispatcher.IsValid())
			{
				EventDispatcher.Initialize(InRequest.EventType);
			}
			EventDispatcher.PendingEvents.Emplace(MoveTemp(ObjectRef), MoveTemp(InResponse), MoveTemp(OnComplete));
		}
		else
		{
			InResponse->Code = EHttpServerResponseCodes::BadRequest;
			OnComplete(MoveTemp(InResponse));
		}
	}

	/** Remote event mechanism delegate handles */
	TArray<FRemoteEventDispatcher> EventDispatchers;

	/** Console commands handle. */
	TUniquePtr<FAutoConsoleCommand> StartServerCommand;
	TUniquePtr<FAutoConsoleCommand> StopServerCommand;

	/** Http router handle */
	TSharedPtr<IHttpRouter> HttpRouter;

	/** Handles to different exposed routes. */
	FHttpRouteHandle RemoteRouteOptionsHandle;
	FHttpRouteHandle RemoteCallRouteHandle;
	FHttpRouteHandle RemotePropertyRouteHandle;
	FHttpRouteHandle RemoteEventRouteHandle;
};

IMPLEMENT_MODULE(FWebRemoteControlModule, WebRemoteControl);
