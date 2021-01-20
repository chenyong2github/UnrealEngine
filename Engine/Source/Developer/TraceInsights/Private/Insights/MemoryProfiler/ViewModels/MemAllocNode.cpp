// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocNode.h"

#define LOCTEXT_NAMESPACE "MemAllocNode"

namespace Insights
{

const FName FMemAllocNode::TypeName(TEXT("FMemAllocNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemAllocNode::GetFullCallstack() const
{
	if (IsValidMemAlloc())
	{
		return GetMemAllocChecked().GetFullCallstack();
	}

	return FText();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
