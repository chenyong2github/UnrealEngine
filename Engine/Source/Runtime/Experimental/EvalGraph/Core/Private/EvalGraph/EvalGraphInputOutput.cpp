// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphInputOutput.h"

#include "EvalGraph/EvalGraphNodeParameters.h"
#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{
	FConnectionTypeBase::FConnectionTypeBase(FName InType, FName InName, FNode* InOwningNode, FGuid InGuid)
		: Type(InType)
		, Name(InName)
		, Guid(InGuid)
		, OwningNode(InOwningNode)
	{}

	void FConnectionTypeBase::AddBaseInput(FNode* InNode, FConnectionTypeBase* That) 
	{ InNode->AddBaseInput(That); }
	void FConnectionTypeBase::AddBaseOutput(FNode* InNode, FConnectionTypeBase* That) 
	{ InNode->AddBaseOutput(That); }


	template<class T>
	FInput<T>::FInput(const FInputParameters<T>& Param, FGuid InGuid )
		: FConnectionTypeBase(Param.Type, Param.Name, Param.Owner, InGuid)
		, Default(Param.Default)
		, Connection(nullptr)
	{
		Super::AddBaseInput(Param.Owner, this);
	}

	template<class T>
	void FInput<T>::SetValue(const T& Value, const FContext& Context)
	{
		Default = Value;
		if (!Connection)
		{
			OwningNode->InvalidateOutputs();
		}
	}

	template<class T>
	void FInput<T>::Invalidate()
	{
		OwningNode->InvalidateOutputs();
	}

	template<class T>
	FOutput<T>::FOutput(const FOutputParameters<T>& Param, FGuid InGuid )
		: FConnectionTypeBase(Param.Type, Param.Name, Param.Owner, InGuid)
	{
		Super::AddBaseOutput(Param.Owner, this);
	}


	template<class T>
	void FOutput<T>::SetValue(T InVal, const FContext& Context)
	{
		CacheKey = Context.GetTypeHash();
		Cache.Data = InVal;
	}

	template<class T>
	T FOutput<T>::Evaluate(const FContext& Context)
	{
		if (CacheKey!=Context.GetTypeHash())
		{
			OwningNode->Evaluate(Context, this);
		}
		ensure(CacheKey==Context.GetTypeHash());
		return Cache.Data;
	}

	template<class T>
	void FOutput<T>::Invalidate( )
	{
		if (CacheKey != UINT_MAX)
		{
			CacheKey = UINT_MAX;
			Cache = T();

			for (FConnectionTypeBase* Con : GetConnections())
			{
				Con->Invalidate();
			}
		}
	}

// Base Types
EVAL_GRAPH_CONNECTION_TYPE(bool, Bool)
EVAL_GRAPH_CONNECTION_TYPE(char, Char)
EVAL_GRAPH_CONNECTION_TYPE(int, Integer)
EVAL_GRAPH_CONNECTION_TYPE(uint8, UInt8)
EVAL_GRAPH_CONNECTION_TYPE(float, Float)
EVAL_GRAPH_CONNECTION_TYPE(double, Double)

}

