// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemAllocNode.h"

// Insights
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemorySharedState.h"
#include "Insights/MemoryProfiler/Widgets/SMemoryProfilerWindow.h"

#define LOCTEXT_NAMESPACE "MemAllocNode"

namespace Insights
{

const FName FMemAllocNode::TypeName(TEXT("FMemAllocNode"));

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FMemAllocNode::GetAllocMemTagAsText() const
{
	const FMemoryAlloc* Alloc = GetMemAlloc();
	if (Alloc)
	{
		return FText::FromString(GetAllocMemTagAsString(Alloc->GetMemTag()));
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FMemAllocNode::GetAllocMemTagAsString(FMemoryTagId InMemTag)
{
	TSharedPtr<SMemoryProfilerWindow> ProfilerWindow = FMemoryProfilerManager::Get()->GetProfilerWindow();
	if (ProfilerWindow)
	{
		auto& SharedState = ProfilerWindow->GetSharedState();
		const Insights::FMemoryTagList& TagList = SharedState.GetTagList();

		const FMemoryTag* Tag = TagList.GetTagById(InMemTag);
		if (Tag)
		{
			return Tag->GetStatName();
		}
	}
	return FString();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
