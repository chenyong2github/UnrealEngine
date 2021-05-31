// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildFunction.h"
#include "DerivedDataCacheKey.h"
#include "Memory/MemoryFwd.h"
#include "Serialization/CompactBinary.h"
#include "Templates/Function.h"
#include <atomic>

namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class IBuildJob; }
namespace UE::DerivedData { enum class EBuildPolicy : uint8; }
namespace UE::DerivedData { enum class ECachePolicy : uint8; }

namespace UE::DerivedData::Private
{

class FBuildJobContext final : public FBuildContext, public FBuildConfigContext
{
public:
	FBuildJobContext(
		IBuildJob& InJob,
		const FCacheKey& InCacheKey,
		const IBuildFunction& InFunction,
		FBuildOutputBuilder& InOutputBuilder,
		EBuildPolicy InBuildPolicy,
		TUniqueFunction<void ()>&& InOnEndAsyncBuild);

	inline const FCacheKey& GetCacheKey() const { return CacheKey; }
	inline const IBuildFunction& GetFunction() const { return Function; }

	inline ECachePolicy GetCachePolicy() const final { return CachePolicy; }
	inline EBuildPolicy GetBuildPolicy() const final { return BuildPolicy; }

	inline bool IsAsyncBuild() const { return bIsAsyncBuild; }
	inline bool ShouldCheckDeterministicOutput() const { return bDeterministicOutputCheck; }

	void AddConstant(FStringView Key, FCbObject&& Value);
	void AddInput(FStringView Key, const FCompressedBuffer& Value);

	void ResetInputs();

private:
	FCbObject FindConstant(FStringView Key) const final;
	FSharedBuffer FindInput(FStringView Key) const final;

	void AddPayload(const FPayload& Payload) final;
	void AddPayload(const FPayloadId& Id, const FCompressedBuffer& Buffer) final;
	void AddPayload(const FPayloadId& Id, const FSharedBuffer& Buffer) final;
	void AddPayload(const FPayloadId& Id, const FCbObject& Object) final;

	void BeginAsyncBuild() final;
	void EndAsyncBuild() final;

	void SetCacheBucket(FCacheBucket Bucket) final;
	void SetCachePolicy(ECachePolicy Policy) final;
	void SetBuildPolicy(EBuildPolicy Policy) final;
	void SkipDeterministicOutputCheck() { bDeterministicOutputCheck = false; }

public:
	void AddRef() const
	{
		ReferenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	void Release() const
	{
		if (ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			delete this;
		}
	}

private:
	IBuildJob& Job;
	FCacheKey CacheKey;
	const IBuildFunction& Function;
	FBuildOutputBuilder& OutputBuilder;
	TMap<FString, FCbObject> Constants;
	TMap<FString, FCompressedBuffer> Inputs;
	TUniqueFunction<void ()> OnEndAsyncBuild;
	ECachePolicy CachePolicy;
	EBuildPolicy BuildPolicy;
	bool bIsAsyncBuild{false};
	bool bIsAsyncBuildComplete{false};
	bool bDeterministicOutputCheck{true};
	mutable std::atomic<uint32> ReferenceCount{0};
};

} // UE::DerivedData::Private
