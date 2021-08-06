// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Compression/CompressedBuffer.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildFunction.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequest.h"
#include "HAL/Event.h"
#include "Memory/MemoryFwd.h"
#include "Serialization/CompactBinary.h"
#include "Templates/Function.h"

namespace UE::DerivedData { class FBuildOutputBuilder; }
namespace UE::DerivedData { class IBuildJob; }
namespace UE::DerivedData { class IRequestOwner; }
namespace UE::DerivedData { enum class EBuildPolicy : uint8; }
namespace UE::DerivedData { enum class ECachePolicy : uint8; }
namespace UE::DerivedData { enum class EPriority : uint8; }

namespace UE::DerivedData::Private
{

class FBuildJobContext final : public FBuildContext, public FBuildConfigContext, public FRequestBase
{
public:
	FBuildJobContext(
		IBuildJob& Job,
		const FCacheKey& CacheKey,
		const IBuildFunction& Function,
		FBuildOutputBuilder& OutputBuilder,
		EBuildPolicy BuildPolicy);

	void BeginBuild(IRequestOwner& Owner, TUniqueFunction<void ()>&& OnEndBuild);

	FStringView GetName() const final;

	inline const FCacheKey& GetCacheKey() const { return CacheKey; }

	inline ECachePolicy GetCachePolicy() const final { return CachePolicy; }
	inline EBuildPolicy GetBuildPolicy() const final { return BuildPolicy; }
	inline uint64 GetRequiredMemory() const { return RequiredMemory; }
	inline bool ShouldCheckDeterministicOutput() const { return bDeterministicOutputCheck; }

	void AddConstant(FStringView Key, FCbObject&& Value);
	void AddInput(FStringView Key, const FCompressedBuffer& Value);

	void ResetInputs();

private:
	FCbObject FindConstant(FStringView Key) const final;
	FSharedBuffer FindInput(FStringView Key) const final;

	void AddPayload(const FPayload& Payload) final;
	void AddPayload(const FPayloadId& Id, const FCompressedBuffer& Buffer) final;
	void AddPayload(const FPayloadId& Id, const FCompositeBuffer& Buffer) final;
	void AddPayload(const FPayloadId& Id, const FSharedBuffer& Buffer) final;
	void AddPayload(const FPayloadId& Id, const FCbObject& Object) final;

	void EndBuild();
	void BeginAsyncBuild() final;
	void EndAsyncBuild() final;

	void SetCacheBucket(FCacheBucket Bucket) final;
	void SetCachePolicy(ECachePolicy Policy) final;
	void SetBuildPolicy(EBuildPolicy Policy) final;
	void SetRequiredMemory(uint64 InRequiredMemory) final { RequiredMemory = InRequiredMemory; }
	void SkipDeterministicOutputCheck() final { bDeterministicOutputCheck = false; }

	void SetPriority(EPriority Priority) final;
	void Cancel() final;
	void Wait() final;

private:
	IBuildJob& Job;
	FCacheKey CacheKey;
	const IBuildFunction& Function;
	FBuildOutputBuilder& OutputBuilder;
	TMap<FString, FCbObject> Constants;
	TMap<FString, FCompressedBuffer> Inputs;
	FEventRef BuildCompleteEvent{EEventMode::ManualReset};
	TUniqueFunction<void ()> OnEndBuild;
	IRequestOwner* Owner{};
	uint64 RequiredMemory{};
	ECachePolicy CachePolicy;
	EBuildPolicy BuildPolicy;
	bool bIsAsyncBuild{false};
	bool bIsAsyncBuildComplete{false};
	bool bDeterministicOutputCheck{true};
};

} // UE::DerivedData::Private
