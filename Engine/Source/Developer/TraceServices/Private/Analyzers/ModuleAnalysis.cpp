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
						Session.AddProvider(TEXT("ModuleProvider"), Provider);
					}
				}
			}
			break;

		case RouteId_ModuleLoad:
			{
				const uint64 Base = GetRealBaseAddress(Context.EventData.GetValue<uint32>("Base"));
				const uint32 Size = Context.EventData.GetValue<uint32>("Size");

				//todo: Use string store
				FStringView ModuleName;
				if (Context.EventData.GetString("Name", ModuleName))
				{
					if (Provider)
					{
						Provider->OnModuleLoad(ModuleName, Base, Size);
					}
				}
			}
			break;

		case RouteId_ModuleUnload:
			{
				const uint32 Base = Context.EventData.GetValue<uint32>("Base");
				if (Provider)
				{
					Provider->OnModuleUnload(Base);
				}
			}
			break;
	}
	return true;
}

////////////////////////////////////////////////////////////////////////////////
uint64 FModuleAnalyzer::GetRealBaseAddress(uint32 EventBase)
{
	// Unshift base address according to capture alignment (e.g. Windows 4k aligned)
	return uint64(EventBase) << ModuleBaseShift;
}

////////////////////////////////////////////////////////////////////////////////
} // namespace TraceServices
