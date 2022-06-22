// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowConnection.h"
#include "Dataflow/DataflowNode.h"
#include "Templates/Function.h"

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

	virtual TArray< FDataflowConnection* > GetConnectedOutputs() override;
	virtual const TArray< const FDataflowConnection* > GetConnectedOutputs() const override;

	template<class T> const T& GetValue(Dataflow::FContext& Context, const T& Default) const
	{
		return GetValueAsInput<T>(Context, Default);
	}


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

public:
	static FDataflowOutput NoOpOutput;

	FDataflowOutput(const Dataflow::FOutputParameters& Param = {}, FGuid InGuid = FGuid::NewGuid());

	TArray<FDataflowInput*>& GetConnections();
	const TArray<FDataflowInput*>& GetConnections() const;

	virtual TArray<FDataflowConnection*> GetConnectedInputs() override;
	virtual const TArray<const FDataflowConnection*> GetConnectedInputs() const override;

	virtual bool AddConnection(FDataflowConnection* InOutput) override;

	virtual bool RemoveConnection(FDataflowConnection* InInput) override;

	template<class T>
	void SetValue(const T& InVal, Dataflow::FContext& Context) const
	{
		if (Property)
		{
			Context.SetData(CacheKey(), new Dataflow::ContextCache<T>(Property, new T(InVal)));
		}
	}

	template<class T> const T& GetValue(Dataflow::FContext& Context, const T& Default) const
	{
		if (!this->Evaluate(Context))
		{
			Context.SetData(CacheKey(), new Dataflow::ContextCache<T>(Property, new T(Default)));
		}


		if (Context.HasData(CacheKey()))
		{
			return Context.GetDataReference<T>(CacheKey(), Default);
		}

		return Default;
	}

		
	bool Evaluate(Dataflow::FContext& Context) const;
	virtual void Invalidate() override;

};