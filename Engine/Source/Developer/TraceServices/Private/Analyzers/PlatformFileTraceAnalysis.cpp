// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "PlatformFileTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/FileActivity.h"

FPlatformFileTraceAnalyzer::FPlatformFileTraceAnalyzer(Trace::IAnalysisSession& InSession, Trace::FFileActivityProvider& InFileActivityProvider)
	: Session(InSession)
	, FileActivityProvider(InFileActivityProvider)
{

}

void FPlatformFileTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_BeginOpen, "PlatformFile", "BeginOpen");
	Builder.RouteEvent(RouteId_EndOpen, "PlatformFile", "EndOpen");
	Builder.RouteEvent(RouteId_BeginClose, "PlatformFile", "BeginClose");
	Builder.RouteEvent(RouteId_EndClose, "PlatformFile", "EndClose");
	Builder.RouteEvent(RouteId_BeginRead, "PlatformFile", "BeginRead");
	Builder.RouteEvent(RouteId_EndRead, "PlatformFile", "EndRead");
	Builder.RouteEvent(RouteId_BeginWrite, "PlatformFile", "BeginWrite");
	Builder.RouteEvent(RouteId_EndWrite, "PlatformFile", "EndWrite");
}

void FPlatformFileTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_BeginOpen:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 TempHandle = EventData.GetValue("TempHandle").As<uint64>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		check(!PendingOpenMap.Contains(TempHandle));
		uint32 FileIndex = FileActivityProvider.GetFileIndex(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		FPendingActivity& Open = PendingOpenMap.Add(TempHandle);
		Open.ActivityIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Open, 0, 0, Time);
		Open.FileIndex = FileIndex;
		break;
	}
	case RouteId_EndOpen:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 TempHandle = EventData.GetValue("TempHandle").As<uint64>();
		uint64 FileHandle = EventData.GetValue("FileHandle").As<uint64>();
		check(PendingOpenMap.Contains(TempHandle));
		FPendingActivity& Open = PendingOpenMap[TempHandle];
		if (FileHandle == uint64(-1)) // TODO: Portable invalid file handle?
		{
			FileActivityProvider.EndActivity(Open.FileIndex, Open.ActivityIndex, Time, true);
		}
		else
		{
			check(!OpenFilesMap.Contains(FileHandle));
			OpenFilesMap.Add(FileHandle, Open.FileIndex);
			FileActivityProvider.EndActivity(Open.FileIndex, Open.ActivityIndex, Time, false);
		}
		PendingOpenMap.Remove(TempHandle);
		break;
	}
	case RouteId_BeginClose:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 TempHandle = EventData.GetValue("TempHandle").As<uint64>();
		uint64 FileHandle = EventData.GetValue("FileHandle").As<uint64>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		check(OpenFilesMap.Contains(FileHandle));
		check(!PendingCloseMap.Contains(TempHandle));
		uint64 FileIndex = OpenFilesMap[FileHandle];
		OpenFilesMap.Remove(FileHandle);
		FPendingActivity& Close = PendingCloseMap.Add(TempHandle);
		Close.ActivityIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Close, 0, 0, Time);
		Close.FileIndex = FileIndex;
		break;
	}
	case RouteId_EndClose:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 TempHandle = EventData.GetValue("TempHandle").As<uint64>();
		check(PendingCloseMap.Contains(TempHandle));
		FPendingActivity& Close = PendingCloseMap[TempHandle];
		FileActivityProvider.EndActivity(Close.FileIndex, Close.ActivityIndex, Time, false);
		PendingCloseMap.Remove(TempHandle);
		break;
	}
	case RouteId_BeginRead:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 ReadHandle = EventData.GetValue("ReadHandle").As<uint64>();
		uint64 FileHandle = EventData.GetValue("FileHandle").As<uint64>();
		uint64 Offset = EventData.GetValue("Offset").As<uint64>();
		uint64 Size = EventData.GetValue("Size").As<uint64>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		check(OpenFilesMap.Contains(FileHandle));
		uint32 FileIndex = OpenFilesMap[FileHandle];
		uint64 ReadIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Read, Offset, Size, Time);
		FPendingActivity& Read = ActiveReadsMap.Add(ReadHandle);
		Read.FileIndex = FileIndex;
		Read.ActivityIndex = ReadIndex;
		break;
	}
	case RouteId_EndRead:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 ReadHandle = EventData.GetValue("ReadHandle").As<uint64>();
		uint64 SizeRead = EventData.GetValue("SizeRead").As<uint64>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		check(ActiveReadsMap.Contains(ReadHandle));
		const FPendingActivity& Read = ActiveReadsMap[ReadHandle];
		FileActivityProvider.EndActivity(Read.FileIndex, Read.ActivityIndex, Time, false);
		ActiveReadsMap.Remove(ReadHandle);
		break;
	}
	case RouteId_BeginWrite:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 WriteHandle = EventData.GetValue("WriteHandle").As<uint64>();
		uint64 FileHandle = EventData.GetValue("FileHandle").As<uint64>();
		uint64 Offset = EventData.GetValue("Offset").As<uint64>();
		uint64 Size = EventData.GetValue("Size").As<uint64>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		check(OpenFilesMap.Contains(FileHandle));
		uint32 FileIndex = OpenFilesMap[FileHandle];
		uint64 WriteIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Write, Offset, Size, Time);
		FPendingActivity& Write = ActiveWritesMap.Add(WriteHandle);
		Write.FileIndex = FileIndex;
		Write.ActivityIndex = WriteIndex;
		break;
	}
	case RouteId_EndWrite:
	{
		double Time = Context.SessionContext.TimestampFromCycle(EventData.GetValue("Cycle").As<uint64>());
		uint64 WriteHandle = EventData.GetValue("WriteHandle").As<uint64>();
		uint64 SizeWritten = EventData.GetValue("SizeWritten").As<uint64>();
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		check(ActiveWritesMap.Contains(WriteHandle));
		const FPendingActivity& Write = ActiveWritesMap[WriteHandle];
		FileActivityProvider.EndActivity(Write.FileIndex, Write.ActivityIndex, Time, false);
		ActiveWritesMap.Remove(WriteHandle);
		break;
	}
	}
}

void FPlatformFileTraceAnalyzer::OnAnalysisEnd()
{
}