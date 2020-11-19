// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"

namespace TraceServices {
namespace FTraceAnalyzerUtils {

template <typename AttachedCharType>
inline FString LegacyAttachmentString(
	const ANSICHAR* FieldName,
	const UE::Trace::IAnalyzer::FOnEventContext& Context)
{
	FString Out;
	if (!Context.EventData.GetString(FieldName, Out))
	{
		Out = FString(
				Context.EventData.GetAttachmentSize() / sizeof(AttachedCharType),
				(const AttachedCharType*)(Context.EventData.GetAttachment()));
	}
	return Out;
}

} // namespace FTraceAnalyzerUtils
} // namespace TraceServices
