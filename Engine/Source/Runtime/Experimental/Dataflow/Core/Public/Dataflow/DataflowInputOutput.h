// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowConnection.h"

namespace Dataflow
{
	template<typename T> class TOutput;

	//
	//  Input
	//

	template<class T>
	struct DATAFLOWCORE_API TInputParameters {
		TInputParameters(FName InName, FNode* InOwner, T InDefault = T())
			: Type(GraphConnectionTypeName<T>())
			, Name(InName)
			, Owner(InOwner)
			, Default(InDefault) {}
		FName Type;
		FName Name;
		FNode* Owner = nullptr;
		T Default;
	};

	template<class T>
	class TInput : public FConnection
	{
		typedef FConnection Super;
		friend class FConnection;

		T Default;
		TOutput<T>* Connection;
	public:
		TInput(const TInputParameters<T>& Param, FGuid InGuid = FGuid::NewGuid())
			: FConnection(FPin::EDirection::INPUT, Param.Type, Param.Name, Param.Owner, InGuid)
			, Default(Param.Default)
			, Connection(nullptr)
		{
			Super::BindInput(Param.Owner, this);
		}
	
		const T& GetDefault() const { return Default; }
		
		const TOutput<T>* GetConnection() const { return Connection; }
		TOutput<T>* GetConnection() { return Connection; }

		virtual bool AddConnection(FConnection* InOutput) override
		{ 
			ensure(Connection == nullptr);
			if (ensure(InOutput->GetType()==this->GetType()))
			{
				Connection = (TOutput<T>*)InOutput;
				return true;
			}
			return false;
		}

		virtual bool RemoveConnection(FConnection* InOutput) override
		{ 
			if (ensure(Connection == (TOutput<T>*)InOutput))
			{
				Connection = nullptr;
				return true;
			}
			return false;
		}

		T GetValue(const FContext& Context) const
		{
			if (Connection)
			{
				return Connection->GetValue(Context);
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

		virtual TArray< FConnection* > GetConnectedOutputs() override
		{ 
			TArray<FConnection* > RetList;
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
	struct DATAFLOWCORE_API TOutputParameters {
		TOutputParameters(FName InName, FNode * InOwner) 
			: Type(GraphConnectionTypeName<T>())
			, Name(InName)
			, Owner(InOwner) {}
		FName Type;
		FName Name;
		FNode* Owner = nullptr;
	};

	template<class T>
	class DATAFLOWCORE_API TOutput : public FConnection
	{
		typedef FConnection Super;
		friend class FConnection;

		uint32 CacheKey = UINT_MAX;
		TCacheValue<T> Cache;
		TArray< TInput<T>* > Connections;
	
	public:
		TOutput(const TOutputParameters<T>& Param, FGuid InGuid = FGuid::NewGuid())
			: FConnection(FPin::EDirection::OUTPUT, Param.Type, Param.Name, Param.Owner, InGuid)
		{
			Super::BindOutput(Param.Owner, this);
		}


		const TArray<TInput<T>*>& GetConnections() const { return Connections; }
		TArray<TInput<T>* >& GetConnections() { return Connections; }

		virtual TArray<FConnection*> GetConnectedInputs() override
		{ 
			TArray<FConnection*> RetList;
			RetList.Reserve(Connections.Num());
			for (TInput<T>* Ptr : Connections) { RetList.Add(Ptr); }
			return RetList;
		}


		virtual bool AddConnection(FConnection* InOutput) override
		{
			if (ensure(InOutput->GetType() == this->GetType()))
			{
				Connections.Add((TInput<T>*)InOutput);
				return true;
			}
			return false;
		}
		
		virtual bool RemoveConnection(FConnection* InInput) override { Connections.RemoveSwap((TInput<T>*)InInput); return true; }

		void SetValue(T InVal, const FContext& Context)
		{
			CacheKey = Context.GetTypeHash();
			Cache.Data = DeepCopy<T>(InVal);
		}

		T GetValue(const FContext& Context)
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

				for (FConnection* Con : GetConnections())
				{
					Con->Invalidate();
				}
			}
		}

	};


//
// Used this macros to defined dataflow connection types 
//

#define DATAFLOW_CONNECTION_TYPE_PRIMITIVE(D, a,A)					\
	template<> inline FName D GraphConnectionTypeName<a>()			\
		{ return FName(TEXT(#A)); }									\
	template<> inline a D DeepCopy<a>(const a & Val)				\
		{ return Val; }												\
	template struct D TOutputParameters<a>;							\
	template class D TOutput<a>;									\
	template struct D TInputParameters<a>;							\
	template class D TInput<a>;

#define DATAFLOW_CONNECTION_TYPE_SHARED_POINTER(D, a,A)				\
	template<> inline FName D GraphConnectionTypeName<a>()			\
		{ return FName(TEXT(#A)); }									\
	template<> inline a D DeepCopy<a>(const a& Val)					\
		{ return Val ? TSharedPtr<a::ElementType>(Val->NewCopy())	\
		: TSharedPtr<a::ElementType>(nullptr); }					\
	template struct D TOutputParameters<a>;							\
	template class D TOutput<a>;									\
	template struct D TInputParameters<a>;							\
	template class D TInput<a>;


DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWCORE_API, bool, Bool)
DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWCORE_API, char, Char)
DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWCORE_API, int, Integer)
DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWCORE_API, uint8, UInt8)
DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWCORE_API, float, Float)
DATAFLOW_CONNECTION_TYPE_PRIMITIVE(DATAFLOWCORE_API, double, Double)
}

