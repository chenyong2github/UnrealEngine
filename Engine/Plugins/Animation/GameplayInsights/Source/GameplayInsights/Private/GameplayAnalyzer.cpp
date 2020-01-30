// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayAnalyzer.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplayProvider.h"

FGameplayAnalyzer::FGameplayAnalyzer(Trace::IAnalysisSession& InSession, FGameplayProvider& InGameplayProvider)
	: Session(InSession)
	, GameplayProvider(InGameplayProvider)
{
}

void FGameplayAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_Class, "Object", "Class");
	Builder.RouteEvent(RouteId_Object, "Object", "Object");
	Builder.RouteEvent(RouteId_ObjectEvent, "Object", "ObjectEvent");
}

bool FGameplayAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_Class:
	{
		const TCHAR* ClassNameAndPathName = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		const TCHAR* ClassName = ClassNameAndPathName;
		int32 ClassNameStringLength = EventData.GetValue<int32>("ClassNameStringLength");
		const TCHAR* ClassPathName = ClassNameAndPathName + ClassNameStringLength;
		uint64 Id = EventData.GetValue<uint64>("Id");
		uint64 SuperId = EventData.GetValue<uint64>("SuperId");
		GameplayProvider.AppendClass(Id, SuperId, ClassName, ClassPathName);
		break;
	}
	case RouteId_Object:
	{
		const TCHAR* ObjectNameAndPathName = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		const TCHAR* ObjectName = ObjectNameAndPathName;
		int32 NameStringLength = EventData.GetValue<int32>("ObjectNameStringLength");
		const TCHAR* ObjectPathName = ObjectNameAndPathName + NameStringLength;
		uint64 Id = EventData.GetValue<uint64>("Id");
		uint64 OuterId = EventData.GetValue<uint64>("OuterId");
		uint64 ClassId = EventData.GetValue<uint64>("ClassId");
		GameplayProvider.AppendObject(Id, OuterId, ClassId, ObjectName, ObjectPathName);
		break;
	}
	case RouteId_ObjectEvent:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 Id = EventData.GetValue<uint64>("Id");
		const TCHAR* Event = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		GameplayProvider.AppendObjectEvent(Id, Context.SessionContext.TimestampFromCycle(Cycle), Event);
		break;
	}
	}

	return true;
}