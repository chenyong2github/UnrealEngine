// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Logging/LogMacros.h"
#include "Trace/Private/Field.h"

#include <memory.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
class TRACEANALYSIS_API IAnalyzer
{
public:
	struct FSessionContext
	{
		uint64 StartCycle;
		uint64 CycleFrequency;

		double TimestampFromCycle(uint64 Cycle) const
		{
			return double(Cycle - StartCycle) / double(CycleFrequency);
		}

		double DurationFromCycleCount(uint64 CycleCount) const
		{
			return double(CycleCount) / double(CycleFrequency);
		}
	};

	struct FInterfaceBuilder
	{
		virtual void			RouteEvent(uint16 RouteId, const ANSICHAR* Logger, const ANSICHAR* Event) = 0;
	};

	struct FOnAnalysisContext
	{
		const FSessionContext&	SessionContext;
		FInterfaceBuilder&		InterfaceBuilder;
	};

	struct FValue
	{
		template <typename AsType>
		AsType As() const;
	};

	struct FArray
	{
	};

	struct FEventData
	{
		virtual const FValue&	GetValue(const ANSICHAR* FieldName) const = 0;
		virtual const FArray&	GetArray(const ANSICHAR* FieldName) const = 0;
		virtual const uint8*	GetData() const = 0;
		virtual const uint8*	GetAttachment() const = 0;
		virtual uint16			GetAttachmentSize() const = 0;
		virtual uint16			GetTotalSize() const = 0;
	};

	struct FOnEventContext
	{
		const FSessionContext&	SessionContext;
		const FEventData&		EventData;
	};

	virtual				~IAnalyzer() = default;
	virtual void		OnAnalysisBegin(const FOnAnalysisContext& Context) = 0;
	virtual void		OnAnalysisEnd() = 0;
	virtual void		OnEvent(uint16 RouteId, const FOnEventContext& Context) = 0;
};

////////////////////////////////////////////////////////////////////////////////
template <typename AsType>
AsType IAnalyzer::FValue::As() const
{
	uint16 FieldType = uint16(UPTRINT(this) >> 48);
	if (FieldType == 0xffff)
	{
		return AsType(0);
	}

	check((FieldType & _Field_Float) == 0);

	const void* Addr = (void*)(UPTRINT(this) & ((1ull << 48) - 1));
	UPTRINT Value = 0;
	uint32 Size = 1 << (FieldType & _Field_Pow2SizeMask);
	memcpy(&Value, Addr, Size);

	return AsType(Value);
}

} // namespace Trace
