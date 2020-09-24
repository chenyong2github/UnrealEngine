// Copyright Epic Games, Inc. All Rights Reserved.
#include "PlatformFileTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "Model/FileActivity.h"

#define DEBUG_PLATFORMFILETRACE 0

#if DEBUG_PLATFORMFILETRACE
#define PLATFORMFILETRACE_WARNING(x) ensureMsgf(false, TEXT(x))
#else
#define PLATFORMFILETRACE_WARNING(x)
#endif

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

bool FPlatformFileTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_BeginOpen:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		#if DEBUG_PLATFORMFILETRACE
		if (PendingOpenMap.Contains(ThreadId))
		{
			PLATFORMFILETRACE_WARNING("Duplicated BeginOpen event!?");
		}
		#endif
		const TCHAR* FileName = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		uint32 FileIndex = FileActivityProvider.GetFileIndex(FileName);
		FPendingActivity& Open = PendingOpenMap.Add(ThreadId);
		Open.ActivityIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Open, ThreadId, 0, 0, Time);
		Open.FileIndex = FileIndex;
		break;
	}
	case RouteId_EndOpen:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint64 FileHandle = EventData.GetValue<uint64>("FileHandle");
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		const FPendingActivity* FindOpen = PendingOpenMap.Find(ThreadId);
		if (FindOpen)
		{
			if (FileHandle == uint64(-1)) // TODO: Portable invalid file handle?
			{
				FileActivityProvider.EndActivity(FindOpen->FileIndex, FindOpen->ActivityIndex, 0, Time, true);
			}
			else
			{
				OpenFilesMap.Add(FileHandle, FindOpen->FileIndex);
				FileActivityProvider.EndActivity(FindOpen->FileIndex, FindOpen->ActivityIndex, 0, Time, false);
			}
			PendingOpenMap.Remove(ThreadId);
		}
		else
		{
			PLATFORMFILETRACE_WARNING("BeginOpen event not traced!?");
		}
		break;
	}
	case RouteId_BeginClose:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint64 FileHandle = EventData.GetValue<uint64>("FileHandle");
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		#if DEBUG_PLATFORMFILETRACE
		if (PendingCloseMap.Contains(ThreadId))
		{
			PLATFORMFILETRACE_WARNING("Duplicated BeginClose event!?");
		}
		#endif
		const uint32* FindFileIndex = OpenFilesMap.Find(FileHandle);
		if (FindFileIndex)
		{
			OpenFilesMap.Remove(FileHandle);
			FPendingActivity& Close = PendingCloseMap.Add(ThreadId);
			Close.ActivityIndex = FileActivityProvider.BeginActivity(*FindFileIndex, Trace::FileActivityType_Close, ThreadId, 0, 0, Time);
			Close.FileIndex = *FindFileIndex;
		}
		else
		{
			PLATFORMFILETRACE_WARNING("File is not open!?");
		}
		break;
	}
	case RouteId_EndClose:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		const FPendingActivity* FindClose = PendingCloseMap.Find(ThreadId);
		if (FindClose)
		{
			FileActivityProvider.EndActivity(FindClose->FileIndex, FindClose->ActivityIndex, 0, Time, false);
			PendingCloseMap.Remove(ThreadId);
		}
		else
		{
			PLATFORMFILETRACE_WARNING("BeginClose event not traced!?");
		}
		break;
	}
	case RouteId_BeginRead:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint64 ReadHandle = EventData.GetValue<uint64>("ReadHandle");
		uint64 FileHandle = EventData.GetValue<uint64>("FileHandle");
		uint64 Offset = EventData.GetValue<uint64>("Offset");
		uint64 Size = EventData.GetValue<uint64>("Size");
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		const uint32* FindFileIndex = OpenFilesMap.Find(FileHandle);
		uint32 FileIndex;
		if (FindFileIndex)
		{
			FileIndex = *FindFileIndex;
		}
		else
		{
			FileIndex = FileActivityProvider.GetUnknownFileIndex();
			OpenFilesMap.Add(FileHandle, FileIndex);
		}
		uint64 ReadIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Read, ThreadId, Offset, Size, Time);
		FPendingActivity& Read = ActiveReadsMap.Add(ReadHandle);
		Read.FileIndex = FileIndex;
		Read.ActivityIndex = ReadIndex;
		break;
	}
	case RouteId_EndRead:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint64 ReadHandle = EventData.GetValue<uint64>("ReadHandle");
		uint64 SizeRead = EventData.GetValue<uint64>("SizeRead");
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		const FPendingActivity* FindRead = ActiveReadsMap.Find(ReadHandle);
		if (FindRead)
		{
			FileActivityProvider.EndActivity(FindRead->FileIndex, FindRead->ActivityIndex, SizeRead, Time, false);
			ActiveReadsMap.Remove(ReadHandle);
		}
		else
		{
			PLATFORMFILETRACE_WARNING("BeginRead event not traced!?");
		}
		break;
	}
	case RouteId_BeginWrite:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint64 WriteHandle = EventData.GetValue<uint64>("WriteHandle");
		uint64 FileHandle = EventData.GetValue<uint64>("FileHandle");
		uint64 Offset = EventData.GetValue<uint64>("Offset");
		uint64 Size = EventData.GetValue<uint64>("Size");
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		const uint32* FindFileIndex = OpenFilesMap.Find(FileHandle);
		uint32 FileIndex;
		if (FindFileIndex)
		{
			FileIndex = *FindFileIndex;
		}
		else
		{
			FileIndex = FileActivityProvider.GetUnknownFileIndex();
			OpenFilesMap.Add(FileHandle, FileIndex);
		}
		uint64 WriteIndex = FileActivityProvider.BeginActivity(FileIndex, Trace::FileActivityType_Write, ThreadId, Offset, Size, Time);
		FPendingActivity& Write = ActiveWritesMap.Add(WriteHandle);
		Write.FileIndex = FileIndex;
		Write.ActivityIndex = WriteIndex;
		break;
	}
	case RouteId_EndWrite:
	{
		double Time = Context.EventTime.AsSeconds(EventData.GetValue<uint64>("Cycle"));
		Session.UpdateDurationSeconds(Time);
		uint64 WriteHandle = EventData.GetValue<uint64>("WriteHandle");
		uint64 SizeWritten = EventData.GetValue<uint64>("SizeWritten");
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		const FPendingActivity* FindWrite = ActiveWritesMap.Find(WriteHandle);
		if (FindWrite)
		{
			FileActivityProvider.EndActivity(FindWrite->FileIndex, FindWrite->ActivityIndex, SizeWritten, Time, false);
			ActiveWritesMap.Remove(WriteHandle);
		}
		else
		{
			PLATFORMFILETRACE_WARNING("BeginWrite event not traced!?");
		}
		break;
	}
	}

	return true;
}

#undef PLATFORMFILETRACE_WARNING
#undef DEBUG_PLATFORMFILETRACE
