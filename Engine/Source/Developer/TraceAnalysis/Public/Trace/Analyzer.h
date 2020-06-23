// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Logging/LogMacros.h"
#include "Trace/Detail/Field.h"

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
	struct FInterfaceBuilder
	{
		/** Subscribe to an event required for analysis.
		 * @param RouteId User-provided identifier for this event subscription.
		 * @param Logger Name of the logger that emits the event.
		 * @param Event Name of the event to subscribe to.
		 * @param bScoped Route scoped events. */
		virtual void RouteEvent(uint16 RouteId, const ANSICHAR* Logger, const ANSICHAR* Event, bool bScoped=false) = 0;

		/** Subscribe to all events from a particular logger.
		 * @param RouteId User-provided identifier for this event subscription.
		 * @param Logger Name of the logger that emits the event.
		 * @param bScoped Route scoped events. */
		virtual void RouteLoggerEvents(uint16 RouteId, const ANSICHAR* Logger, bool bScoped=false) = 0;

		/** Subscribe to all events in the trace stream being analyzed.
		 * @param RouteId User-provided identifier for this event subscription.
		 * @param bScoped Route scoped events. */
		virtual void RouteAllEvents(uint16 RouteId, bool bScoped=false) = 0;
	};

	struct FOnAnalysisContext
	{
		FInterfaceBuilder& InterfaceBuilder;
	};

	struct TRACEANALYSIS_API FEventFieldInfo
	{
		enum class EType { None, Integer, Float, AnsiString, WideString };

		/** Returns the name of the field. */
		const ANSICHAR* GetName() const;

		/** What type of field is this? */
		EType GetType() const;

		/** Is this field an array-type field? */
		bool IsArray() const;
	};

	struct TRACEANALYSIS_API FEventTypeInfo
	{
		/** Each event is assigned a unique ID when logged. Not that this is not
		 * guaranteed to be the same for the same event from one trace to the next. */
		uint32 GetId() const;

		/** The name of the event. */
		const ANSICHAR* GetName() const;

		/** Returns the logger name the event is associated with. */
		const ANSICHAR* GetLoggerName() const;

		/** The number of member fields this event has. */
		uint32 GetFieldCount() const;

		/** By-index access to fields' type information. */
		const FEventFieldInfo* GetFieldInfo(uint32 Index) const;
	};

	struct TRACEANALYSIS_API FArrayReader
	{
		/* Returns the number of elements in the array */
		uint32 Num() const;

	protected:
		const void* GetImpl(uint32 Index, int16& SizeAndType) const;
	};

	template <typename ValueType>
	struct TArrayReader
		: public FArrayReader
	{
		/* Returns a element from the array an the given index or zero if Index
		 * is out of bounds. */
		ValueType operator [] (uint32 Index) const;

		/** Get a pointer to the contiguous array data */
		const ValueType* GetData() const;
	};

	struct TRACEANALYSIS_API FEventData
	{
		/** Returns an object describing the underlying event's type. */
		const FEventTypeInfo& GetTypeInfo() const;

		/** Queries the value of a field of the event. It is not necessary to match
		 * ValueType to the type in the event.
		 * @param FieldName The name of the event's field to get the value for.
		 * @param Default Return this value if the given field was not found.
		 * @return Value of the field (coerced to ValueType) if found, otherwise 0. */
		template <typename ValueType> ValueType GetValue(const ANSICHAR* FieldName, ValueType Default=ValueType(0)) const;

		/** Returns an object for reading data from an array-type field. A valid
		 * array reader object will always be return even if no field matching the
		 * given name was found.
		 * @param FieldName The name of the event's field to get the value for. */
		template <typename ValueType> const TArrayReader<ValueType>& GetArray(const ANSICHAR* FieldName) const;

		/** Returns an array view for reading data from an array-type field. A valid
		 * array view will always be returned even if no field matching the
		 * given name was found.
		 * @param FieldName The name of the event's field to get the value for. */
		template <typename ValueType> TArrayView<const ValueType> GetArrayView(const ANSICHAR* FieldName) const;

		/** Return the value of a string-type field. The view-type prototypes
		 * must match the underlying string type while the FString-variant is
		 * agnostic of the field's encoding.
		  * @param Out Destination object for the field's value.
		  * @return True if the field was found. */
		bool GetString(const ANSICHAR* FieldName, FAnsiStringView& Out) const;
		bool GetString(const ANSICHAR* FieldName, FStringView& Out) const;
		bool GetString(const ANSICHAR* FieldName, FString& Out) const;

		/** Serializes the event to Cbor object.
		 * @param Recipient of the Cbor serialization. Data is appeneded to Out. */
		void SerializeToCbor(TArray<uint8>& Out) const;

		/** Returns the event's attachment. Not that this will always return an
		 * address but if the event has no attachment then reading from that
		 * address if undefined. */
		const uint8* GetAttachment() const;

		/** Returns the size of the events attachment, or 0 if none. */
		uint32 GetAttachmentSize() const;

	private:
		const void* GetValueImpl(const ANSICHAR* FieldName, int16& SizeAndType) const;
		const FArrayReader* GetArrayImpl(const ANSICHAR* FieldName) const;
	};

	struct TRACEANALYSIS_API FThreadInfo
	{
		/* Returns the trace-specific id for the thread */
		uint32 GetId() const;

		/* Returns the system if for the thread. Because this may not be known by
		 * trace and because IDs can be reused by the system, relying on the value
		 * of this is discouraged. */
		uint32 GetSystemId() const;

		/* Returns a hint for use when sorting threads. */
		int32 GetSortHint() const;

		/* Returns the thread's name or an empty string */
		const ANSICHAR* GetName() const;

		/* Returns the name of the group a thread has been assigned ti, or an empty string */
		const ANSICHAR* GetGroupName() const;
	};

	struct TRACEANALYSIS_API FEventTime
	{
		/** Returns the integer timestamp for the event or zero if there no associated timestamp. */
		uint64 GetTimestamp() const;

		/** Time of the event in seconds (from teh start of the trace). Zero if there is no time for the event. */
		double AsSeconds() const;

		/** Returns a timestamp for the event compatible with FPlatformTime::Cycle64(), or zero if the event has no timestamp. */
		uint64 AsCycle64() const;

		/** Returns a FPlatformTime::Cycle64() value as seconds relative to the start of the trace. */
		double AsSeconds(uint64 Cycles64) const;

		/** As AsSeconds(Cycles64) but absolute. */
		double AsSecondsAbsolute(int64 DurationCycles64) const;
	};

	struct FOnEventContext
	{
		const FThreadInfo&	ThreadInfo;
		const FEventTime&	EventTime;
		const FEventData&	EventData;
	};

	virtual ~IAnalyzer() = default;

	/** Called when analysis of a trace is beginning. Analyzer implementers can
	 * subscribe to the events that they are interested in at this point
	 * @param Context Contextual information and interface for subscribing to events. */
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context)
	{
	}

	/** Indicates that the analysis of a trace log has completed and there are no
	 * further events */
	virtual void OnAnalysisEnd()
	{
	}

	/** Called when information about a thread has been updated. It is entirely
	 * possible that this might get called more than once for a particular thread
	 * if its details changed.
	 * @param ThreadInfo Describes the thread whose information has changed. */
	virtual void OnThreadInfo(const FThreadInfo& ThreadInfo)
	{
	}

	/** When a new event type appears in the trace stream, this method is called
	 * if the event type has been subscribed to.
	 * @param RouteId User-provided identifier for this event subscription.
	 * @param TypeInfo Object describing the new event's type.
	 * @return This analyzer is removed from the analysis session if false is returned. */
	virtual bool OnNewEvent(uint16 RouteId, const FEventTypeInfo& TypeInfo)
	{
		return true;
	}

	enum class EStyle : uint32
	{
		Normal,
		EnterScope,
		LeaveScope,
	};

	/** For each event subscribed to in OnAnalysisBegin(), the analysis engine
	 * will call this method when those events are encountered in a trace log
	 * @param RouteId User-provided identifier given when subscribing to a particular event.
	 * @param Style Indicates the style of event. Note that EventData is *undefined* if the style is LeaveScope!
	 * @param Context Access to the instance of the subscribed event.
	 * @return This analyzer is removed from the analysis session if false is returned. */
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
	{
		return true;
	}

private:
	template <typename ValueType> static ValueType CoerceValue(const void* Addr, int16 SizeAndType);
};

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
ValueType IAnalyzer::CoerceValue(const void* Addr, int16 SizeAndType)
{
	switch (SizeAndType)
	{
	case -4: return ValueType(*(const float*)(Addr));
	case -8: return ValueType(*(const double*)(Addr));
	case  1: return ValueType(*(const uint8*)(Addr));
	case  2: return ValueType(*(const uint16*)(Addr));
	case  4: return ValueType(*(const uint32*)(Addr));
	case  8: return ValueType(*(const uint64*)(Addr));
	}

	return ValueType(0);
}

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
ValueType IAnalyzer::FEventData::GetValue(const ANSICHAR* FieldName, ValueType Default) const
{
	int16 FieldSizeAndType;
	if (const void* Addr = GetValueImpl(FieldName, FieldSizeAndType))
	{
		return CoerceValue<ValueType>(Addr, FieldSizeAndType);
	}
	return Default;
}

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
const IAnalyzer::TArrayReader<ValueType>& IAnalyzer::FEventData::GetArray(const ANSICHAR* FieldName) const
{
	const FArrayReader* Base = GetArrayImpl(FieldName);
	return *(TArrayReader<ValueType>*)(Base);
}

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
TArrayView<const ValueType> IAnalyzer::FEventData::GetArrayView(const ANSICHAR* FieldName) const
{
	const TArrayReader<ValueType>& ArrayReader = GetArray<ValueType>(FieldName);
	return TArrayView<const ValueType>(ArrayReader.GetData(), ArrayReader.Num());
}

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
ValueType IAnalyzer::TArrayReader<ValueType>::operator [] (uint32 Index) const
{
	int16 ElementSizeAndType;
	if (const void* Addr = GetImpl(Index, ElementSizeAndType))
	{
		return CoerceValue<ValueType>(Addr, ElementSizeAndType);
	}
	return ValueType(0);
}

////////////////////////////////////////////////////////////////////////////////
template <typename ValueType>
const ValueType* IAnalyzer::TArrayReader<ValueType>::GetData() const
{
	int16 ElementSizeAndType;
	const void* Addr = GetImpl(0, ElementSizeAndType);

	if (Addr == nullptr || sizeof(ValueType) != abs(ElementSizeAndType))
	{
		return nullptr;
	}

	return (const ValueType*)Addr;
}

} // namespace Trace
