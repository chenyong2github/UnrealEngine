// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildInput.h"

#include "Compression/CompressedBuffer.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildPrivate.h"
#include <atomic>

namespace UE::DerivedData::Private
{

class FBuildInputBuilderInternal final : public IBuildInputBuilderInternal
{
public:
	inline explicit FBuildInputBuilderInternal(FStringView InName)
		: Name(InName)
	{
		checkf(!Name.IsEmpty(), TEXT("A build input requires a non-empty name."));
	}

	~FBuildInputBuilderInternal() final = default;

	void AddInput(FStringView Key, const FCompressedBuffer& Buffer) final
	{
		const uint32 KeyHash = GetTypeHash(Key);
		checkf(!Key.IsEmpty(), TEXT("Empty key used in input for build of '%s'."), *Name);
		checkf(!Inputs.ContainsByHash(KeyHash, Key), TEXT("Duplicate key '%.*s' used in input for "),
			TEXT("build of '%s'."), Key.Len(), Key.GetData(), *Name);
		Inputs.EmplaceByHash(KeyHash, Key, Buffer);
	}

	FBuildInput Build() final;

	FString Name;
	TMap<FString, FCompressedBuffer> Inputs;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FBuildInputInternal final : public IBuildInputInternal
{
public:
	explicit FBuildInputInternal(FBuildInputBuilderInternal&& InputBuilder);

	~FBuildInputInternal() final = default;

	FStringView GetName() const final { return Name; }

	const FCompressedBuffer& GetInput(FStringView Key) const final;

	inline void AddRef() const final
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	inline void Release() const final
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	FString Name;
	TMap<FString, FCompressedBuffer> Inputs;
	mutable std::atomic<uint32> ReferenceCount{0};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildInputInternal::FBuildInputInternal(FBuildInputBuilderInternal&& InputBuilder)
	: Name(MoveTemp(InputBuilder.Name))
	, Inputs(MoveTemp(InputBuilder.Inputs))
{
	Inputs.KeySort(TLess<>());
}

const FCompressedBuffer& FBuildInputInternal::GetInput(FStringView Key) const
{
	if (const FCompressedBuffer* Buffer = Inputs.FindByHash(GetTypeHash(Key), Key))
	{
		return *Buffer;
	}
	return FCompressedBuffer::Null;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildInput FBuildInputBuilderInternal::Build()
{
	return CreateBuildInput(new FBuildInputInternal(MoveTemp(*this)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FBuildInput CreateBuildInput(IBuildInputInternal* Input)
{
	return FBuildInput(Input);
}

FBuildInputBuilder CreateBuildInputBuilder(IBuildInputBuilderInternal* InputBuilder)
{
	return FBuildInputBuilder(InputBuilder);
}

FBuildInputBuilder CreateBuildInput(FStringView Name)
{
	return CreateBuildInputBuilder(new FBuildInputBuilderInternal(Name));
}

} // UE::DerivedData::Private
