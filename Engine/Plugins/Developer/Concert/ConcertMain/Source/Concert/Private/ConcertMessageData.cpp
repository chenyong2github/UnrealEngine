// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertMessageData.h"
#include "IdentifierTable/ConcertTransportArchives.h"

#include "Misc/App.h"
#include "UObject/StructOnScope.h"

#include "StructSerializer.h"
#include "StructDeserializer.h"
#include "Backends/CborStructSerializerBackend.h"
#include "Backends/CborStructDeserializerBackend.h"

void FConcertInstanceInfo::Initialize()
{
	InstanceId = FApp::GetInstanceId();
	InstanceName = FApp::GetInstanceName();

	if (IsRunningDedicatedServer())
	{
		InstanceType = TEXT("Server");
	}
	else if (FApp::IsGame())
	{
		InstanceType = TEXT("Game");
	}
	else if (IsRunningCommandlet())
	{
		InstanceType = TEXT("Commandlet");
	}
	else if (GIsEditor)
	{
		InstanceType = TEXT("Editor");
	}
	else
	{
		InstanceType = TEXT("Other");
	}
}

FText FConcertInstanceInfo::ToDisplayString() const
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertInstanceInfo", "InstanceName", "Instance Name: {0}"), FText::FromString(InstanceName));
	return TextBuilder.ToText();
}

bool FConcertInstanceInfo::operator==(const FConcertInstanceInfo& Other) const
{
	return	InstanceId == Other.InstanceId &&
			InstanceName == Other.InstanceName &&
			InstanceType == Other.InstanceType;
}

bool FConcertInstanceInfo::operator!=(const FConcertInstanceInfo& Other) const
{
	return !operator==(Other);
}

void FConcertServerInfo::Initialize()
{
	ServerName = FPlatformProcess::ComputerName();
	InstanceInfo.Initialize();
	InstanceInfo.InstanceType = TEXT("Server");
	ServerFlags = EConcertSeverFlags::None;
}

FText FConcertServerInfo::ToDisplayString() const
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertServerInfo", "ServerName", "Server Name: {0}"), FText::FromString(ServerName));
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertServerInfo", "AdminEndpointId", "Admin Endpoint ID: {0}"), FText::FromString(AdminEndpointId.ToString()));
	TextBuilder.AppendLine(InstanceInfo.ToDisplayString());
	return TextBuilder.ToText();
}

void FConcertClientInfo::Initialize()
{
	InstanceInfo.Initialize();
	DeviceName = FPlatformProcess::ComputerName();
	PlatformName = FPlatformProperties::PlatformName();
	UserName = FApp::GetSessionOwner();
	bHasEditorData = WITH_EDITORONLY_DATA;
	bRequiresCookedData = FPlatformProperties::RequiresCookedData();
}

FText FConcertClientInfo::ToDisplayString() const
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertClientInfo", "DeviceName", "Device Name: {0}"), FText::FromString(DeviceName));
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertClientInfo", "PlatformName", "Platform Name: {0}"), FText::FromString(PlatformName));
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertClientInfo", "UserName", "User Name: {0}"), FText::FromString(UserName));
	TextBuilder.AppendLine(InstanceInfo.ToDisplayString());
	return TextBuilder.ToText();
}

bool FConcertClientInfo::operator==(const FConcertClientInfo& Other) const
{
	return	InstanceInfo == Other.InstanceInfo &&
			DeviceName == Other.DeviceName &&
			PlatformName == Other.PlatformName &&
			UserName == Other.UserName &&
			DisplayName == Other.DisplayName &&
			AvatarColor == Other.AvatarColor &&
			DesktopAvatarActorClass == Other.DesktopAvatarActorClass &&
			VRAvatarActorClass == Other.VRAvatarActorClass &&
			Tags == Other.Tags &&
			bHasEditorData == Other.bHasEditorData &&
			bRequiresCookedData == Other.bRequiresCookedData;
}

bool FConcertClientInfo::operator!=(const FConcertClientInfo& Other) const
{
	return !operator==(Other);
}

FText FConcertSessionClientInfo::ToDisplayString() const
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLine(ClientInfo.ToDisplayString());
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionClientInfo", "ClientEndpointId", "Client Endpoint ID: {0}"), FText::FromString(ClientEndpointId.ToString()));
	return TextBuilder.ToText();
}

FText FConcertSessionInfo::ToDisplayString() const
{
	FTextBuilder TextBuilder;
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionInfo", "SessionId", "Session ID: {0}"), FText::FromString(SessionId.ToString()));
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionInfo", "SessionName", "Session Name: {0}"), FText::FromString(SessionName));
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionInfo", "OwnerUserName", "Session Owner: {0}"), FText::FromString(OwnerUserName));
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionInfo", "ProjectName", "Session Project: {0}"), FText::FromString(Settings.ProjectName));
	//TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionInfo", "BaseRevision", "Session Base Revision: {0}"), FText::AsNumber(Settings.BaseRevision, &FNumberFormattingOptions::DefaultNoGrouping()));
	if (VersionInfos.Num() > 0)
	{
		const FConcertSessionVersionInfo& VersionInfo = VersionInfos.Last();
		TextBuilder.AppendLineFormat(
			NSLOCTEXT("ConcertSessionInfo", "EngineVersion", "Session Engine Version: {0}.{1}.{2}-{3}"), 
			FText::AsNumber(VersionInfo.EngineVersion.Major, &FNumberFormattingOptions::DefaultNoGrouping()),
			FText::AsNumber(VersionInfo.EngineVersion.Minor, &FNumberFormattingOptions::DefaultNoGrouping()),
			FText::AsNumber(VersionInfo.EngineVersion.Patch, &FNumberFormattingOptions::DefaultNoGrouping()),
			FText::AsNumber(VersionInfo.EngineVersion.Changelist, &FNumberFormattingOptions::DefaultNoGrouping())
			);
	}
	TextBuilder.AppendLineFormat(NSLOCTEXT("ConcertSessionInfo", "ServerEndpointId", "Server Endpoint ID: {0}"), FText::FromString(ServerEndpointId.ToString()));
	return TextBuilder.ToText();
}

bool FConcertSessionFilter::ActivityIdPassesFilter(const int64 InActivityId) const
{
	if (ActivityIdsToInclude.Contains(InActivityId))
	{
		return true;
	}

	if (ActivityIdsToExclude.Contains(InActivityId))
	{
		return false;
	}

	return ActivityIdLowerBound <= InActivityId 
		&& ActivityIdUpperBound >= InActivityId;
}

namespace PayloadDetail
{

bool SerializePayloadImpl(const UScriptStruct* InEventType, const void* InEventData, int32& OutUncompressedDataSizeBytes, TArray<uint8>& OutCompressedData, TFunctionRef<bool(const UScriptStruct*, const void*, TArray<uint8>&)> InSerializeFunc)
{
	bool bSuccess = false;

	OutUncompressedDataSizeBytes = 0;
	OutCompressedData.Reset();

	if (InEventType && InEventData)
	{
		// Serialize the uncompressed data
		TArray<uint8> UncompressedData;
		bSuccess = InSerializeFunc(InEventType, InEventData, UncompressedData);

		if (bSuccess)
		{
			// if we serialized something, compress it
			if (UncompressedData.Num() > 0)
			{
				// Compress the result to send on the wire
				int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, UncompressedData.Num());
				OutCompressedData.SetNumUninitialized(CompressedSize);
				if (FCompression::CompressMemory(NAME_Zlib, OutCompressedData.GetData(), CompressedSize, UncompressedData.GetData(), UncompressedData.Num()))
				{
					OutUncompressedDataSizeBytes = UncompressedData.Num();
					OutCompressedData.SetNum(CompressedSize, false);
				}
				else
				{
					bSuccess = false;
					OutUncompressedDataSizeBytes = 0;
					OutCompressedData.Reset();
				}
			}
			// didn't have anything to compress or serialize
			else
			{
				bSuccess = true;
				OutUncompressedDataSizeBytes = 0;
			}
		}
	}

	return bSuccess;
}

bool DeserializePayloadImpl(const UScriptStruct* InEventType, void* InOutEventData, const int32 InUncompressedDataSizeBytes, const TArray<uint8>& InCompressedData, TFunctionRef<bool(const UScriptStruct*, void*, const TArray<uint8>&)> InDeserializeFunc)
{
	bool bSuccess = false;

	if (InEventType && InOutEventData)
	{
		// Don't bother if we do not actually have anything to deserialize
		if (InUncompressedDataSizeBytes > 0)
		{
			// Uncompress the data
			TArray<uint8> UncompressedData;
			UncompressedData.SetNumUninitialized(InUncompressedDataSizeBytes);
			if (FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), UncompressedData.Num(), InCompressedData.GetData(), InCompressedData.Num()))
			{
				// Deserialize the uncompressed data
				bSuccess = InDeserializeFunc(InEventType, InOutEventData, UncompressedData);
			}
		}
		else
		{
			bSuccess = true;
		}
	}

	return bSuccess;
}

bool SerializeBinaryPayload(const UScriptStruct* InEventType, const void* InEventData, int32& OutUncompressedDataSizeBytes, TArray<uint8>& OutCompressedData)
{
	return SerializePayloadImpl(InEventType, InEventData, OutUncompressedDataSizeBytes, OutCompressedData, [](const UScriptStruct* InSourceEventType, const void* InSourceEventData, TArray<uint8>& OutSerializedData)
	{
		FConcertIdentifierWriter Archive(nullptr, OutSerializedData);
		Archive.SetWantBinaryPropertySerialization(true);
		const_cast<UScriptStruct*>(InSourceEventType)->SerializeItem(Archive, (uint8*)InSourceEventData, nullptr);
		return !Archive.GetError();
	});
}

bool DeserializeBinaryPayload(const UScriptStruct* InEventType, void* InOutEventData, const int32 InUncompressedDataSizeBytes, const TArray<uint8>& InCompressedData)
{
	return DeserializePayloadImpl(InEventType, InOutEventData, InUncompressedDataSizeBytes, InCompressedData, [](const UScriptStruct* InTargetEventType, void* InOutTargetEventData, const TArray<uint8>& InSerializedData)
	{
		FConcertIdentifierReader Archive(nullptr, InSerializedData);
		Archive.SetWantBinaryPropertySerialization(true);
		const_cast<UScriptStruct*>(InTargetEventType)->SerializeItem(Archive, (uint8*)InOutTargetEventData, nullptr);
		return !Archive.GetError();
	});
}

bool SerializeCborPayload(const UScriptStruct* InEventType, const void* InEventData, int32& OutUncompressedDataSizeBytes, TArray<uint8>& OutCompressedData)
{
	return SerializePayloadImpl(InEventType, InEventData, OutUncompressedDataSizeBytes, OutCompressedData, [](const UScriptStruct* InSourceEventType, const void* InSourceEventData, TArray<uint8>& OutSerializedData)
	{
		FMemoryWriter Writer(OutSerializedData);
		FCborStructSerializerBackend Serializer(Writer, EStructSerializerBackendFlags::Default);
		FStructSerializer::Serialize(InSourceEventData, *const_cast<UScriptStruct*>(InSourceEventType), Serializer);
		return !Writer.GetError();
	});
}

bool DeserializeCborPayload(const UScriptStruct* InEventType, void* InOutEventData, const int32 InUncompressedDataSizeBytes, const TArray<uint8>& InCompressedData)
{
	return DeserializePayloadImpl(InEventType, InOutEventData, InUncompressedDataSizeBytes, InCompressedData, [](const UScriptStruct* InTargetEventType, void* InOutTargetEventData, const TArray<uint8>& InSerializedData)
	{
		FMemoryReader Reader(InSerializedData);
		FCborStructDeserializerBackend Deserializer(Reader);
		return FStructDeserializer::Deserialize(InOutTargetEventData, *const_cast<UScriptStruct*>(InTargetEventType), Deserializer) && !Reader.GetError();
	});
}

} // namespace PayloadDetail

bool FConcertSessionSerializedPayload::SetPayload(const FStructOnScope& InPayload)
{
	const UStruct* PayloadStruct = InPayload.GetStruct();
	check(PayloadStruct->IsA<UScriptStruct>());
	return SetPayload((UScriptStruct*)PayloadStruct, InPayload.GetStructMemory());
}

bool FConcertSessionSerializedPayload::SetPayload(const UScriptStruct* InPayloadType, const void* InPayloadData)
{
	check(InPayloadType && InPayloadData);
	PayloadTypeName = *InPayloadType->GetPathName();
	return PayloadDetail::SerializeBinaryPayload(InPayloadType, InPayloadData, UncompressedPayloadSize, CompressedPayload);
}

bool FConcertSessionSerializedPayload::GetPayload(FStructOnScope& OutPayload) const
{
	const UStruct* PayloadType = FindObject<UStruct>(nullptr, *PayloadTypeName.ToString());
	if (PayloadType)
	{
		OutPayload.Initialize(PayloadType);
		const UStruct* PayloadStruct = OutPayload.GetStruct();
		check(PayloadStruct->IsA<UScriptStruct>());
		return PayloadDetail::DeserializeBinaryPayload((UScriptStruct*)PayloadStruct, OutPayload.GetStructMemory(), UncompressedPayloadSize, CompressedPayload);
	}
	return false;
}

bool FConcertSessionSerializedPayload::GetPayload(const UScriptStruct* InPayloadType, void* InOutPayloadData) const
{
	check(InPayloadType && InOutPayloadData);
	const UStruct* PayloadType = FindObject<UStruct>(nullptr, *PayloadTypeName.ToString());
	if (PayloadType)
	{
		check(InPayloadType->IsChildOf(PayloadType));
		return PayloadDetail::DeserializeBinaryPayload((UScriptStruct*)InPayloadType, InOutPayloadData, UncompressedPayloadSize, CompressedPayload);
	}
	return false;
}

bool FConcertSessionSerializedCborPayload::SetPayload(const FStructOnScope& InPayload)
{
	const UStruct* PayloadStruct = InPayload.GetStruct();
	check(PayloadStruct->IsA<UScriptStruct>());
	return SetPayload((UScriptStruct*)PayloadStruct, InPayload.GetStructMemory());
}

bool FConcertSessionSerializedCborPayload::SetPayload(const UScriptStruct* InPayloadType, const void* InPayloadData)
{
	check(InPayloadType && InPayloadData);
	PayloadTypeName = *InPayloadType->GetPathName();
	return PayloadDetail::SerializeCborPayload(InPayloadType, InPayloadData, UncompressedPayloadSize, CompressedPayload);
}

bool FConcertSessionSerializedCborPayload::GetPayload(FStructOnScope& OutPayload) const
{
	const UStruct* PayloadType = FindObject<UStruct>(nullptr, *PayloadTypeName.ToString());
	if (PayloadType)
	{
		OutPayload.Initialize(PayloadType);
		const UStruct* PayloadStruct = OutPayload.GetStruct();
		check(PayloadStruct->IsA<UScriptStruct>());
		return PayloadDetail::DeserializeCborPayload((UScriptStruct*)PayloadStruct, OutPayload.GetStructMemory(), UncompressedPayloadSize, CompressedPayload);
	}
	return false;
}

bool FConcertSessionSerializedCborPayload::GetPayload(const UScriptStruct* InPayloadType, void* InOutPayloadData) const
{
	check(InPayloadType && InOutPayloadData);
	const UStruct* PayloadType = FindObject<UStruct>(nullptr, *PayloadTypeName.ToString());
	if (PayloadType)
	{
		check(InPayloadType->IsChildOf(PayloadType));
		return PayloadDetail::DeserializeCborPayload((UScriptStruct*)InPayloadType, InOutPayloadData, UncompressedPayloadSize, CompressedPayload);
	}
	return false;
}
