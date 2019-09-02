#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/Function.h"

namespace Trace
{

enum class ENetProfilerDeliveryStatus : uint8
{
	Unknown,
	Delivered,
	Dropped
};

enum ENetProfilerConnectionMode : uint8
{
	Outgoing = 0,
	Incoming = 1,
	Count
};

struct FNetProfilerName
{
	const TCHAR* Name;	// Name
	uint32 NameIndex;	// Index in the type array?
};

typedef double FNetProfilerTimeStamp;

struct FNetProfilerLifeTime
{
	FNetProfilerTimeStamp Begin = 0;
	FNetProfilerTimeStamp End = 0;
};

//struct FNetProfilerProtocol
//{
//	const TCHAR* Name;
//	uint32 ProtocolIndex;
//	uint64 ProtocolIdentifier;
//};

struct FNetProfilerObjectInstance
{
	uint32 ObjectIndex;				// Index in the object array
	uint16 NameIndex;				// Index in the Name array
	uint32 TypeId;					// ProtocolIdentifier
	uint32 NetId;					// NetHandleIndex or NetGUID
	FNetProfilerLifeTime LifeTime;	// Lifetime of this instance
};

struct FNetProfilerContentEvent
{
	uint32 NameIndex ;			// identify the name / type, should we store the actual Name as well
	uint32 ObjectInstanceIndex;	// object instance, non zero if this is a NetObject

	uint64 StartPos : 16;		// Start position in the packet
	uint64 EndPos : 16;			// End position in the packet
	uint64 Level : 4;			// Level
	uint64 ParentIndex : 28;	// Parent to be able to build a tree of nested events?
};

struct FNetProfilerPacket
{
	FNetProfilerTimeStamp TimeStamp;
	uint32 SequenceNumber;
	uint32 ContentSizeInBits;						// This is the part that is tracked by the PacketContents
	uint32 TotalPacketSizeInBytes;					// This is the actual size of the packet sent on the socket
	ENetProfilerDeliveryStatus DeliveryStatus;		// Indicates if the packet was delivered or not, updated as soon as we know

	// Index into Events
	uint32 StartEventIndex;
	uint32 EventCount;
};

struct FNetProfilerConnection
{
	const TCHAR* Name;
	FNetProfilerLifeTime LifeTime;
	uint32 GameIntanceIndex;
	uint32 ConnectionIndex : 16;
	uint32 ConnectionId : 14;
	uint32 bHasIncomingData : 1;
	uint32 bHasOutgoingData : 1;
};

struct FNetProfilerGameInstance
{
	FNetProfilerLifeTime LifeTime;
	uint32 GameInstanceIndex;
	uint32 GameInstanceId;
};

// What queries do we need?
class INetProfilerProvider : public IProvider
{
public:
	// Access Names
	virtual uint32 GetNameCount() const = 0;
	virtual void ReadNames(TFunctionRef<void(const FNetProfilerName*, uint64)> Callback) const = 0;
	virtual void ReadName(uint32 NameIndex, TFunctionRef<void(const FNetProfilerName&)> Callback) const = 0;

	// Access GameInstances
	virtual uint32 GetGameInstanceCount() const = 0;
	virtual void ReadGameInstances(TFunctionRef<void(const FNetProfilerGameInstance&)> Callback) const = 0;

	// Access Connections
	virtual uint32 GetConnectionCount(uint32 GameInstanceIndex) const = 0;
	virtual void ReadConnections(uint32 GameInstanceIndex, TFunctionRef<void(const FNetProfilerConnection&)> Callback) const = 0;
	virtual void ReadConnection(uint32 ConnectionIndex, TFunctionRef<void(const FNetProfilerConnection&)> Callback) const = 0;
	virtual uint32 GetConnectionChangeCount() const = 0;

	// Access Object Instances
	virtual uint32 GetObjectCount(uint32 GameInstanceIndex) const = 0;	
	virtual void ReadObjects(uint32 GameInstanceIndex, TFunctionRef<void(const FNetProfilerObjectInstance&)> Callback) const = 0;
	virtual void ReadObject(uint32 GameInstanceIndex, uint32 ObjectIndex, TFunctionRef<void(const FNetProfilerObjectInstance&)> Callback) const = 0;
	virtual uint32 GeObjectsChangeCount(uint32 GameInstanceIndex) const = 0;

	// Enumerate packets in the provided packet interval
	virtual uint32 GetPacketCount(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode) const = 0;
	virtual void EnumeratePackets(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 PacketIndexIntervalStart, uint32 PacketIndexIntervalEnd, TFunctionRef<void(const FNetProfilerPacket&)> Callback) const = 0;
	virtual uint32 GetPacketChangeCount(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode) const = 0;

	// Enumerate packet content events by range
	virtual void EnumeratePacketContentEventsByIndex(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 StartEventIndex, uint32 EndEventIndex, TFunctionRef<void(const FNetProfilerContentEvent&)> Callback) const = 0;
	virtual void EnumeratePacketContentEventsByPosition(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode, uint32 PacketIndex, uint32 StartPosition, uint32 EndPosition, TFunctionRef<void(const FNetProfilerContentEvent&)> Callback) const = 0;
	virtual uint32 GetPacketContentEventChangeCount(uint32 ConnectionIndex, ENetProfilerConnectionMode Mode) const = 0;
};

TRACESERVICES_API const INetProfilerProvider& ReadNetProfilerProvider(const IAnalysisSession& Session);

}
