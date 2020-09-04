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
	Builder.RouteEvent(RouteId_World, "Object", "World");
	Builder.RouteEvent(RouteId_ClassPropertyStringId, "Object", "ClassPropertyStringId");
	Builder.RouteEvent(RouteId_ClassProperty, "Object", "ClassProperty");
	Builder.RouteEvent(RouteId_PropertiesStart, "Object", "PropertiesStart");
	Builder.RouteEvent(RouteId_PropertiesEnd, "Object", "PropertiesEnd");
	Builder.RouteEvent(RouteId_PropertyValue, "Object", "PropertyValue");
}

bool FGameplayAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_World:
	{
		uint64 Id = EventData.GetValue<uint64>("Id");
		int32 PIEInstanceId = EventData.GetValue<int32>("PIEInstanceId");
		uint8 Type = EventData.GetValue<uint8>("Type");
		uint8 NetMode = EventData.GetValue<uint8>("NetMode");
		bool bIsSimulating = EventData.GetValue<bool>("IsSimulating");
		GameplayProvider.AppendWorld(Id, PIEInstanceId, Type, NetMode, bIsSimulating);
		break;
	}
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
		GameplayProvider.AppendObjectEvent(Id, Context.EventTime.AsSeconds(Cycle), Event);
		break;
	}
	case RouteId_ClassPropertyStringId:
	{
		uint32 Id = EventData.GetValue<uint32>("Id");
		FStringView Value; EventData.GetString("Value", Value);
		GameplayProvider.AppendClassPropertyStringId(Id, Value);
		break;
	}
	case RouteId_ClassProperty:
	{
		uint64 ClassId = EventData.GetValue<uint64>("ClassId");
		int32 Id = EventData.GetValue<int32>("Id");
		int32 ParentId = EventData.GetValue<int32>("ParentId");
		uint32 TypeId = EventData.GetValue<uint32>("TypeId");
		uint32 KeyId = EventData.GetValue<uint32>("KeyId");
		GameplayProvider.AppendClassProperty(ClassId, Id, ParentId, TypeId, KeyId);
		break;
	}
	case RouteId_PropertiesStart:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 ObjectId = EventData.GetValue<uint64>("ObjectId");
		GameplayProvider.AppendPropertiesStart(ObjectId, Context.EventTime.AsSeconds(Cycle), Cycle);
		break;
	}
	case RouteId_PropertiesEnd:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 ObjectId = EventData.GetValue<uint64>("ObjectId");
		GameplayProvider.AppendPropertiesEnd(ObjectId, Context.EventTime.AsSeconds(Cycle));
		break;
	}
	case RouteId_PropertyValue:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint64 ObjectId = EventData.GetValue<uint64>("ObjectId");
		int32 PropertyId = EventData.GetValue<int32>("PropertyId");
		FStringView Value; EventData.GetString("Value", Value);
		GameplayProvider.AppendPropertyValue(ObjectId, Context.EventTime.AsSeconds(Cycle), Cycle, PropertyId, Value);
		break;
	}
	}

	return true;
}