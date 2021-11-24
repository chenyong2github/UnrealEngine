// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuild.h"

#include "Algo/Accumulate.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildFunctionRegistry.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildPrivate.h"
#include "DerivedDataBuildScheduler.h"
#include "DerivedDataBuildSession.h"
#include "DerivedDataBuildTypes.h"
#include "DerivedDataBuildWorkerRegistry.h"
#include "DerivedDataCache.h"
#include "Misc/Guid.h"

namespace UE::DerivedData::Private
{

DEFINE_LOG_CATEGORY(LogDerivedDataBuild);

/**
 * Derived Data Build System
 *
 * Public Data Types:
 *
 * FBuildDefinition:
 * - Function, Constants, Key->InputId
 * - From FBuildDefinitionBuilder via IBuild::CreateDefinition()
 * - Serializes to/from FCbObject
 * FBuildAction:
 * - Function+Version, BuildSystemVersion, Constants, Key->InputHash+InputSize
 * - From FBuildActionBuilder via IBuild::CreateAction()
 * - Serializes to/from FCbObject
 * FBuildInputs:
 * - Key->InputBuffer
 * - From FBuildInputsBuilder via IBuild::CreateInputs()
 * FBuildOutput:
 * - Metadata, Payloads[], Diagnostics[] (Level, Category, Message)
 * - From FBuildOutputBuilder via IBuild::CreateOutput()
 * - Serializes to/from FCbObject and FCacheRecord
 * FBuildKey:
 * - Unique ID for FBuildDefinition
 * FBuildActionKey:
 * - Unique ID for FBuildAction
 * - Combined with FCacheBucket to create a FCacheKey
 *
 * Public Interface Types:
 *
 * IBuildFunction:
 * - Name (Unique), Version
 * - Represents the steps necessary to convert inputs to the output
 * IBuildFunctionRegistry:
 * - Registry of IBuildFunction used by IBuild
 * IBuildInputResolver:
 * - Resolves FBuildKey to FBuildDefinition
 * - Resolves FBuildDefinition to Key->(RawHash, RawSize, OptionalBuffer)
 * IBuildJob:
 * - From FBuildKey or FBuildDefinition or FBuildAction+FBuildInputs via FBuildSession
 * - Represents an executing build request
 * IBuildScheduler:
 * - Schedules execution of the operations on IBuildJob
 * FBuildSession:
 * - From ICache+IBuild+IBuildScheduler+IBuildInputResolver via IBuild::CreateSession()
 * - Interface for scheduling builds from a key, definition, or action+inputs
 */
class FBuild final : public IBuild
{
public:
	explicit FBuild(ICache& InCache)
		: Cache(InCache)
	{
	}

	FBuildDefinitionBuilder CreateDefinition(FStringView Name, FStringView Function) final
	{
		return CreateBuildDefinition(Name, Function);
	}

	FBuildActionBuilder CreateAction(FStringView Name, FStringView Function) final
	{
		return CreateBuildAction(Name, Function, FunctionRegistry->FindFunctionVersion(Function), Version);
	}

	FBuildInputsBuilder CreateInputs(FStringView Name) final
	{
		return CreateBuildInputs(Name);
	}

	FBuildOutputBuilder CreateOutput(FStringView Name, FStringView Function) final
	{
		return CreateBuildOutput(Name, Function);
	}

	FBuildSession CreateSession(FStringView Name, IBuildInputResolver* InputResolver, IBuildScheduler* Scheduler) final
	{
		return CreateBuildSession(Name, Cache, *this, Scheduler ? *Scheduler : *DefaultScheduler, InputResolver);
	}

	const FGuid& GetVersion() const final
	{
		return Version;
	}

	IBuildFunctionRegistry& GetFunctionRegistry() const final
	{
		return *FunctionRegistry;
	}

	IBuildWorkerRegistry& GetWorkerRegistry() const final
	{
		return *WorkerRegistry;
	}

private:
	ICache& Cache;
	TUniquePtr<IBuildFunctionRegistry> FunctionRegistry{CreateBuildFunctionRegistry()};
	TUniquePtr<IBuildWorkerRegistry> WorkerRegistry{CreateBuildWorkerRegistry()};
	TUniquePtr<IBuildScheduler> DefaultScheduler{CreateBuildScheduler()};
	const FGuid Version{TEXT("02c9caed-123c-4fc3-b8c1-7816058abed5")};
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IBuild* CreateBuild(ICache& Cache)
{
	return new FBuild(Cache);
}

} // UE::DerivedData::Private

namespace UE::DerivedData::Private { class FBuildPolicyShared; }

namespace UE::DerivedData
{

class Private::FBuildPolicyShared final : public Private::IBuildPolicyShared
{
public:
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

	inline TConstArrayView<FBuildPayloadPolicy> GetPayloadPolicies() const final
	{
		return Payloads;
	}

	inline void AddPayloadPolicy(const FBuildPayloadPolicy& Policy) final
	{
		Payloads.Add(Policy);
	}

	inline void Build() final
	{
		Algo::SortBy(Payloads, &FBuildPayloadPolicy::Id);
	}

private:
	TArray<FBuildPayloadPolicy, TInlineAllocator<14>> Payloads;
	mutable std::atomic<uint32> ReferenceCount{0};
};

EBuildPolicy FBuildPolicy::GetPayloadPolicy(const FPayloadId& Id) const
{
	if (Shared)
	{
		if (TConstArrayView<FBuildPayloadPolicy> Payloads = Shared->GetPayloadPolicies(); !Payloads.IsEmpty())
		{
			if (int32 Index = Algo::BinarySearchBy(Payloads, Id, &FBuildPayloadPolicy::Id); Index != INDEX_NONE)
			{
				return Payloads[Index].Policy;
			}
		}
	}
	return DefaultPayloadPolicy;
}

void FBuildPolicyBuilder::AddPayloadPolicy(const FBuildPayloadPolicy& Policy)
{
	if (!Shared)
	{
		Shared = new Private::FBuildPolicyShared;
	}
	Shared->AddPayloadPolicy(Policy);
}

FBuildPolicy FBuildPolicyBuilder::Build()
{
	FBuildPolicy Policy(BasePolicy);
	if (Shared)
	{
		Shared->Build();
		const auto PolicyOr = [](EBuildPolicy A, EBuildPolicy B) { return A | (B & EBuildPolicy::Default); };
		const TConstArrayView<FBuildPayloadPolicy> Payloads = Shared->GetPayloadPolicies();
		Policy.CombinedPolicy = Algo::TransformAccumulate(Payloads, &FBuildPayloadPolicy::Policy, BasePolicy, PolicyOr);
		Policy.Shared = MoveTemp(Shared);
	}
	return Policy;
}

} // UE::DerivedData
