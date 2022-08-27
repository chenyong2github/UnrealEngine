// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FGLTFConvertBuilder;

template <typename OutputType, typename... InputTypes>
class TGLTFConverter
{
	typedef TTuple<InputTypes...> InputKeyType;

public:

	TGLTFConverter(FGLTFConvertBuilder& Builder)
		: Builder(Builder)
	{
	}

	virtual ~TGLTFConverter() = default;

	OutputType Get(InputTypes&&... Inputs) const
	{
		const InputKeyType InputKey(Forward<InputTypes>(Inputs)...);
		if (OutputType* SavedOutput = SavedOutputs.Find(InputKey))
		{
			return *SavedOutput;
		}

		return {};
	}

	OutputType Add(InputTypes... Inputs)
	{
		const InputKeyType InputKey(Forward<InputTypes>(Inputs)...);
		OutputType NewOutput = Convert(Forward<InputTypes>(Inputs)...);

		SavedOutputs.Add(InputKey, NewOutput);
		return NewOutput;
	}

	OutputType GetOrAdd(InputTypes... Inputs)
	{
		const InputKeyType InputKey(Forward<InputTypes>(Inputs)...);
		if (OutputType* SavedOutput = SavedOutputs.Find(InputKey))
		{
			return *SavedOutput;
		}

		OutputType NewOutput = Convert(Forward<InputTypes>(Inputs)...);

		SavedOutputs.Add(InputKey, NewOutput);
		return NewOutput;
	}

protected:

	virtual OutputType Convert(InputTypes... Inputs) = 0;

	FGLTFConvertBuilder& Builder;

private:

	TMap<InputKeyType, OutputType> SavedOutputs;
};
