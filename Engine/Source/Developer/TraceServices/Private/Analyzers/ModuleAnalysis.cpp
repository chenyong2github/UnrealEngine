// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModuleAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Model/ModuleProvider.h"
#include "UObject/NameTypes.h"

namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
enum Routes
{
	RouteId_ModuleInit,
	RouteId_ModuleLoad,
	RouteId_ModuleUnload,
};

////////////////////////////////////////////////////////////////////////////////
FModuleAnalyzer::FModuleAnalyzer(IAnalysisSession& InSession)
	: Session(InSession)
	, Provider(nullptr)
	, ModuleBaseShift(0)
{
}

////////////////////////////////////////////////////////////////////////////////
void FModuleAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_ModuleInit, "Diagnostics", "ModuleInit");
	Builder.RouteEvent(RouteId_ModuleLoad, "Diagnostics", "ModuleLoad");
	Builder.RouteEvent(RouteId_ModuleUnload, "Diagnostics", "ModuleUnload");
}

////////////////////////////////////////////////////////////////////////////////
void FModuleAnalyzer::OnAnalysisEnd()
{
	if (Provider)
	{
		Provider->OnAnalysisComplete();
	}
}


////////////////////////////////////////////////////////////////////////////////
bool FModuleAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	switch(RouteId)
	{
		case RouteId_ModuleInit:
			{
				ModuleBaseShift = Context.EventData.GetValue("ModuleBaseShift", 16);
				FAnsiStringView SymbolFormat;
				if (Context.EventData.GetString("SymbolFormat", SymbolFormat))
				{
					check(Provider == nullptr); // Should only get one init message
					Provider = CreateModuleProvider(Session, SymbolFormat);
					if (Provider)
					{
						Session.AddProvider(GetModuleProviderName(), Provider);
					}
				}
			}
			break;

		case RouteId_ModuleLoad:
			if (Provider != nullptr)
			{
				//todo: Use string store
				FStringView ModuleName;
				if (Context.EventData.GetString("Name", ModuleName))
				{
					const uint64 Base = GetBaseAddress(Context.EventData);
					const uint32 Size = Context.EventData.GetValue<uint32>("Size");
					const TArrayReader<uint8>& ImageId = Context.EventData.GetArray<uint8>("ImageId");

					Provider->OnModuleLoad(ModuleName, Base, Size, ImageId.GetData(), ImageId.Num());
				}
			}
			break;

		case RouteId_ModuleUnload:
			if (Provider != nullptr)
			{
				const uint64 Base = GetBaseAddress(Context.EventData);
				Provider->OnModuleUnload(Base);
			}
			break;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////
uint64	FModuleAnalyzer::GetBaseAddress(const FEventData& EventData) const
{
	if (ModuleBaseShift == 0)
	{
		// With 0 base shift assume newer trace event is used with 64-bit base address
		return EventData.GetValue<uint64>("Base");
	}
	else
	{
		// Unshift base address according to capture alignment (e.g. Windows 4k aligned)
		uint64 Base = uint64(EventData.GetValue<uint32>("Base"));
		return Base << ModuleBaseShift;
	}
}

////////////////////////////////////////////////////////////////////////////////
} // namespace TraceServices
