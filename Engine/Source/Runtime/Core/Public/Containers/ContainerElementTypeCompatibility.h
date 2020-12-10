// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

template <typename InElementType>
struct TContainerElementTypeCompatibility
{
	typedef InElementType ReinterpretType;

	template <typename IterBeginType, typename IterEndType, typename OperatorType = InElementType&(*)(IterBeginType&)>
	static void ReinterpretRange(IterBeginType Iter, IterEndType IterEnd, OperatorType Operator = [](IterBeginType& InIt) -> InElementType& { return *InIt; })
	{
	}

	typedef InElementType CopyFromOtherType;

	static constexpr void CopyingFromOtherType() {}
};

template <typename ElementType>
struct TIsContainerElementTypeReinterpretable
{
	enum { Value = !TIsSame<typename TContainerElementTypeCompatibility<ElementType>::ReinterpretType, ElementType>::Value };
};

template <typename ElementType>
struct TIsContainerElementTypeCopyable
{
	enum { Value = !TIsSame<typename TContainerElementTypeCompatibility<ElementType>::CopyFromOtherType, ElementType>::Value };
};
