// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"

#include "DataflowConnection.generated.h"

struct FDataflowNode;


namespace Dataflow
{

	template<class T> inline DATAFLOWCORE_API FName GraphConnectionTypeName();
	template<class T> inline DATAFLOWCORE_API T DeepCopy(const T&);

	struct FPin
	{
		enum class EDirection : uint8 {
			NONE = 0,
			INPUT,
			OUTPUT
		};
		EDirection Direction;
		FName Type;
		FName Name;
	};
}

//
// Input Output Base
//
USTRUCT()
struct DATAFLOWCORE_API FDataflowConnection
{
	GENERATED_USTRUCT_BODY()

protected:
	Dataflow::FPin::EDirection Direction;
	FName Type;
	FName Name;
	FDataflowNode* OwningNode = nullptr;
	FProperty* Property = nullptr;
	FGuid  Guid;

	friend struct FDataflowNode;

public:
	FDataflowConnection() {};
	FDataflowConnection(Dataflow::FPin::EDirection Direction, FName InType, FName InName, FDataflowNode* OwningNode = nullptr, FProperty* InProperty = nullptr, FGuid InGuid = FGuid::NewGuid());
	virtual ~FDataflowConnection() {};

	FDataflowNode* GetOwningNode() { return OwningNode; }
	const FDataflowNode* GetOwningNode() const { return OwningNode; }

	Dataflow::FPin::EDirection GetDirection() const { return Direction; }
	uint32 GetOffset( ) const;

	FName GetType() const { return Type; }

	FGuid GetGuid() const { return Guid; }
	void SetGuid(FGuid InGuid) { Guid = InGuid; }

	FName GetName() const { return Name; }
	void SetName(FName InName) { Name = InName; }

	size_t RealAddress() const { ensure(OwningNode);  return (size_t)OwningNode + (size_t)GetOffset(); };
	size_t CacheKey() const { return RealAddress(); };

	virtual bool AddConnection(FDataflowConnection* In) { return false; };
	virtual bool RemoveConnection(FDataflowConnection* In) { return false; }

	virtual TArray< FDataflowConnection* > GetConnectedInputs() { return TArray<FDataflowConnection* >(); }
	virtual const TArray< const FDataflowConnection* > GetConnectedInputs() const { return TArray<const FDataflowConnection* >(); }

	virtual TArray< FDataflowConnection* > GetConnectedOutputs() { return TArray<FDataflowConnection* >(); }
	virtual const TArray< const FDataflowConnection* > GetConnectedOutputs() const { return TArray<const FDataflowConnection* >(); }

	template<class T>
	bool IsA(const T* InVar) const
	{
		return (size_t)OwningNode + (size_t)GetOffset() == (size_t)InVar;

	}

	template<class T> const T& GetValueAsInput(Dataflow::FContext& Context, const T& Default) const
	{
		if (GetConnectedOutputs().Num())
		{
			ensure(GetConnectedOutputs().Num() == 1);
			if (const FDataflowConnection* ConnectionBase = (GetConnectedOutputs()[0]))
			{
				if (!ConnectionBase->Evaluate(Context))
				{
					Context.SetData(ConnectionBase->CacheKey(), new Dataflow::ContextCache<T>(Property, new T(Default)));
				}
				if (Context.HasData(ConnectionBase->CacheKey()))
				{
					return Context.GetDataReference<T>(ConnectionBase->CacheKey(), Default);
				}
			}
		}
		return Default;
	}

	virtual void Invalidate() {};
	virtual bool Evaluate(Dataflow::FContext& Context) const { return false; };

};
