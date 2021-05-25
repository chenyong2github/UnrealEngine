// Copyright Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "IRemoteControlModule.h"
#include "RemoteControlModels.h"
#include "RemoteControlRequest.generated.h"

struct FBlockDelimiters
{
	int64 BlockStart = -1;
	int64 BlockEnd = -1;

	/** Get the size of current block */
	int64 GetBlockSize() const { return BlockEnd - BlockStart; }
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

	/** Holds the request's TCHAR payload. */
	TArray<uint8> TCHARBody;

protected:
	void AddStructParameter(FString ParameterName)
	{
		StructParameters.Add(MoveTemp(ParameterName), FBlockDelimiters());
	}

	/** Holds the start and end of struct parameters */
	TMap<FString, FBlockDelimiters> StructParameters;
};

USTRUCT()
struct FRCRequestWrapper : public FRCRequest
{
	GENERATED_BODY()
	
	FRCRequestWrapper()
		: RequestId(-1)
	{
		AddStructParameter(BodyLabel());
	}

	/**
	 * Get the label for the parameters struct.
	 */
	static FString BodyLabel() { return TEXT("Body"); }
	
	UPROPERTY()
	FString URL;

	UPROPERTY()
	FName Verb;

	UPROPERTY()
	int32 RequestId;
};

/**
 * Holds a request that wraps multiple requests..
 */
USTRUCT()
struct FRCBatchRequest : public FRCRequest
{
	GENERATED_BODY()

	/**
	 * The list of batched requests.
	 */
	UPROPERTY()
	TArray<FRCRequestWrapper> Requests;
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
 * Holds a request to set a property on a preset
 */
USTRUCT()
struct FRCPresetSetPropertyRequest : public FRCRequest
{
	GENERATED_BODY()

	FRCPresetSetPropertyRequest()
	{
		AddStructParameter(PropertyValueLabel());
	}

	/**
	 * Get the label for the PropertyValue struct.
	 */
	static FString PropertyValueLabel() { return TEXT("PropertyValue"); }

	/**
	 * Whether a transaction should be created for the call.
	 */
	UPROPERTY()
	bool GenerateTransaction = false;

	UPROPERTY()
	bool ResetToDefault = false;
};

/**
 * Holds a request to call a function on a preset
 */
USTRUCT()
struct FRCPresetCallRequest : public FRCRequest
{
	GENERATED_BODY()

	FRCPresetCallRequest()
	{
		AddStructParameter(ParametersLabel());
	}

	/**
	 * Get the label for the parameters struct.
	 */
	static FString ParametersLabel() { return TEXT("Parameters"); }

	/**
	 * Whether a transaction should be created for the call.
	 */
	UPROPERTY()
	bool GenerateTransaction = false;
};

/**
 * Holds a request to describe an object using its path.
 */
USTRUCT()
struct FDescribeObjectRequest: public FRCRequest
{
	GENERATED_BODY()

	FDescribeObjectRequest()
	{
	}

	/**
	 * The target object's path.
	 */
	UPROPERTY()
	FString ObjectPath;
};

/**
 * Holds a request to search for an asset.
 */
USTRUCT()
struct FSearchAssetRequest : public FRCRequest
{
	GENERATED_BODY()

	FSearchAssetRequest()
		: Limit(100)
	{
	}

	/**
	 * The search query which will be compared with the asset names.
	 */
	UPROPERTY()
	FString Query;

	/*
	 * The filter applied to this search.
	 */
	UPROPERTY()
	FRCAssetFilter Filter;
	
	/**
	 * The maximum number of search results returned.
	 */
	UPROPERTY()
	int32 Limit;
};

/**
 * Holds a request to search for an actor.
 */
USTRUCT()
struct FSearchActorRequest : public FRCRequest
{
	GENERATED_BODY()

	FSearchActorRequest()
		: Limit(100)
	{
	}

	/*
	 * The search query.
	 */
	UPROPERTY()
	FString Query;

	/**
	 * The target actor's class. 
	 */
	UPROPERTY()
	FString Class;

	/**
	 * The maximum number of search results returned.
	 */
	UPROPERTY()
	int32 Limit;
};

/**
 * Holds a request to search for an asset.
 */
USTRUCT()
struct FSearchObjectRequest : public FRCRequest
{
	GENERATED_BODY()

	FSearchObjectRequest()
		: Limit(100)
	{
	}

	/*
	 * The search query.
	 */
	UPROPERTY()
	FString Query;

	/**
	 * The target object's class.
	 */
	UPROPERTY()
	FString Class;

	/**
	 * The search target's outer object.
	 */
	UPROPERTY()
	FString Outer;

	/**
	 * The maximum number of search results returned.
	 */
	UPROPERTY()
	int32 Limit;
};


/**
 * Holds a request to set a metadata field.
 */
USTRUCT()
struct FSetPresetMetadataRequest : public FRCRequest
{
	GENERATED_BODY()

	FSetPresetMetadataRequest() = default;

	/**
	 * The new value for the metadata field.
	 */
	UPROPERTY()
	FString Value;
};

/**
 * Holds a request to set a metadata field.
 */
USTRUCT()
struct FSetEntityMetadataRequest : public FRCRequest
{
	GENERATED_BODY()

	FSetEntityMetadataRequest() = default;

	/**
	 * The new value for the metadata field.
	 */
	UPROPERTY()
	FString Value;
};

/**
 * Holds a request to set an entity's label.
 */
USTRUCT()
struct FSetEntityLabelRequest : public FRCRequest
{
	GENERATED_BODY()

	/**
	 * The new label to assign.
	 */
	UPROPERTY()
	FString NewLabel;
};

/**
 * Holds a request to get an asset's thumbnail.
 */
USTRUCT()
struct FGetObjectThumbnailRequest : public FRCRequest
{
	GENERATED_BODY()

	FGetObjectThumbnailRequest() = default;

	/**
	 * The target object's path.
	 */
	UPROPERTY()
	FString ObjectPath;
};

/**
 * Holds a request made for web socket.
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
	int32 Id = INDEX_NONE;
};

/**
 * Holds a request made for web socket.
 */
USTRUCT()
struct FRCWebSocketPresetRegisterBody : public FRCRequest
{
	GENERATED_BODY()

	FRCWebSocketPresetRegisterBody()
	{
		AddStructParameter(ParametersFieldLabel());
	}

	/**
	 * Get the label for the property value struct.
	 */
	static FString ParametersFieldLabel() { return TEXT("Parameters"); }

	/**
	 * Name of the preset its registering.
	 */
	UPROPERTY()
	FString PresetName;

	/** Whether changes to properties triggered remotely should fire an event. */
	UPROPERTY()
	bool IgnoreRemoteChanges = false;
};

