// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Conversion/DisplayClusterNetworkDataConversion.h"

#include "Cluster/DisplayClusterClusterEvent.h"
#include "Network/Packet/DisplayClusterPacketBinary.h"
#include "Network/Packet/DisplayClusterPacketJson.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncStrings.h"


#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"


namespace DisplayClusterNetworkDataConversion
{
	// Internal packet helpers
	void JsonEventsFromInternalPacket(const TSharedPtr<FDisplayClusterPacketInternal>& Packet, TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents)
	{
		TArray<FString> TextObjects;
		Packet->GetTextObjects(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, TextObjects, false);

		for (const auto& it : TextObjects)
		{
			TSharedPtr<FDisplayClusterClusterEventJson> JsonEvent = MakeShared<FDisplayClusterClusterEventJson>();
			if (JsonEvent->DeserializeFromString(*it))
			{
				JsonEvents.Add(JsonEvent);
			}
		}
	}

	void JsonEventsToInternalPacket(const TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TSharedPtr<FDisplayClusterPacketInternal>& Packet)
	{
		for (const auto& it : JsonEvents)
		{
			const FString TextObject = it->SerializeToString();
			Packet->AddTextObject(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, TextObject);
		}
	}

	void BinaryEventsFromInternalPacket(const TSharedPtr<FDisplayClusterPacketInternal>& Packet, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents)
	{
		TArray<TArray<uint8>> BinaryObjects;
		Packet->GetBinObjects(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, BinaryObjects, false);

		for (const auto& it : BinaryObjects)
		{
			TSharedPtr<FDisplayClusterClusterEventBinary> BinaryEvent = MakeShared<FDisplayClusterClusterEventBinary>();
			if (BinaryEvent->DeserializeFromByteArray(it))
			{
				BinaryEvents.Add(BinaryEvent);
			}
		}
	}

	void BinaryEventsToInternalPacket(const TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents, TSharedPtr<FDisplayClusterPacketInternal>& Packet)
	{
		for (const auto& it : BinaryEvents)
		{
			TArray<uint8> BinaryObject;
			it->SerializeToByteArray(BinaryObject);
			Packet->AddBinObject(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, BinaryObject);
		}
	}

	// JSON events conversion
	bool JsonPacketToJsonEvent(const TSharedPtr<FDisplayClusterPacketJson>& Packet, FDisplayClusterClusterEventJson& OutBinaryEvent)
	{
		return FJsonObjectConverter::JsonObjectToUStruct(Packet->GetJsonData().ToSharedRef(), &OutBinaryEvent);
	}

	TSharedPtr<FDisplayClusterPacketJson> JsonEventToJsonPacket(const FDisplayClusterClusterEventJson& JsonEvent)
	{
		TSharedPtr<FDisplayClusterPacketJson> Packet;

		TSharedPtr<FJsonObject> JsonObject = FJsonObjectConverter::UStructToJsonObject(JsonEvent);
		if (JsonObject)
		{
			Packet = MakeShared<FDisplayClusterPacketJson>();
			Packet->SetJsonData(JsonObject);
		}

		return Packet;
	}

	// Binary events conversion
	bool BinaryPacketToBinaryEvent(const TSharedPtr<FDisplayClusterPacketBinary>& Packet, FDisplayClusterClusterEventBinary& OutBinaryEvent)
	{
		const TArray<uint8>& Buffer = Packet->GetPacketData();
		return OutBinaryEvent.DeserializeFromByteArray(Buffer);
	}

	TSharedPtr<FDisplayClusterPacketBinary> BinaryEventToBinaryPacket(const FDisplayClusterClusterEventBinary& BinaryEvent)
	{
		TSharedPtr<FDisplayClusterPacketBinary> Packet = MakeShared<FDisplayClusterPacketBinary>();
		TArray<uint8>& Buffer = Packet->GetPacketData();
		BinaryEvent.SerializeToByteArray(Buffer);
		return Packet;
	}
}
