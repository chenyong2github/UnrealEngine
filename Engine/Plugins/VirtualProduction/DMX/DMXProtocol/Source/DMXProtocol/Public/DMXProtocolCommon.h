// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolConstants.h"

class FBufferArchive;
class FJsonObject;
class FArrayReader;
class IDMXProtocol;
class IDMXProtocolFactory;
class IDMXProtocolRDM;
class IDMXProtocolUniverse;
class IDMXProtocolSender;
class IDMXProtocolReceiver;
class FDMXSignal;

template<class TUniverse>
class FDMXProtocolUniverseManager;
class FDMXProtocolPackager;

struct FDMXBuffer;
struct FDMXPacket;
struct IDMXProtocolPacket;

using IDMXFragmentMap = TMap<uint32, uint8>;
using IDMXUniverseSignalMap = TMap<int32, TSharedPtr<FDMXSignal>>;

using FArrayReaderPtr = TSharedPtr<FArrayReader, ESPMode::ThreadSafe>;
using FDMXPacketPtr = TSharedPtr<FDMXPacket, ESPMode::ThreadSafe>;
using FDMXBufferPtr = TSharedPtr<FDMXBuffer, ESPMode::ThreadSafe>;
using IDMXProtocolUniversePtr = TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe>;
using IDMXProtocolPtr = TSharedPtr<IDMXProtocol, ESPMode::ThreadSafe>;
using IDMXProtocolPtrWeak = TWeakPtr<IDMXProtocol, ESPMode::ThreadSafe>;

enum class EDMXSendResult : uint8;
