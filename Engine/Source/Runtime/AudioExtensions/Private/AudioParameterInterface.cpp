// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioParameterInterface.h"
#include "IAudioProxyInitializer.h"


namespace AudioParameterPrivate
{
	template <typename T>
	void SetOrMergeArray(const TArray<T>& InArray, TArray<T>& OutArray, bool bInMerge)
	{
		if (bInMerge)
		{
			OutArray.Append(InArray);
		}
		else
		{
			OutArray = InArray;
		}
	}
}

void FAudioParameter::Merge(const FAudioParameter& InParameter, bool bInTakeName, bool bInTakeType, bool bInMergeArrayTypes)
{
	if (bInTakeName)
	{
		ParamName = InParameter.ParamName;
	}

	if (bInTakeType)
	{
		ParamType = InParameter.ParamType;
	}

	switch (InParameter.ParamType)
	{
		case EAudioParameterType::Boolean:
		{
			BoolParam = InParameter.BoolParam;
		}
		break;

		case EAudioParameterType::BooleanArray:
		{
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayBoolParam, ArrayBoolParam, bInMergeArrayTypes);
		}
		break;

		case EAudioParameterType::Float:
		{
			FloatParam = InParameter.FloatParam;
		}
		break;

		case EAudioParameterType::FloatArray:
		{
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayFloatParam, ArrayFloatParam, bInMergeArrayTypes);
		}
		break;

		case EAudioParameterType::Integer:
		case EAudioParameterType::NoneArray:
		{
			if (bInMergeArrayTypes)
			{
				IntParam += InParameter.IntParam;
			}
			else
			{
				IntParam = InParameter.IntParam;
			}
		}
		break;

		case EAudioParameterType::IntegerArray:
		{
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayIntParam, ArrayIntParam, bInMergeArrayTypes);
		}
		break;

		case EAudioParameterType::None:
		{
			FloatParam = InParameter.FloatParam;
			BoolParam = InParameter.BoolParam;
			IntParam = InParameter.IntParam;
			ObjectParam = InParameter.ObjectParam;
			StringParam = InParameter.StringParam;

			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayBoolParam, ArrayBoolParam, bInMergeArrayTypes);
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayFloatParam, ArrayFloatParam, bInMergeArrayTypes);
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayIntParam, ArrayIntParam, bInMergeArrayTypes);
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayObjectParam, ArrayObjectParam, bInMergeArrayTypes);
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayStringParam, ArrayStringParam, bInMergeArrayTypes);

			if (!bInMergeArrayTypes)
			{
				ObjectProxies.Reset();
			}

			for (const Audio::IProxyDataPtr& ProxyPtr : InParameter.ObjectProxies)
			{
				if (ensure(ProxyPtr.IsValid()))
				{
					ObjectProxies.Emplace(ProxyPtr->Clone());
				}
			}
		}
		break;

		case EAudioParameterType::Object:
		{
			ObjectParam = InParameter.ObjectParam;

			ObjectProxies.Reset();
			for (const Audio::IProxyDataPtr& ProxyPtr : InParameter.ObjectProxies)
			{
				if (ensure(ProxyPtr.IsValid()))
				{
					ObjectProxies.Emplace(ProxyPtr->Clone());
				}
			}
		}
		break;

		case EAudioParameterType::ObjectArray:
		{
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayObjectParam, ArrayObjectParam, bInMergeArrayTypes);

			if (!bInMergeArrayTypes)
			{
				ObjectProxies.Reset();
			}

			for (const Audio::IProxyDataPtr& ProxyPtr : InParameter.ObjectProxies)
			{
				if (ensure(ProxyPtr.IsValid()))
				{
					ObjectProxies.Emplace(ProxyPtr->Clone());
				}
			}
		}
		break;

		case EAudioParameterType::String:
		{
			StringParam = InParameter.StringParam;
		}
		break;

		case EAudioParameterType::StringArray:
		{
			AudioParameterPrivate::SetOrMergeArray(InParameter.ArrayStringParam, ArrayStringParam, bInMergeArrayTypes);
		}
		break;

		default:
			break;
	}
}

void FAudioParameter::Merge(TArray<FAudioParameter>&& InParams, TArray<FAudioParameter>& OutParams)
{
	if (InParams.IsEmpty())
	{
		return;
	}

	if (OutParams.IsEmpty())
	{
		OutParams.Append(MoveTemp(InParams));
		return;
	}

	auto SortParamsPredicate = [](const FAudioParameter& A, const FAudioParameter& B) { return A.ParamName.FastLess(B.ParamName); };

	InParams.Sort(SortParamsPredicate);
	OutParams.Sort(SortParamsPredicate);

	for (int32 i = OutParams.Num() - 1; i >= 0; --i)
	{
		while (!InParams.IsEmpty())
		{
			FAudioParameter& OutParam = OutParams[i];
			if (InParams.Last().ParamName.FastLess(OutParam.ParamName))
			{
				break;
			}

			constexpr bool bAllowShrinking = false;
			FAudioParameter NewParam = InParams.Pop(bAllowShrinking);
			if (NewParam.ParamName == OutParam.ParamName)
			{
				NewParam.Merge(OutParam);
				OutParam = MoveTemp(NewParam);
			}
			else
			{
				OutParams.Emplace(MoveTemp(NewParam));
			}
		}
	}
}

UAudioParameterInterface::UAudioParameterInterface(FObjectInitializer const& InObjectInitializer)
	: UInterface(InObjectInitializer)
{
}
