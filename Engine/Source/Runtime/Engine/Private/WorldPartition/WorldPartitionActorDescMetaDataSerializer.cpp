// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionActorMetaDataSerializer.h"

#if WITH_EDITOR

bool FActorMetaDataReader::Serialize(FName Name, bool& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		if (ValueStr == TEXT("0"))
		{
			Value = false;
			return true;
		}
		else if (ValueStr == TEXT("1"))
		{
			Value = true;
			return true;
		}
	}

	return false;
}

bool FActorMetaDataReader::Serialize(FName Name, int8& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		Value = FCString::Atoi(*ValueStr);
		return true;
	}

	return false;
}

bool FActorMetaDataReader::Serialize(FName Name, int32& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		Value = FCString::Atoi(*ValueStr);
		return true;
	}

	return false;
}

bool FActorMetaDataReader::Serialize(FName Name, int64& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		Value = FCString::Atoi64(*ValueStr);
		return true;
	}

	return false;
}

bool FActorMetaDataReader::Serialize(FName Name, FGuid& Value)
{
	FString ValueStr;
	return ReadTag(Name, ValueStr) && FGuid::Parse(ValueStr, Value);
}

bool FActorMetaDataReader::Serialize(FName Name, FVector& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		Value.InitFromString(ValueStr);
		return true;
	}

	return false;
}

bool FActorMetaDataReader::Serialize(FName Name, FTransform& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		Value.InitFromString(ValueStr);
		return true;
	}

	return false;
}

bool FActorMetaDataReader::Serialize(FName Name, FString& Value)
{
	return ReadTag(Name, Value);
}

bool FActorMetaDataReader::Serialize(FName Name, FName& Value)
{
	FString ValueStr;
	if (ReadTag(Name, ValueStr))
	{
		Value = *ValueStr;
		return true;
	}

	return false;
}

bool FActorMetaDataReader::ReadTag(FName Name, FString& Value)
{
	return AssetData.GetTagValue(Name, Value);
}

bool FActorMetaDataWriter::Serialize(FName Name, bool& Value)
{
	return WriteTag(Name, Value ? TEXT("1") : TEXT("0"));
}

bool FActorMetaDataWriter::Serialize(FName Name, int8& Value)
{
	return WriteTag(Name, *FString::Printf(TEXT("%d"), Value));
}

bool FActorMetaDataWriter::Serialize(FName Name, int32& Value)
{
	return WriteTag(Name, *FString::Printf(TEXT("%d"), Value));
}

bool FActorMetaDataWriter::Serialize(FName Name, int64& Value)
{
	return WriteTag(Name, *FString::Printf(TEXT("%lld"), Value));
}

bool FActorMetaDataWriter::Serialize(FName Name, FGuid& Value)
{
	return WriteTag(Name, Value.ToString(EGuidFormats::Base36Encoded));
}

bool FActorMetaDataWriter::Serialize(FName Name, FVector& Value)
{
	return WriteTag(Name, Value.ToCompactString());
}

bool FActorMetaDataWriter::Serialize(FName Name, FTransform& Value)
{
	return WriteTag(Name, Value.ToString());
}

bool FActorMetaDataWriter::Serialize(FName Name, FString& Value)
{
	return WriteTag(Name, Value);
}

bool FActorMetaDataWriter::Serialize(FName Name, FName& Value)
{
	return WriteTag(Name, Value.ToString());
}

bool FActorMetaDataWriter::WriteTag(FName Name, const FString& Value)
{
	Tags.Add(UObject::FAssetRegistryTag(Name, Value, UObject::FAssetRegistryTag::TT_Hidden));
	return true;
}
#endif