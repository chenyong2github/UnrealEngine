// Copyright Epic Games, Inc. All Rights Reserved.

#include "CborWriter.h"
#include "Serialization/MemoryWriter.h"
#include "Templates/UniquePtr.h"
#include "Trace/Analyzer.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
void SerializeToCborImpl(
	TArray<uint8>& Out,
	const IAnalyzer::FEventData& EventData,
	uint32 EventSize)
{
	/* All this look up of fields is a bit slow. It'd be better to use the internals
	 * of the analysis engine instead */

	const IAnalyzer::FEventTypeInfo& TypeInfo = EventData.GetTypeInfo();

	uint32 SizeHint = ((EventSize * 11) / 10); // + 10%
	SizeHint += TypeInfo.GetFieldCount() * 16;
	SizeHint += Out.Num();

	Out.Reserve(SizeHint);
	FMemoryWriter MemoryWriter(Out, false, true);
	FCborWriter CborWriter(&MemoryWriter, ECborEndianness::StandardCompliant);

	CborWriter.WriteContainerStart(ECborCode::Map, TypeInfo.GetFieldCount());
	for (uint32 i = 0, n = TypeInfo.GetFieldCount(); i < n; ++i)
	{
		const IAnalyzer::FEventFieldInfo& FieldInfo = *(TypeInfo.GetFieldInfo(i));

		const ANSICHAR* FieldName = FieldInfo.GetName();
		CborWriter.WriteValue(FieldName, FCStringAnsi::Strlen(FieldName));

		if (FieldInfo.GetType() == IAnalyzer::FEventFieldInfo::EType::AnsiString)
		{
			FAnsiStringView View;
			EventData.GetString(FieldName, View);
			CborWriter.WriteValue((const char*)(View.GetData()), View.Len());
			continue;
		}

		if (FieldInfo.GetType() == IAnalyzer::FEventFieldInfo::EType::WideString)
		{
			FString String;
			EventData.GetString(FieldName, String);
			CborWriter.WriteValue(String);
			continue;
		}

		if (FieldInfo.IsArray())
		{
			if (FieldInfo.GetType() == IAnalyzer::FEventFieldInfo::EType::Integer)
			{
				const IAnalyzer::TArrayReader<uint64>& Reader = EventData.GetArray<uint64>(FieldName);
				if (uint32 Num = Reader.Num())
				{
					CborWriter.WriteContainerStart(ECborCode::Array, Reader.Num());
					for (uint32 j = 0; j < Num; ++j)
					{
						uint64 Value = Reader[j];
						CborWriter.WriteValue(Value);
					}
				}
				else
				{
					CborWriter.WriteNull();
				}
			}
			else
			{
				const IAnalyzer::TArrayReader<double>& Reader = EventData.GetArray<double>(FieldName);
				if (uint32 Num = Reader.Num())
				{
					CborWriter.WriteContainerStart(ECborCode::Array, Reader.Num());
					for (uint32 j = 0; j < Num; ++j)
					{
						double Value = Reader[j];
						CborWriter.WriteValue(Value);
					}
				}
				else
				{
					CborWriter.WriteNull();
				}
			}
			continue;
		}

		if (FieldInfo.GetType() == IAnalyzer::FEventFieldInfo::EType::Integer)
		{
			uint64 Value = EventData.GetValue<uint64>(FieldName);
			CborWriter.WriteValue(Value);
			continue;
		}

		if (FieldInfo.GetType() == IAnalyzer::FEventFieldInfo::EType::Float)
		{
			double Value = EventData.GetValue<double>(FieldName);
			CborWriter.WriteValue(Value);
			continue;
		}

		// No suitable value type was added if we get here.
		CborWriter.WriteNull();
	}
}

} // namespace Trace
