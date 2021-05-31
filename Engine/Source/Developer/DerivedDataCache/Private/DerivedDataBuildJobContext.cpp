// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildJobContext.h"

#include "DerivedDataBuildJob.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPolicy.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataCache.h"
#include "Hash/Blake3.h"
#include "Misc/StringBuilder.h"
#include "UObject/NameTypes.h"

namespace UE::DerivedData::Private
{

FBuildJobContext::FBuildJobContext(
	IBuildJob& InJob,
	const FCacheKey& InCacheKey,
	const IBuildFunction& InFunction,
	FBuildOutputBuilder& InOutputBuilder,
	EBuildPolicy InBuildPolicy,
	TUniqueFunction<void ()>&& InOnEndAsyncBuild)
	: Job(InJob)
	, CacheKey(InCacheKey)
	, Function(InFunction)
	, OutputBuilder(InOutputBuilder)
	, OnEndAsyncBuild(MoveTemp(InOnEndAsyncBuild))
	, CachePolicy(ECachePolicy::Default)
	, BuildPolicy(InBuildPolicy)
{
}

void FBuildJobContext::AddConstant(FStringView Key, FCbObject&& Value)
{
	Constants.EmplaceByHash(GetTypeHash(Key), Key, MoveTemp(Value));
}

void FBuildJobContext::AddInput(FStringView Key, const FCompressedBuffer& Value)
{
	Inputs.EmplaceByHash(GetTypeHash(Key), Key, Value);
}

void FBuildJobContext::ResetInputs()
{
	Constants.Empty();
	Inputs.Empty();
}

FCbObject FBuildJobContext::GetConstant(FStringView Key) const
{
	if (const FCbObject* Object = Constants.FindByHash(GetTypeHash(Key), Key))
	{
		return *Object;
	}
	return FCbObject();
}

FSharedBuffer FBuildJobContext::GetInput(FStringView Key) const
{
	if (const FCompressedBuffer* Input = Inputs.FindByHash(GetTypeHash(Key), Key))
	{
		FSharedBuffer Buffer = Input->Decompress();
		const FBlake3Hash RawHash = FBlake3::HashBuffer(Buffer);
		if (RawHash == Input->GetRawHash() && Buffer.GetSize() == Input->GetRawSize())
		{
			return Buffer;
		}
		else
		{
			TStringBuilder<32> Category;
			Category << ImplicitConv<FName>(LogDerivedDataBuild.GetCategoryName());
			TStringBuilder<256> Error;
			Error << TEXT("Input '") << Key << TEXT("' was expected to have raw hash ") << Input->GetRawHash()
				<< TEXT(" and raw size ") << Input->GetRawSize() << TEXT(" but has raw hash ") << RawHash
				<< TEXT(" and raw size ") << Buffer.GetSize() << TEXT(" after decompression for build of '")
				<< Job.GetName() << TEXT("' by ") << Job.GetFunction() << TEXT(".");
			OutputBuilder.AddError(Category, Error);
			UE_LOG(LogDerivedDataBuild, Error, TEXT("%.*s"), Error.Len(), Error.GetData());
		}
	}
	return FSharedBuffer();
}

void FBuildJobContext::AddPayload(const FPayload& Payload)
{
	OutputBuilder.AddPayload(Payload);
}

void FBuildJobContext::AddPayload(const FPayloadId& Id, const FCompressedBuffer& Buffer)
{
	AddPayload(FPayload(Id, Buffer));
}

void FBuildJobContext::AddPayload(const FPayloadId& Id, const FSharedBuffer& Buffer)
{
	AddPayload(FPayload(Id, FCompressedBuffer::Compress(NAME_Default, Buffer)));
}

void FBuildJobContext::AddPayload(const FPayloadId& Id, const FCbObject& Object)
{
	FMemoryView SerializedView;
	if (Object.TryGetSerializedView(SerializedView))
	{
		AddPayload(FPayload(Id, FCompressedBuffer::Compress(NAME_Default, Object.GetBuffer())));
	}
	else
	{
		AddPayload(FPayload(Id, FCompressedBuffer::Compress(NAME_Default, FCbObject::Clone(Object).GetBuffer())));
	}
}

void FBuildJobContext::BeginAsyncBuild()
{
	checkf(!bIsAsyncBuild, TEXT("BeginAsyncBuild may only be called once for build of '%s' by %s."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	bIsAsyncBuild = true;
}

void FBuildJobContext::EndAsyncBuild()
{
	checkf(bIsAsyncBuild, TEXT("EndAsyncBuild may only be called after BeginAsyncBuild for build of '%s' by %s."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	checkf(!bIsAsyncBuildComplete, TEXT("EndAsyncBuild may only be called once for build of '%s' by %s."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	bIsAsyncBuildComplete = true;
	OnEndAsyncBuild();
}

void FBuildJobContext::SetCacheBucket(FCacheBucket Bucket)
{
	checkf(!Bucket.IsNull(), TEXT("Null cache bucket not allowed for build of '%s' by %s. ")
		TEXT("The cache can be disabled by calling SetCachePolicy(ECachePolicy::Disable)."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	CacheKey.Bucket = Bucket;
}

void FBuildJobContext::SetCachePolicy(ECachePolicy Policy)
{
	checkf(!EnumHasAnyFlags(Policy, ECachePolicy::SkipData),
		TEXT("SkipData flags not allowed on cache policy for build of '%s' by %s. ")
		TEXT("Flags for skipping data may be set indirectly through EBuildPolicy."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	CachePolicy = Policy;
}

void FBuildJobContext::SetBuildPolicy(EBuildPolicy Policy)
{
	checkf(!EnumHasAnyFlags(BuildPolicy ^ Policy, EBuildPolicy::SkipCacheGet | EBuildPolicy::SkipCachePut),
		TEXT("SkipCache flags may not be modified on build policy for build of '%s' by %s. ")
		TEXT("Flags for skipping cache operations may be set through ECachePolicy."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	checkf(!EnumHasAnyFlags(BuildPolicy ^ Policy, EBuildPolicy::SkipBuild | EBuildPolicy::SkipData),
		TEXT("Skip flags may not be modified on build policy for build of '%s' by %s. ")
		TEXT("Flags for skipping the build or the data may only be set through the session."),
		*WriteToString<128>(Job.GetName()), *WriteToString<32>(Job.GetFunction()));
	BuildPolicy = Policy;
}

} // UE::DerivedData::Private
