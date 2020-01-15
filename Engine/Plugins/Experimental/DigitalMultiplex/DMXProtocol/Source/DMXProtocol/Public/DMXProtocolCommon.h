// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolConstants.h"

class FBufferArchive;
class FJsonObject;
class FArrayReader;
class IDMXProtocol;
class IDMXProtocolDevice;
class IDMXProtocolFactory;
class IDMXProtocolInterface;
class IDMXProtocolInterfaceUSB;
class IDMXProtocolInterfaceEthernet;
class IDMXProtocolPort;
class IDMXProtocolRDM;
class IDMXProtocolUniverse;
class IDMXProtocolSender;
class IDMXProtocolReceiver;

class FDMXProtocolDeviceManager;
class FDMXProtocolInterfaceManager;
class FDMXProtocolPortManager;
class FDMXProtocolUniverseManager;

class FDMXProtocolPackager;

struct FDMXBuffer;
struct FDMXPacket;
struct IDMXProtocolPacket;

using FDataPtr = TSharedPtr<TArray<uint8>>;
using IDevicesMap = TMap<IDMXProtocolInterface*, TSharedPtr<IDMXProtocolDevice>>;
using IInterfacesMap = TMap<uint32, TSharedPtr<IDMXProtocolInterface>>;

using IPortsMap = TMap<uint8, TSharedPtr<IDMXProtocolPort>>;
using IPortsDeviceMap = TMap<IDMXProtocolDevice*, IPortsMap>;
using IUniversesMap = TMap<IDMXProtocolPort*, TSharedPtr<IDMXProtocolUniverse>>;
using IUniversesIDMap = TMap<uint16, IDMXProtocolPort*>;

using IDMXFragmentMap = TMap<uint32, uint8>;

using FArrayReaderPtr = TSharedPtr<FArrayReader, ESPMode::ThreadSafe>;
using FDMXPacketPtr = TSharedPtr<FDMXPacket, ESPMode::ThreadSafe>;