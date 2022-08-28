// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

template <typename OutputType, typename... InputTypes>
class TGLTFConverter
{
	typedef TTuple<InputTypes...> InputTupleType;

public:

	virtual ~TGLTFConverter() = default;

	OutputType Get(InputTypes... Inputs) const
	{
		Sanitize(Inputs...);
		InputTupleType InputTuple(MoveTemp(Inputs)...);
		const uint32 InputHash = GetTypeHash(InputTuple);

		if (OutputType* CachedOutput = CachedOutputs.FindByHash(InputHash, InputTuple))
		{
			return *CachedOutput;
		}

		return {};
	}

	OutputType Add(InputTypes... Inputs)
	{
		Sanitize(Inputs...);
		InputTupleType InputTuple(MoveTemp(Inputs)...);
		const uint32 InputHash = GetTypeHash(InputTuple);

		OutputType NewOutput = InputTuple.ApplyAfter(&TGLTFConverter::Convert, this);
		CachedOutputs.AddByHash(InputHash, MoveTemp(InputTuple), NewOutput);
		return NewOutput;
	}

	OutputType GetOrAdd(InputTypes... Inputs)
	{
		Sanitize(Inputs...);
		InputTupleType InputTuple(MoveTemp(Inputs)...);
		const uint32 InputHash = GetTypeHash(InputTuple);

		if (OutputType* CachedOutput = CachedOutputs.FindByHash(InputHash, InputTuple))
		{
			return *CachedOutput;
		}

		OutputType NewOutput = InputTuple.ApplyAfter(&TGLTFConverter::Convert, this);
		CachedOutputs.AddByHash(InputHash, MoveTemp(InputTuple), NewOutput);
		return NewOutput;
	}

protected:

	virtual void Sanitize(InputTypes&... Inputs) { }

	virtual OutputType Convert(InputTypes... Inputs) = 0;

private:

	TMap<InputTupleType, OutputType> CachedOutputs;
};
