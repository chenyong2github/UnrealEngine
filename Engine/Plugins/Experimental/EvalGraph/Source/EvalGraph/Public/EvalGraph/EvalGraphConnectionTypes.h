// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "GeometryCollection/ManagedArrayCollection.h"

namespace Eg
{
	// ---------------------------------------------------------
	//
	// General purpose EManagedArrayType definition. 
	// This defines things like 
	//     EGraphConnectionType::FManagedArrayCollectionType
	// see EvalGraphConnectionTypeValues.inl for specific types.
	//
#define EVAL_GRAPH_CONNECTION_TYPE(a,A) F##A##Type,
	enum class EGraphConnectionType : uint32
	{
		FNoneType,
#include "EvalGraphConnectionTypeValues.inl"
	};
#undef EVAL_GRAPH_CONNECTION_TYPE

	// ---------------------------------------------------------
	//  GraphConnectionType<T>
	//    Templated function to return a EGraphConnectionType.
	//
	template<class T> inline EGraphConnectionType GraphConnectionType();
#define EVAL_GRAPH_CONNECTION_TYPE(a,A) template<> inline EGraphConnectionType GraphConnectionType<a>() { return EGraphConnectionType::F##A##Type; }
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE

	// ---------------------------------------------------------
	//  GraphConnectionTypeName
	//    Templated function to return a EGraphConnectionType.
	//
	template<class T> inline FName GraphConnectionTypeName();
#define EVAL_GRAPH_CONNECTION_TYPE(a,A) template<> inline FName GraphConnectionTypeName<a>() { return FName(TEXT(#A)); }
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE

// ---------------------------------------------------------
//  GraphConnectionTypeName
//    Templated function to return a EGraphConnectionType as a FName
//
	inline FName GraphConnectionTypeName(EGraphConnectionType ValueType)
	{
		switch (ValueType)
		{
#define EVAL_GRAPH_CONNECTION_TYPE(a,A)	case EGraphConnectionType::F##A##Type:\
		return FName(TEXT(#A));
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE
		}
		return FName("FNoneType");
	}

	// ---------------------------------------------------------
	//  void*
	//     Returns a new EGraphConnectionType pointer based on 
	//     passed type.
	//
	inline void* NewGraphValueType(EGraphConnectionType ValueType)
	{
		switch (ValueType)
		{
#define EVAL_GRAPH_CONNECTION_TYPE(a,A)	case EGraphConnectionType::F##A##Type:\
		return new a();
#include "EvalGraphConnectionTypeValues.inl"
#undef EVAL_GRAPH_CONNECTION_TYPE
		}
		check(false);
		return nullptr;
	}

}

