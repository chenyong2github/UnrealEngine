// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Async/Future.h"
#include "Misc/Timespan.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

COOKONTHEFLY_API DECLARE_LOG_CATEGORY_EXTERN(LogCookOnTheFly, Log, All);

namespace UE { namespace Cook
{

enum
{ 
	/* The default port used by the the cook-on-the-fly server. */
	DefaultCookOnTheFlyServingPort = 42899
};

/**
 * Flags and message types to be used with the cook-on-the-fly server.
 *
 * The first 8 bits specifies the message type, i.e request, response or a on-way message.
 */
enum class ECookOnTheFlyMessage : uint32
{
	/* Represents no message. */
	None				= 0x00,
	
	/* A one way message. */
	Message				= 0x01,
	/* A request message. */
	Request				= 0x02,
	/* A response message. */
	Response			= 0x04,
	TypeFlags			= 0x0F,

	/* The handshake request message. */
	Handshake			= 0x10,
	/* Request to cook a package. */
	CookPackage			= 0x20,
	/* Get all currenlty cooked packages. */
	GetCookedPackages	= 0x30,
	/* Request to recompile shaders. */
	RecompileShaders	= 0x40,
	/* One way message indicating that one or more packages has been cooked. */
	PackagesCooked		= 0x50,
	/* One way message indicating that one or more files has been added. */
	FilesAdded			= 0x60,
	/* Heartbeat message. */
	Heartbeat			= 0x70,
};
ENUM_CLASS_FLAGS(ECookOnTheFlyMessage);

/**
 * Returns a string from the specified message.
 */
inline const TCHAR* LexToString(ECookOnTheFlyMessage Message)
{
	EnumRemoveFlags(Message, ECookOnTheFlyMessage::TypeFlags);

	switch (Message)
	{
		case ECookOnTheFlyMessage::None:
			return TEXT("None");
		case ECookOnTheFlyMessage::Handshake:
			return TEXT("Handshake");
		case ECookOnTheFlyMessage::CookPackage:
			return TEXT("CookPackage");
		case ECookOnTheFlyMessage::GetCookedPackages:
			return TEXT("GetCookedPackages");
		case ECookOnTheFlyMessage::RecompileShaders:
			return TEXT("RecompileShaders");
		case ECookOnTheFlyMessage::PackagesCooked:
			return TEXT("PackagesCooked");
		case ECookOnTheFlyMessage::FilesAdded:
			return TEXT("FilesAdded");
		case ECookOnTheFlyMessage::Heartbeat:
			return TEXT("Heartbeat");
		default:
			return TEXT("Unknown");
	};
}

/**
 * Cook-on-the-fly message status.
 */
enum class ECookOnTheFlyMessageStatus : uint32
{
	/** No status. */
	None,
	/** The message is successful. */
	Ok,
	/** The message failed. */
	Error
};

/**
 * Returns a string from the specified message status.
 */
inline const TCHAR* LexToString(ECookOnTheFlyMessageStatus Status)
{
	switch (Status)
	{
		case ECookOnTheFlyMessageStatus::None:
			return TEXT("None");
		case ECookOnTheFlyMessageStatus::Ok:
			return TEXT("Ok");
		case ECookOnTheFlyMessageStatus::Error:
			return TEXT("Error");
		default:
			return TEXT("Unknown");
	}
}

/**
 * Cook-on-the-fly message header.
 */
struct FCookOnTheFlyMessageHeader
{
	/** Type of message */
	ECookOnTheFlyMessage MessageType = ECookOnTheFlyMessage::None;
	/** The message status. */
	ECookOnTheFlyMessageStatus MessageStatus = ECookOnTheFlyMessageStatus::None;
	/** Sender id */
	uint32 SenderId = 0;
	/** Correlation id, used to match response with request. */
	uint32 CorrelationId = 0;
	/** When the message was sent. */
	int64 Timestamp = 0;

	COOKONTHEFLY_API FString ToString() const;

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCookOnTheFlyMessageHeader& Header);
};

/**
 * Cook-on-the-fly message.
 */
class FCookOnTheFlyMessage
{
public:
	/** Creates a new instance of a cook-on-the-fly message. */
	COOKONTHEFLY_API FCookOnTheFlyMessage() = default;

	/** Creates a new instance of a cook-on-the-fly message with the specified message type. */
	explicit FCookOnTheFlyMessage(ECookOnTheFlyMessage MessageType)
	{
		Header.MessageType = MessageType;
	}

	/** Returns the message header. */
	FCookOnTheFlyMessageHeader& GetHeader()
	{
		return Header;
	}

	/** Returns the message header. */
	const FCookOnTheFlyMessageHeader& GetHeader() const
	{
		return Header;
	}

	/** Set a new message header. */
	void SetHeader(const FCookOnTheFlyMessageHeader& InHeader)
	{
		Header = InHeader;
	}

	/** Sets the message status. */
	void SetStatus(ECookOnTheFlyMessageStatus InStatus)
	{
		Header.MessageStatus = InStatus;
	}

	/** Returns the message status. */
	ECookOnTheFlyMessageStatus GetStatus() const
	{
		return Header.MessageStatus;
	}

	/** Returns whether the message stauts is OK. */
	bool IsOk() const
	{
		return Header.MessageStatus == ECookOnTheFlyMessageStatus::Ok;
	}

	/** Set the message body. */
	COOKONTHEFLY_API void SetBody(TArray<uint8> InBody);

	/** Set body to serializable type. */
	template<typename BodyType>
	void SetBodyTo(BodyType InBody)
	{
		Body.Empty();
		FMemoryWriter Ar(Body);
		Ar << InBody;
	}
	
	/** Returns the message body. */
	TArray<uint8>& GetBody()
	{
		return Body;
	}

	/** Returns the message body. */
	const TArray<uint8>& GetBody() const
	{
		return Body;
	}

	/** Serialize the body as the specified type. */
	template<typename BodyType>
	BodyType GetBodyAs() const
	{
		BodyType Type;
		FMemoryReader Ar(Body);
		Ar << Type;

		return MoveTemp(Type);
	}

	/** Returns the total size of the message header and message body. */
	int64 TotalSize() const
	{
		return sizeof(FCookOnTheFlyMessageHeader) + Body.Num();
	}

	/** Creates an archive for ready the message body. */
	COOKONTHEFLY_API TUniquePtr<FArchive> ReadBody() const;

	/** Creates an archive for writing the message body. */
	COOKONTHEFLY_API TUniquePtr<FArchive> WriteBody();

	COOKONTHEFLY_API friend FArchive& operator<<(FArchive& Ar, FCookOnTheFlyMessage& Message);

protected:
	FCookOnTheFlyMessageHeader Header;
	TArray<uint8> Body;
};

using FCookOnTheFlyRequest = FCookOnTheFlyMessage;

using FCookOnTheFlyResponse = FCookOnTheFlyMessage;

/**
 * Connection status
 */
enum class ECookOnTheFlyConnectionStatus
{
	Disconnected,
	Connected
};

/**
 * A connected cook-on-the-fly client.
 */
struct FCookOnTheFlyClient
{
	/** A client ID set by the server. */
	uint32 ClientId = 0;
	/** The platform. */
	FName PlatformName;
};

using FCookOnTheFlyRequestHandler = TFunction<bool(FCookOnTheFlyClient, const FCookOnTheFlyRequest&, FCookOnTheFlyResponse&)>;

using FCookOnTheFlyClientConnectionHandler = TFunction<bool(FCookOnTheFlyClient, ECookOnTheFlyConnectionStatus)>;

using FFillRequest = TFunction<void(FArchive&)>;

using FProcessResponse = TFunction<bool(FArchive&)>;

/**
 * Cook-on-the-fly connection server options.
 */
struct FCookOnTheFlyServerOptions
{
	/** The port to listen for new connections. */
	int32 Port = DefaultCookOnTheFlyServingPort;

	/** Callback invoked when a client has connected or disconnected. */
	FCookOnTheFlyClientConnectionHandler HandleClientConnection;

	/** Callback invoked when the server receives a new request. */
	FCookOnTheFlyRequestHandler HandleRequest;
};

/**
 * Cook-on-the-fly host address.
 */
struct FCookOnTheFlyHostOptions
{
	/** Host address. */
	TArray<FString> Hosts;
	/** Host port. */
	int32 Port = DefaultCookOnTheFlyServingPort;
	/** How long to wait for the server to start. */
	FTimespan ServerStartupWaitTime;
};

/**
 * A connection server used to communicate with cook-on-the-fly clients.
 */
class ICookOnTheFlyConnectionServer
{
public:
	virtual ~ICookOnTheFlyConnectionServer() { }

	/** Start the cook-on-the-fly server. */
	virtual bool StartServer() = 0;

	/** Stop the cook-on-the-fly server. */
	virtual void StopServer() = 0;

	/** Broadcast message to all connected clients for the specified platform. */
	virtual bool BroadcastMessage(const FCookOnTheFlyMessage& Message, const FName& PlatformName = NAME_None) = 0;
};

/**
 * A connection used to communicate with the cook-on-the-fly server.
 */
class ICookOnTheFlyServerConnection
{
public:
	virtual ~ICookOnTheFlyServerConnection() { }

	/**
	 * Returns whether connected to the cook-on-the-fly server.
	 */
	virtual bool IsConnected() const = 0;
	
	/**
	 * Disconnect from the server.
	 */
	virtual void Disconnect() = 0;

	/**
	 * Sends a request to the server.
	 *
	 * @param Request The request message to send.
	 * @param FillRequest Callback to populate the request message..
	 * @param ProcessResponse Callback to process the response message.
	 */
	//virtual bool SendRequest(ECookOnTheFlyMessage Request, FFillRequest&& FillRequest, FProcessResponse&& ProcessResponse) = 0;
	virtual TFuture<FCookOnTheFlyResponse> SendRequest(const FCookOnTheFlyRequest& Request) = 0;

	/**
	 * Event triggered when a new message has been sent from the server.
	 */
	DECLARE_EVENT_OneParam(ICookOnTheFlyServerConnection, FMessageEvent, const FCookOnTheFlyMessage&);
	virtual FMessageEvent& OnMessage() = 0;
};

/**
 * Cook-on-the-fly module
 */
class ICookOnTheFlyModule
	: public IModuleInterface
{
public:
	virtual ~ICookOnTheFlyModule() { }

	/**
	 * Creates a new instance of a cook-on-the-fly connection server.
	 *
	 * @param Options The cook-on-the-fly connection server options.
	 */
	virtual TUniquePtr<ICookOnTheFlyConnectionServer> CreateConnectionServer(FCookOnTheFlyServerOptions Options) = 0;

	/**
	 * Connect to the cook-on-the-fly server.
	 *
	 * @param HostOptions Cook-on-the-fly host options.
	 */
	virtual TUniquePtr<ICookOnTheFlyServerConnection> ConnectToServer(const FCookOnTheFlyHostOptions& HostOptions) = 0;
};

}} // namespace UE::Cook
