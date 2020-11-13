// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace TraceServices {
namespace FTraceAnalyzerUtils {

template <typename AttachedCharType>
inline FString LegacyAttachmentString(
	const ANSICHAR* FieldName,
	const Trace::IAnalyzer::FOnEventContext& Context)
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
