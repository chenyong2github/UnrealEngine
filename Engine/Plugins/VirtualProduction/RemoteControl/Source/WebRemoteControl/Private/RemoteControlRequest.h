// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "IRemoteControlModule.h"
#include "RemoteControlRequest.generated.h"

struct FBlockDelimiters
{
	int64 BlockStart = -1;
	int64 BlockEnd = -1;
};

/**
 * Holds a request made to the remote control server.
 */
USTRUCT()
struct FRCRequest
{
	GENERATED_BODY()

	virtual ~FRCRequest() = default;

	TMap<FString, FBlockDelimiters>& GetStructParameters()
	{
		return StructParameters;
	}

	FBlockDelimiters& GetParameterDelimiters(const FString& ParameterName)
	{
		return StructParameters.FindChecked(ParameterName);
	}

protected:
	void AddStructParameter(FString ParameterName)
	{
		StructParameters.Add(MoveTemp(ParameterName), FBlockDelimiters());
	}

	/** Holds the start and end of struct parameters */
	TMap<FString, FBlockDelimiters> StructParameters;
};

UENUM()
enum class ERemoteControlEvent : uint8
{
	PreObjectPropertyChanged = 0,
	ObjectPropertyChanged,
	EventCount,
};

/**
 * Holds a request to create an event hook.
 */
USTRUCT()
struct FRemoteControlObjectEventHookRequest : public FRCRequest
{
	GENERATED_BODY()

	FRemoteControlObjectEventHookRequest()
		: EventType(ERemoteControlEvent::EventCount)
	{}

	/**
	 * What type of event should be listened to.
	 */
	UPROPERTY()
	ERemoteControlEvent EventType;

	/**
	 * The path of the target object.
	 */
	UPROPERTY()
	FString ObjectPath;

	/**
	 * The name of the property to watch for changes.
	 */
	UPROPERTY()
	FString PropertyName;
};


/**
 * Holds a request to call a function
 */
USTRUCT()
struct FRCCallRequest : public FRCRequest
{
	GENERATED_BODY()

	FRCCallRequest()
	{
		AddStructParameter(ParametersLabel());
	}

	/**
	 * Get the label for the parameters struct.
	 */
	static FString ParametersLabel() { return TEXT("Parameters"); }

	/**
	 * The path of the target object.
	 */
	UPROPERTY()
	FString ObjectPath;

	/**
	 * The name of the function to call.
	 */
	UPROPERTY()
	FString FunctionName;

	/**
	 * Whether a transaction should be created for the call.
	 */
	UPROPERTY()
	bool GenerateTransaction = false;
};

/**
 * Holds a request to access an object
 */
USTRUCT()
struct FRCObjectRequest : public FRCRequest
{
	GENERATED_BODY()

	FRCObjectRequest()
	{
		AddStructParameter(PropertyValueLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString PropertyValueLabel() { return TEXT("PropertyValue"); }

	/**
	 * Get whether the property should be writen or read and if a transaction should be created.
	 */
	ERCAccess GetAccessValue() const
	{
		ERCAccess RCAccess = Access;
		if (RCAccess == ERCAccess::NO_ACCESS)
		{
			RCAccess = ERCAccess::READ_ACCESS;
			// Use read access by default when no access is specified, but use write access if property value is specified
			if (StructParameters.FindChecked(TEXT("PropertyValue")).BlockStart > 0)
			{
				if (GenerateTransaction)
				{
					RCAccess = ERCAccess::WRITE_TRANSACTION_ACCESS;
				}
				else
				{
					RCAccess = ERCAccess::WRITE_ACCESS;
				}
			}
		}
		
		return RCAccess;
	}

public:
	/**
	 * The path of the target object.
	 */
	UPROPERTY()
	FString ObjectPath;

	/**
	 * The property to read or modify.
	 */
	UPROPERTY()
	FString PropertyName;

	/**
	 * Whether the property should be reset to default.
	 */
	UPROPERTY()
	bool ResetToDefault = false;

	/**
	 * Whether a transaction should be created for the call.
	 */
	UPROPERTY()
	bool GenerateTransaction = false;

private:
	/**
	 * Indicates if the property should be read or written to.
	 */
	UPROPERTY()
	ERCAccess Access = ERCAccess::NO_ACCESS;
};

/**
 * Holds a request made to the remote control server.
 */
USTRUCT()
struct FRCWebSocketRequest : public FRCRequest
{
	GENERATED_BODY()

	FRCWebSocketRequest()
	{
		AddStructParameter(ParametersFieldLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString ParametersFieldLabel() { return TEXT("Parameters"); }

	/**
	 * Name of the websocket message.
	 */
	UPROPERTY()
	FString MessageName;

	/**
	 * (Optional) Id of the incoming message, used to identify a deferred response to the clients.
	 */
	UPROPERTY()
	int32 Id = -1;
};
