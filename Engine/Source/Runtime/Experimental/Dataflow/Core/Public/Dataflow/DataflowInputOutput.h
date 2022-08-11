// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNode.h"
#include "Templates/Function.h"
#include "Async/Async.h"
#include "GenericPlatform/GenericPlatformCriticalSection.h"

#include "DataflowInputOutput.generated.h"


struct FDataflowOutput;

//
//  Input
//
namespace Dataflow
{
	struct DATAFLOWCORE_API FInputParameters {
		FInputParameters(FName InType = FName(""), FName InName = FName(""), FDataflowNode * InOwner = nullptr, FProperty * InProperty = nullptr)
			: Type(InType)
			, Name(InName)
			, Owner(InOwner)
			, Property(InProperty){}
		FName Type;
		FName Name;
		FDataflowNode* Owner = nullptr;
		FProperty* Property = nullptr;
	};
}

USTRUCT()
struct FDataflowInput : public FDataflowConnection
{
	GENERATED_USTRUCT_BODY()

	static FDataflowInput NoOpInput;

	friend struct FDataflowConnection;

	FDataflowOutput* Connection;
public:
	FDataflowInput(const Dataflow::FInputParameters& Param = {}, FGuid InGuid = FGuid::NewGuid());

	virtual bool AddConnection(FDataflowConnection* InOutput) override;
	virtual bool RemoveConnection(FDataflowConnection* InOutput) override;

	FDataflowOutput* GetConnection() { return Connection; }
	const FDataflowOutput* GetConnection() const { return Connection; }

	virtual TArray< FDataflowOutput* > GetConnectedOutputs();
	virtual const TArray< const FDataflowOutput* > GetConnectedOutputs() const;

	template<class T>
	const T& GetValue(Dataflow::FContext& Context, const T& Default) const;

	template<class T>
	TFuture<const T&> GetValueParallel(Dataflow::FContext& Context, const T& Default) const;

	virtual void Invalidate() override;
};

//
// Output
//
namespace Dataflow
{
	struct DATAFLOWCORE_API FOutputParameters
	{
		FOutputParameters(FName InType = FName(""), FName InName = FName(""), FDataflowNode* InOwner = nullptr, FProperty* InProperty = nullptr)
			: Type(InType)
			, Name(InName)
			, Owner(InOwner)
			, Property(InProperty) {}

		FName Type;
		FName Name;
		FDataflowNode* Owner = nullptr;
		FProperty* Property = nullptr;
	};
}
USTRUCT()
struct DATAFLOWCORE_API FDataflowOutput : public FDataflowConnection
{
	GENERATED_USTRUCT_BODY()

	friend struct FDataflowConnection;

	mutable uint32 CacheKeyValue = UINT_MAX;
	mutable Dataflow::TCacheValue Cache;


	TArray< FDataflowInput* > Connections;

	size_t PassthroughOffsetAddress = INDEX_NONE;

public:
	static FDataflowOutput NoOpOutput;
	
	mutable TSharedPtr<FCriticalSection> OutputLock; // @todo(dataflow) find alternatives
	// Why is this made into a SharedPtr? FCriticalSection cannot be copied - making FCriticalSection a member of
	// FDataflowOutput deletes the copy constructor of FDataflowOutput. But some other part of the code needs
	// FDataflowOutput to be copyable. Hence wrapped it in a sharedPtr inspired by NiagaraEmitter.h:227.

	FDataflowOutput(const Dataflow::FOutputParameters& Param = {}, FGuid InGuid = FGuid::NewGuid());

	TArray<FDataflowInput*>& GetConnections();
	const TArray<FDataflowInput*>& GetConnections() const;

	virtual TArray<FDataflowInput*> GetConnectedInputs();
	virtual const TArray<const FDataflowInput*> GetConnectedInputs() const;

	virtual bool AddConnection(FDataflowConnection* InOutput) override;

	virtual bool RemoveConnection(FDataflowConnection* InInput) override;

	virtual FORCEINLINE void SetPassthroughOffsetAddress(const size_t InPassthroughOffsetAddress)
	{
		PassthroughOffsetAddress = InPassthroughOffsetAddress;
	}

	virtual FORCEINLINE void* GetPassthroughRealAddress() const
	{
		if(PassthroughOffsetAddress != INDEX_NONE)
		{
			return (void*)((size_t)OwningNode + PassthroughOffsetAddress);
		}
		return nullptr;
	}
 
	template<class T>
	void SetValue(const T& InVal, Dataflow::FContext& Context) const
	{
		if (Property)
		{
			Context.SetData(CacheKey(), Property, InVal);
		}
	}

	template<class T> const T& GetValue(Dataflow::FContext& Context, const T& Default) const
	{
		if (!this->Evaluate<T>(Context))
		{
			Context.SetData(CacheKey(), Property, Default);
		}

		if (Context.HasData(CacheKey()))
		{
			return Context.GetData(CacheKey(), Property, Default);
		}

		return Default;
	}

	template<class T>
	bool Evaluate(Dataflow::FContext& Context) const;

	template<class T>
	TFuture<bool> EvaluateParallel(Dataflow::FContext& Context) const;

	virtual void Invalidate() override;

};
 
template<class T>
const T& FDataflowInput::GetValue(Dataflow::FContext& Context, const T& Default) const
{
	if (GetConnectedOutputs().Num())
	{
		ensure(GetConnectedOutputs().Num() == 1);
		if (const FDataflowOutput* ConnectionOut = GetConnection())
		{
			if (!ConnectionOut->Evaluate<T>(Context))
			{
				Context.SetData(ConnectionOut->CacheKey(), Property, Default);
			}
			if (Context.HasData(ConnectionOut->CacheKey()))
			{
				const T& data = Context.GetData(ConnectionOut->CacheKey(), Property, Default);
				return data;
			}
		}
	}
	return Default;
}

template<class T>
TFuture<const T&> FDataflowInput::GetValueParallel(Dataflow::FContext& Context, const T& Default) const
{
	return Async(EAsyncExecution::TaskGraph, [&]() -> const T& { return this->GetValue<T>(Context, Default); });
}

template<class T>
bool FDataflowOutput::Evaluate(Dataflow::FContext& Context) const
{
	OutputLock->Lock(); ON_SCOPE_EXIT { this->OutputLock->Unlock(); };

	check(OwningNode);
 
	if (OwningNode->bActive)
	{
		// check if the cache has a valid version
		if(Context.HasData(CacheKey(), OwningNode->LastModifiedTimestamp))
		{
			return true;
		}
		// if not, evaluate
		OwningNode->Evaluate(Context, this);
		// Validation
		if (!Context.HasData(CacheKey()))
		{
			ensureMsgf(false, TEXT("Failed to evaluate output (%s:%s)"), *OwningNode->GetName().ToString(), *GetName().ToString());
			return false;
		}
		return true;
	}
	else if(const FDataflowInput* PassthroughInput = OwningNode->FindInput(GetPassthroughRealAddress()))
	{
		T PassthroughData = PassthroughInput->GetValue<T>(Context, *reinterpret_cast<const T*>(PassthroughInput->RealAddress()));
		SetValue<T>(PassthroughData, Context);
		return true;
	}
 
	return false;
}

template<class T>
TFuture<bool> FDataflowOutput::EvaluateParallel(Dataflow::FContext& Context) const
{
	return Async(EAsyncExecution::TaskGraph, [&]() -> bool { return this->Evaluate<T>(Context); });
}