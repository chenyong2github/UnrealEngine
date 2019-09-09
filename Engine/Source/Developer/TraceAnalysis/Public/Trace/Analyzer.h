// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Logging/LogMacros.h"
#include "Trace/Private/Field.h"

#include <memory.h>

namespace Trace
{

/**
 * Interface that users implement to analyze the events in a trace. Analysis
 * works by subscribing to events by name along with a user-provider "route"
 * identifier. The IAnalyzer then receives callbacks when those events are
 * encountered along with an interface to query the value of the event's fields.
 *
 * To analyze a trace, concrete IAnalyzer-derived objects are registered with a
 * FAnalysisContext which is then asked to launch and coordinate the analysis.
 */
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
		/** Subscribe to an event required for analysis.
		 * @param RouteId User-provided identifier for this event subscription.
		 * @param Logger Name of the logger that emits the event.
		 * @param Event Name of the event to subscribe to. */
		virtual void RouteEvent(uint16 RouteId, const ANSICHAR* Logger, const ANSICHAR* Event) = 0;
	};

	struct FOnAnalysisContext
	{
		const FSessionContext&	SessionContext;
		FInterfaceBuilder&		InterfaceBuilder;
	};

	struct TRACEANALYSIS_API FEventFieldInfo
	{
		enum class EType { None, Integer, Float, };

		/** Returns the name of the field. */
		const ANSICHAR* GetName() const;

		/** Offset from the start of the event to this field's data. */
		uint32 GetOffset() const;

		/** Returns the size of the field's data. */
		uint32 GetSize() const;

		/** What type of field is this? */
		EType GetType() const;
	};

	struct TRACEANALYSIS_API FEventTypeInfo
	{
		/** Each event is assigned a unique ID when logged. Not that this is not
		 * guaranteed to be the same for the same event from one trace to the next. */
		uint32 GetId() const;

		/** Returns the total size of the event. */
		uint32 GetSize() const;

		/** The name of the event. */
		const ANSICHAR* GetName() const;

		/** Returns the logger name the event is associated with. */
		const ANSICHAR* GetLoggerName() const;

		/** The number of member fields this event has. */
		uint32 GetFieldCount() const;

		/** By-index access to fields' type information. */
		const FEventFieldInfo* GetFieldInfo(uint32 Index) const;
	};

	struct TRACEANALYSIS_API FEventData
	{
		/** Returns an object describing the underlying event's type. */
		const FEventTypeInfo& GetTypeInfo() const;

		/** Queries the value of a field of the event. */
		template <typename ValueType> ValueType GetValue(const ANSICHAR* FieldName) const;

		/** Returns the event's attachment. Not that this will always return an
		 * address but if the event has no attachment then reading from that
		 * address if undefined. */
		const uint8* GetAttachment() const;

		/** Returns the size of the events attachment, or 0 if none. */
		uint32 GetAttachmentSize() const;

		/** Provides a pointer to the raw event data. */
		const uint8* GetRawPointer() const;

	private:
		const void* GetValueImpl(const ANSICHAR* FieldName, uint16& Type) const;
	};

	struct FOnEventContext
	{
		const FSessionContext&	SessionContext;
		const FEventData&		EventData;
	};

	virtual ~IAnalyzer() = default;

	/** Called when analysis of a trace is beginning. Analyzer implementers can
	 * subscribe to the events that they are interested in at this point
	 * @param Context Contextual information and interface for subscribing to events. */
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) {}

	/** Indicates that the analysis of a trace log has completed and there are no
	 * further events */
	virtual void OnAnalysisEnd() {}

	/** When a new event type appears in the trace stream, this method is called
	 * if the event type has been subscribed to.
	 * @param RouteId User-provided identifier for this event subscription.
	 * @param TypeInfo Object describing the new event's type. */
	virtual void OnNewEvent(uint16 RouteId, const FEventTypeInfo& TypeInfo) {}

	/** For each event subscribed to in OnAnalysisBegin(), the analysis engine
	 * will call this method when those events are encountered in a trace log
	 * @param RouteId User-provided identifier given when subscribing to a particular event
	 * @param Context Access to the instance of the subscribed event */
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) {}
};

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
ValueType IAnalyzer::FEventData::GetValue(const ANSICHAR* FieldName) const
{
	uint16 FieldTypeId;
	const void* Addr = GetValueImpl(FieldName, FieldTypeId);
	if (Addr == nullptr)
	{
		return ValueType(0);
	}

	uint32 Pow2Size = (FieldTypeId & _Field_Pow2SizeMask);
	uint32 Category = (FieldTypeId & _Field_CategoryMask);

	if (Category == _Field_Float)
	{
		switch (Pow2Size)
		{
		case _Field_32: return ValueType(*(const float*)(Addr));
		case _Field_64: return ValueType(*(const double*)(Addr));
		}
	}
	else if (Category == _Field_Integer)
	{
		switch (Pow2Size)
		{
		case _Field_8:  return ValueType(*(const uint8*)(Addr));
		case _Field_16: return ValueType(*(const uint16*)(Addr));
		case _Field_32: return ValueType(*(const uint32*)(Addr));
		case _Field_64: return ValueType(*(const uint64*)(Addr));
		}
	}

	return ValueType(0);
}

} // namespace Trace
