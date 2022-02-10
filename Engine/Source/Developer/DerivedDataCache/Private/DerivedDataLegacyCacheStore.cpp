// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataLegacyCacheStore.h"

#include "Containers/StringConv.h"
#include "DerivedDataBackendInterface.h"
#include "Misc/Crc.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "String/BytesToHex.h"
#include "String/Find.h"

namespace UE::DerivedData
{

void ILegacyCacheStore::LegacyPut(
	const TConstArrayView<FLegacyCachePutRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCachePutComplete&& OnComplete)
{
	struct FKeyWithUserData
	{
		FLegacyCacheKey Key;
		uint64 UserData;
	};

	TArray<FKeyWithUserData, TInlineAllocator<1>> LegacyRequests;
	TArray<FCachePutValueRequest, TInlineAllocator<1>> ValueRequests;
	LegacyRequests.Reserve(Requests.Num());
	ValueRequests.Reserve(Requests.Num());

	uint64 RequestIndex = 0;
	for (const FLegacyCachePutRequest& Request : Requests)
	{
		LegacyRequests.Add({Request.Key, Request.UserData});
		ValueRequests.Add({Request.Name, Request.Key.GetKey(), Request.Value.GetValue(), Request.Policy, RequestIndex++});
	}

	PutValue(ValueRequests, Owner, [LegacyRequests = MoveTemp(LegacyRequests), OnComplete = MoveTemp(OnComplete)](FCachePutValueResponse&& Response)
	{
		const FKeyWithUserData& LegacyRequest = LegacyRequests[int32(Response.UserData)];
		OnComplete({Response.Name, LegacyRequest.Key, LegacyRequest.UserData, Response.Status});
	});
}

void ILegacyCacheStore::LegacyGet(
	const TConstArrayView<FLegacyCacheGetRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCacheGetComplete&& OnComplete)
{
	struct FKeyWithUserData
	{
		FLegacyCacheKey Key;
		uint64 UserData;
	};

	TArray<FKeyWithUserData, TInlineAllocator<1>> LegacyRequests;
	TArray<FCacheGetValueRequest, TInlineAllocator<1>> ValueRequests;
	LegacyRequests.Reserve(Requests.Num());
	ValueRequests.Reserve(Requests.Num());

	uint64 RequestIndex = 0;
	for (const FLegacyCacheGetRequest& Request : Requests)
	{
		LegacyRequests.Add({Request.Key, Request.UserData});
		ValueRequests.Add({Request.Name, Request.Key.GetKey(), Request.Policy, RequestIndex++});
	}

	GetValue(ValueRequests, Owner, [LegacyRequests = MoveTemp(LegacyRequests), OnComplete = MoveTemp(OnComplete)](FCacheGetValueResponse&& Response)
	{
		const FKeyWithUserData& LegacyRequest = LegacyRequests[int32(Response.UserData)];
		OnComplete({Response.Name, LegacyRequest.Key, FLegacyCacheValue(Response.Value), LegacyRequest.UserData, Response.Status});
	});
}

void ILegacyCacheStore::LegacyDelete(
	const TConstArrayView<FLegacyCacheDeleteRequest> Requests,
	IRequestOwner& Owner,
	FOnLegacyCacheDeleteComplete&& OnComplete)
{
	CompleteWithStatus(Requests, OnComplete, EStatus::Error);
}

FLegacyCacheKey::FLegacyCacheKey(const FStringView InFullKey, const int32 MaxKeyLength)
	: FullKey(InFullKey)
{
	if (InFullKey.Len() > MaxKeyLength)
	{
		const auto FullKeyUCS2 = StringCast<UCS2CHAR>(InFullKey.GetData(), InFullKey.Len());
		const int32 FullKeyLength = FullKeyUCS2.Length();
		const uint32 CRCofPayload(FCrc::MemCrc32(FullKeyUCS2.Get(), FullKeyLength * sizeof(UCS2CHAR)));

		uint8 Hash[FSHA1::DigestSize];
		FSHA1 HashState;
		HashState.Update((const uint8*)&FullKeyLength, sizeof(int32));
		HashState.Update((const uint8*)&CRCofPayload, sizeof(uint32));
		HashState.Update((const uint8*)FullKeyUCS2.Get(), FullKeyLength * sizeof(UCS2CHAR));
		HashState.Final();
		HashState.GetHash(Hash);

		TStringBuilder<128> ShortKeyBuilder;
		ShortKeyBuilder << InFullKey.Left(MaxKeyLength - FSHA1::DigestSize * 2 - 2) << TEXT("__");
		String::BytesToHex(Hash, ShortKeyBuilder);

		check(ShortKeyBuilder.Len() == MaxKeyLength && ShortKeyBuilder.Len() > 0);
		ShortKey = ShortKeyBuilder;
	}

	TStringBuilder<64> Bucket;
	Bucket << TEXT("Legacy");
	if (const int32 BucketEnd = String::FindFirstChar(InFullKey, TEXT('_')); BucketEnd != INDEX_NONE)
	{
		Bucket << InFullKey.Left(BucketEnd);
	}
	Key.Bucket = FCacheBucket(Bucket);
	Key.Hash = FIoHash::HashBuffer(MakeMemoryView(FTCHARToUTF8(InFullKey)));
}

bool FLegacyCacheKey::ReadValueTrailer(FCompositeBuffer& Value) const
{
	if (!ShortKey.IsEmpty())
	{
		TUtf8StringBuilder<256> FullKeyUtf8;
		FullKeyUtf8 << FullKey;
		const FMemoryView CompareKey(FullKeyUtf8.ToString(), (FullKeyUtf8.Len() + 1) * sizeof(UTF8CHAR));
		if (Value.GetSize() < CompareKey.GetSize())
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("FLegacyCacheKey: Hash collision or short value for key %s."), *FullKey);
			return false;
		}
		FUniqueBuffer CopyBuffer;
		const uint64 KeyOffset = Value.GetSize() - CompareKey.GetSize();
		const FMemoryView ValueKey = Value.ViewOrCopyRange(KeyOffset, CompareKey.GetSize(), CopyBuffer);
		if (!CompareKey.EqualBytes(ValueKey))
		{
			UE_LOG(LogDerivedDataCache, Display, TEXT("FLegacyCacheKey: Hash collision for key %s."), *FullKey);
			return false;
		}
		Value = Value.Mid(0, KeyOffset);
	}
	return true;
}

void FLegacyCacheKey::WriteValueTrailer(FCompositeBuffer& Value) const
{
	if (!ShortKey.IsEmpty())
	{
		TUtf8StringBuilder<256> FullKeyUtf8;
		FullKeyUtf8 << FullKey;
		Value = FCompositeBuffer(Value, FSharedBuffer::Clone(FullKeyUtf8.ToString(), (FullKeyUtf8.Len() + 1) * sizeof(UTF8CHAR)));
	}
}

FLegacyCacheValue::FLegacyCacheValue(const FValue& Value)
{
	if (Value.HasData() || !Value.GetRawHash().IsZero())
	{
		Shared = new Private::FLegacyCacheValueShared(Value);
	}
}

FLegacyCacheValue::FLegacyCacheValue(const FCompositeBuffer& RawData)
{
	if (RawData)
	{
		Shared = new Private::FLegacyCacheValueShared(RawData);
	}
}

Private::FLegacyCacheValueShared::FLegacyCacheValueShared(const FValue& InValue)
	: Value(InValue)
{
}

Private::FLegacyCacheValueShared::FLegacyCacheValueShared(const FCompositeBuffer& InRawData)
	: RawData(InRawData.MakeOwned())
{
}

const FValue& Private::FLegacyCacheValueShared::GetValue()
{
	FWriteScopeLock WriteLock(Lock);
	if (!Value.HasData() && !RawData.IsNull())
	{
		Value = FValue::Compress(RawData);
	}
	return Value;
}

const FCompositeBuffer& Private::FLegacyCacheValueShared::GetRawData()
{
	FWriteScopeLock WriteLock(Lock);
	if (RawData.IsNull() && Value.HasData())
	{
		RawData = Value.GetData().DecompressToComposite();
	}
	return RawData;
}

FIoHash Private::FLegacyCacheValueShared::GetRawHash() const
{
	return Value.HasData() ? Value.GetRawHash() : FIoHash::HashBuffer(RawData);
}

uint64 Private::FLegacyCacheValueShared::GetRawSize() const
{
	return Value.HasData() ? Value.GetRawSize() : RawData.GetSize();
}

FLegacyCachePutResponse FLegacyCachePutRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, UserData, Status};
}

FLegacyCacheGetResponse FLegacyCacheGetRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, {}, UserData, Status};
}

FLegacyCacheDeleteResponse FLegacyCacheDeleteRequest::MakeResponse(const EStatus Status) const
{
	return {Name, Key, UserData, Status};
}

} // UE::DerivedData
