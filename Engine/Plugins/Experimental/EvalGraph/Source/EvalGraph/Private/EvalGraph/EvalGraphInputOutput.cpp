// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphInputOutput.h"

#include "EvalGraph/EvalGraphConnectionTypes.h"
#include "EvalGraph/EvalGraphNodeParameters.h"
#include "EvalGraph/EvalGraphNode.h"

namespace Eg
{

	FConnectionTypeBase::FConnectionTypeBase(EGraphConnectionType InType, FNode* InOwningNode, FGuid InGuid)
		: Type(InType)
		, Guid(InGuid)
		, OwningNode(InOwningNode)
	{}

	void FConnectionTypeBase::AddBaseInput(FNode* InNode, FConnectionTypeBase* That) 
	{ InNode->AddBaseInput(That); }
	void FConnectionTypeBase::AddBaseOutput(FNode* InNode, FConnectionTypeBase* That) 
	{ InNode->AddBaseOutput(That); }


	template<class T>
	FInput<T>::FInput(const FInputParameters<T>& Param, FGuid InGuid )
		: FConnectionTypeBase(GraphConnectionType<T>(), Param.Owner, InGuid)
		, Name(Param.Name)
		, Default(Param.Default)
		, Connection(nullptr)
	{
		Super::AddBaseInput(Param.Owner, this);
	}


	template<class T>
	FOutput<T>::FOutput(const FOutputParameters& Param, FGuid InGuid )
		: FConnectionTypeBase(GraphConnectionType<T>(), Param.Owner, InGuid)
		, Name(Param.Name)
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


#define EVAL_GRAPH_CONNECTION_TYPE(a,A) template class FOutput<a>;
#include "../../Public/EvalGraph/EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE

#define EVAL_GRAPH_CONNECTION_TYPE(a,A) template class FInput<a>;
#include "../../Public/EvalGraph/EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE
}

