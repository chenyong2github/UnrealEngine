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

template<class TUniverse>
class FDMXProtocolUniverseManager;
class FDMXProtocolPackager;

struct FDMXBuffer;
struct FDMXPacket;
struct IDMXProtocolPacket;

using FDataPtr = TSharedPtr<TArray<uint8>>;
using IDMXFragmentMap = TMap<uint32, uint8>;

using FArrayReaderPtr = TSharedPtr<FArrayReader, ESPMode::ThreadSafe>;
using FDMXPacketPtr = TSharedPtr<FDMXPacket, ESPMode::ThreadSafe>;
using IDMXProtocolUniversePtr = TSharedPtr<IDMXProtocolUniverse, ESPMode::ThreadSafe>;
using IDMXProtocolPtr = TSharedPtr<IDMXProtocol, ESPMode::ThreadSafe>;
using IDMXProtocolPtrWeak = TWeakPtr<IDMXProtocol, ESPMode::ThreadSafe>;
using FDMXBufferPtr = TSharedPtr<FDMXBuffer, ESPMode::ThreadSafe>;

enum class EDMXSendResult : uint8;
