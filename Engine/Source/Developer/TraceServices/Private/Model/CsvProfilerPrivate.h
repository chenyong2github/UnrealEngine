// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/CsvProfiler.h"
#include "TraceServices/Containers/Tables.h"
#include "Containers/Map.h"

namespace Trace
{

enum ECsvStatSeriesType
{
	CsvStatSeriesType_Timer,
	CsvStatSeriesType_ExclusiveTimer,
	CsvStatSeriesType_CustomStatInt,
	CsvStatSeriesType_CustomStatFloat
};

enum ECsvOpType
{
	CsvOpType_Set,
	CsvOpType_Min,
	CsvOpType_Max,
	CsvOpType_Accumulate,
};

class FCsvProfilerProvider
	: public ICsvProfilerProvider
{
public:
	FCsvProfilerProvider(IAnalysisSession& InSession);
	virtual ~FCsvProfilerProvider();
	virtual const IUntypedTable& GetTable(uint32 CaptureIndex) const override { return Captures[CaptureIndex]->Table; }
	virtual void EnumerateCaptures(TFunctionRef<void(const FCaptureInfo&)> Callback) const override;
	void StartCapture(const TCHAR* Filename, int32 FrameNumber);
	void EndCapture(int32 FrameNumber);
	uint64 AddSeries(const TCHAR* Name, ECsvStatSeriesType Type);
	void SetTimerValue(uint64 SeriesHandle, int32 FrameNumber, double ElapsedTime);
	void SetCustomStatValue(uint64 SeriesHandle, int32 FrameNumber, ECsvOpType OpType, int32 Value);
	void SetCustomStatValue(uint64 SeriesHandle, int32 FrameNumber, ECsvOpType OpType, float Value);
	void AddEvent(int32 FrameNumber, const TCHAR* Text);
	void SetMetadata(const TCHAR* Key, const TCHAR* Value);

private:
	struct FStatSeriesValue
	{
		FStatSeriesValue() { Value.AsInt = 0; }
		union
		{
			int64 AsInt;
			double AsDouble;
		} Value;
		bool bIsValid = false;
	};

	struct FStatSeries
	{
		TMap<uint64, FStatSeriesValue> Values;
		TSet<uint32> Captures;
		const TCHAR* Name = nullptr;
		ECsvStatSeriesType Type;
	};

	struct FEvents
	{
		TArray<const TCHAR*> Events;
		const TCHAR* SemiColonSeparatedEvents = nullptr;
	};

	struct FCapture;

	class FTableLayout
		: public ITableLayout
	{
	public:
		FTableLayout(const TArray<FStatSeries*>& InStatSeries)
			: StatSeries(InStatSeries)
		{

		}

		virtual uint64 GetColumnCount() const override;
		virtual const TCHAR* GetColumnName(uint64 ColumnIndex) const override;
		virtual ETableColumnType GetColumnType(uint64 ColumnIndex) const override;

	private:
		const TArray<FStatSeries*>& StatSeries;
	};

	class FTableReader
		: public IUntypedTableReader
	{
	public:
		FTableReader(const FCapture& InCapture, const TMap<int32, FEvents*>& InEvents)
			: Capture(InCapture)
			, Events(InEvents)
			, CurrentRowIndex(0)
		{
		}

		virtual bool IsValid() const override;
		virtual void NextRow() override;
		virtual void SetRowIndex(uint64 RowIndex) override;
		virtual bool GetValueBool(uint64 ColumnIndex) const override;
		virtual int64 GetValueInt(uint64 ColumnIndex) const override;
		virtual float GetValueFloat(uint64 ColumnIndex) const override;
		virtual double GetValueDouble(uint64 ColumnIndex) const override;
		virtual const TCHAR* GetValueCString(uint64 ColumnIndex) const override;

	private:
		const FStatSeriesValue* GetValue(uint64 ColumnIndex) const;

		const FCapture& Capture;
		const TMap<int32, FEvents*>& Events;
		uint64 CurrentRowIndex;
	};

	class FTable
		: public IUntypedTable
	{
	public:
		FTable(const FCapture& InCapture, const TMap<int32, FEvents*>& InEvents)
			: Layout(InCapture.StatSeries)
			, Capture(InCapture)
			, Events(InEvents)
		{

		}

		virtual const ITableLayout& GetLayout() const override { return Layout; }
		virtual uint64 GetRowCount() const override;
		virtual IUntypedTableReader* CreateReader() const override;

	private:
		FTableLayout Layout;
		const FCapture& Capture;
		const TMap<int32, FEvents*>& Events;
	};

	struct FCapture
	{
		FCapture(const TMap<int32, FEvents*>& Events)
			: Table(*this, Events)
		{
			
		}

		FTable Table;
		const TCHAR* Filename;
		int32 StartFrame = -1;
		int32 EndFrame = -1;
		TArray<FStatSeries*> StatSeries;
		TMap<const TCHAR*, const TCHAR*> Metadata;
	};

	FStatSeriesValue& GetValueRef(uint64 SeriesHandle, int32 FrameNumber);

	IAnalysisSession& Session;
	TArray<FStatSeries*> StatSeries;
	TMap<int32, FEvents*> Events;
	TMap<const TCHAR*, const TCHAR*> Metadata;
	TArray<FCapture*> Captures;
	FCapture* CurrentCapture = nullptr;
};

}