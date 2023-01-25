// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingInputProtocol.h"
#include "PixelStreamingInputMessage.h"
#include "Dom/JsonValue.h"

TMap<FString, FPixelStreamingInputMessage> FPixelStreamingInputProtocol::ToStreamerProtocol;
TMap<FString, FPixelStreamingInputMessage> FPixelStreamingInputProtocol::FromStreamerProtocol;

TSharedPtr<FJsonObject> FPixelStreamingInputProtocol::ToJson(EPixelStreamingMessageDirection Direction)
{
	TSharedPtr<FJsonObject> ProtocolJson = MakeShareable(new FJsonObject());
	TMap<FString, FPixelStreamingInputMessage> MessageProtocol =
		(Direction == EPixelStreamingMessageDirection::ToStreamer)
		? FPixelStreamingInputProtocol::ToStreamerProtocol
		: FPixelStreamingInputProtocol::FromStreamerProtocol;

	ProtocolJson->SetField("Direction", MakeShared<FJsonValueNumber>(static_cast<uint8>(Direction)));
	for (TMap<FString, FPixelStreamingInputMessage>::TIterator Iter = MessageProtocol.CreateIterator(); Iter; ++Iter)
	{
		TSharedPtr<FJsonObject> MessageJson = MakeShareable(new FJsonObject());
		FString MessageType = Iter.Key();
		FPixelStreamingInputMessage Message = Iter.Value();

		MessageJson->SetField("id", MakeShared<FJsonValueNumber>(Message.GetID()));
		MessageJson->SetField("byteLength", MakeShared<FJsonValueNumber>(Message.GetByteLength()));

		if (Message.GetByteLength() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> StructureJson;
			TArray<EPixelStreamingMessageTypes> Structure = Message.GetStructure();
			for (auto It = Structure.CreateIterator(); It; ++It)
			{
				FString Text;
				switch (*It)
				{
					case EPixelStreamingMessageTypes::Uint8:
						Text = "uint8";
						break;
					case EPixelStreamingMessageTypes::Uint16:
						Text = "uint16";
						break;
					case EPixelStreamingMessageTypes::Int16:
						Text = "int16";
						break;
					case EPixelStreamingMessageTypes::Float:
						Text = "float";
						break;
					case EPixelStreamingMessageTypes::Double:
						Text = "double";
						break;
					default:
						Text = "";
				}
				TSharedRef<FJsonValueString> JsonValue = MakeShareable(new FJsonValueString(FString::Printf(TEXT("%s"), *Text)));
				StructureJson.Add(JsonValue);
			}
			MessageJson->SetArrayField("structure", StructureJson);
		}

		ProtocolJson->SetField(*MessageType, MakeShared<FJsonValueObject>(MessageJson));
	}

	return ProtocolJson;
}