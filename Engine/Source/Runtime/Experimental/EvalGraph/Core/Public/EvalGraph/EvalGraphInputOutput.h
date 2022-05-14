// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EvalGraph/EvalGraphNodeParameters.h"
#include "EvalGraph/EvalGraphNode.h"
#include "EvalGraph/EvalGraphConnectionBase.h"

namespace Eg
{
	template<typename T> class FOutput;

	//
	//  Input
	//

	template<class T>
	struct EVALGRAPHCORE_API FInputParameters {
		FInputParameters(FName InName, FNode* InOwner, T InDefault = T())
			: Type(GraphConnectionTypeName<T>())
			, Owner(InOwner)
			, Default(InDefault) {}
		FName Type;
		FName Name;
		FNode* Owner = nullptr;
		T Default;
	};

	template<class T>
	class EVALGRAPHCORE_API FInput : public FConnectionBase
	{
		typedef FConnectionBase Super;
		friend class FConnectionBase;

		T Default;
		FOutput<T>* Connection;
	public:
		FInput(const FInputParameters<T>& Param, FGuid InGuid = FGuid::NewGuid())
			: FConnectionBase(Param.Type, Param.Name, Param.Owner, InGuid)
			, Default(Param.Default)
			, Connection(nullptr)
		{
			Super::AddBaseInput(Param.Owner, this);
		}
	
		const T& GetDefault() const { return Default; }
		
		const FOutput<T>* GetConnection() const { return Connection; }
		FOutput<T>* GetConnection() { return Connection; }

		virtual bool AddConnection(FConnectionBase* InOutput) override
		{ 
			ensure(Connection == nullptr);
			if (ensure(InOutput->GetType()==this->GetType()))
			{
				Connection = (FOutput<T>*)InOutput;
				return true;
			}
			return false;
		}

		virtual bool RemoveConnection(FConnectionBase* InOutput) override
		{ 
			if (ensure(Connection == (FOutput<T>*)InOutput))
			{
				Connection = nullptr;
				return true;
			}
			return false;
		}

		T GetValue(const FContext& Context)
		{
			if (Connection)
			{
				return Connection->Evaluate(Context);
			}
			return Default;
		}

		void SetValue(const T& Value, const FContext& Context)
		{
			Default = Value;
			if (!Connection)
			{
				OwningNode->InvalidateOutputs();
			}
		}

		virtual TArray< FConnectionBase* > GetBaseOutputs() override
		{ 
			TArray<FConnectionBase* > RetList;
			if (GetConnection())
			{
				RetList.Add(GetConnection());
			}
			return RetList;
		}

		virtual void Invalidate() override
		{
			OwningNode->InvalidateOutputs();
		}

	};


	//
	// Output
	//

	template<class T>
	struct EVALGRAPHCORE_API FOutputParameters {
		FOutputParameters(FName InName, FNode * InOwner) 
			: Type(GraphConnectionTypeName<T>())
			, Name(InName)
			, Owner(InOwner) {}
		FName Type;
		FName Name;
		FNode* Owner = nullptr;
	};

	template<class T>
	class EVALGRAPHCORE_API FOutput : public FConnectionBase
	{
		typedef FConnectionBase Super;
		friend class FConnectionBase;

		uint32 CacheKey = UINT_MAX;
		TCacheValue<T> Cache;
		TArray< FInput<T>* > Connections;
	
	public:
		FOutput(const FOutputParameters<T>& Param, FGuid InGuid = FGuid::NewGuid())
			: FConnectionBase(Param.Type, Param.Name, Param.Owner, InGuid)
		{
			Super::AddBaseOutput(Param.Owner, this);
		}


		const TArray<FInput<T>*>& GetConnections() const { return Connections; }
		TArray<FInput<T>* >& GetConnections() { return Connections; }

		virtual TArray<FConnectionBase*> GetBaseInputs() override
		{ 
			TArray<FConnectionBase*> RetList;
			RetList.Reserve(Connections.Num());
			for (FInput<T>* Ptr : Connections) { RetList.Add(Ptr); }
			return RetList;
		}


		virtual bool AddConnection(FConnectionBase* InOutput) override
		{
			if (ensure(InOutput->GetType() == this->GetType()))
			{
				Connections.Add((FInput<T>*)InOutput);
				return true;
			}
			return false;
		}
		
		virtual bool RemoveConnection(FConnectionBase* InInput) override { Connections.RemoveSwap((FInput<T>*)InInput); return true; }

		void SetValue(T InVal, const FContext& Context)
		{
			CacheKey = Context.GetTypeHash();
			Cache.Data = InVal;
		}

		T Evaluate(const FContext& Context)
		{
			if (CacheKey != Context.GetTypeHash())
			{
				OwningNode->Evaluate(Context, this);
			}
			ensure(CacheKey == Context.GetTypeHash());
			return Cache.Data;
		}

		virtual void Invalidate() override
		{
			if (CacheKey != UINT_MAX)
			{
				CacheKey = UINT_MAX;
				Cache = T();

				for (FConnectionBase* Con : GetConnections())
				{
					Con->Invalidate();
				}
			}
		}

	};


#define EVAL_GRAPH_CONNECTION_TYPE(a,A) \
template<> FName GraphConnectionTypeName<a>() { return FName(TEXT(#A)); }\
template class FOutput<a>;\
template class FInput<a>;


}

