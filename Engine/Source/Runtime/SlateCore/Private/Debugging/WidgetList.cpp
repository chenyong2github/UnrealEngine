// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetList.h"

#if UE_WITH_SLATE_DEBUG_WIDGETLIST

#include "Application/SlateApplicationBase.h"
#include "CoreGlobals.h"
#include "FastUpdate/WidgetProxy.h"
#include "FastUpdate/SlateInvalidationRoot.h"
#include "Layout/Children.h"
#include "Misc/OutputDeviceFile.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SWidget.h"

namespace UE
{
namespace Slate
{

// ------------------------------------------------

TArray<const SWidget*> FWidgetList::AllWidgets;

// ------------------------------------------------

void LogAllWidgetsDebugInfoImpl(FOutputDevice& Ar, bool bDebug, bool bPaint, bool bProxy, bool bChildren, bool bParent)
{
	TStringBuilder<1024> MessageBuilder;

	MessageBuilder << TEXT("Pointer;DebugInfo");
	if (bDebug)
	{
		MessageBuilder << TEXT(";Type;WidgetPath;ReadableLocation");
	}
	if (bPaint)
	{
		MessageBuilder << TEXT(";LastPaintFrame;LayerId;AllottedGeometryAbsoluteSizeX;AllottedGeometryAbsoluteSizeY");
	}
	if (bProxy)
	{
		MessageBuilder << TEXT(";InvalidationRootPointer;InvalidationRootDebugInfo;ProxyIndex");
	}
	if (bChildren)
	{
		MessageBuilder << TEXT(";NumAllChildren;NumChildren");
	}
	if (bParent)
	{
		MessageBuilder << TEXT(";ParentPointer;ParentDebugInfo");
	}
	Ar.Log(MessageBuilder.ToString());

	const TArray<const SWidget*>& WidgetList = FWidgetList::GetAllWidgets();
	for (const SWidget* Widget : WidgetList)
	{
		MessageBuilder.Reset();

		MessageBuilder.Appendf(TEXT("%p"), (void*)Widget);
		MessageBuilder << TEXT(";");
		MessageBuilder << FReflectionMetaData::GetWidgetDebugInfo(Widget);

		if (bDebug)
		{
			MessageBuilder << TEXT(";");
			MessageBuilder << Widget->GetTypeAsString();
			MessageBuilder << TEXT(";");
			MessageBuilder << FReflectionMetaData::GetWidgetPath(Widget);
			MessageBuilder << TEXT(";");
			MessageBuilder << Widget->GetReadableLocation();
		}

		if (bPaint)
		{
			FVector2D AbsoluteSize = Widget->GetPersistentState().AllottedGeometry.GetAbsoluteSize();
			MessageBuilder << TEXT(";");
			MessageBuilder << Widget->Debug_GetLastPaintFrame();
			MessageBuilder << TEXT(";");
			MessageBuilder << Widget->GetPersistentState().LayerId;
			MessageBuilder << TEXT(";");
			MessageBuilder.Appendf(TEXT("%f"), AbsoluteSize.X);
			MessageBuilder << TEXT(";");
			MessageBuilder.Appendf(TEXT("%f"), AbsoluteSize.Y);
		}

		if (bProxy)
		{
			const FWidgetProxyHandle ProxyHandle = Widget->GetProxyHandle();
			if (ProxyHandle.IsValid())
			{
				MessageBuilder << TEXT(";");
				MessageBuilder.Appendf(TEXT("%p"), (void*)ProxyHandle.GetInvalidationRoot());
				MessageBuilder << TEXT(";");
				MessageBuilder << FReflectionMetaData::GetWidgetDebugInfo(ProxyHandle.GetInvalidationRoot()->GetInvalidationRootWidget());
			}
			else
			{
				MessageBuilder << TEXT(";;;");
			}
		}

		if (bChildren)
		{
			MessageBuilder << TEXT(";");
			MessageBuilder << const_cast<SWidget*>(Widget)->GetAllChildren()->Num();
			MessageBuilder << TEXT(";");
			MessageBuilder << const_cast<SWidget*>(Widget)->GetChildren()->Num();
		}

		if (bParent)
		{
			const SWidget* ParentWidget = Widget->GetParentWidget().Get();
			MessageBuilder << TEXT(";");
			MessageBuilder.Appendf(TEXT("%p"), (void*)ParentWidget);
			MessageBuilder << TEXT(";");
			MessageBuilder << FReflectionMetaData::GetWidgetDebugInfo(ParentWidget);
		}

		Ar.Log(MessageBuilder.ToString());
	}
}

void LogAllWidgetsDebugInfo(const TArray< FString >& Args, UWorld*, FOutputDevice& Ar)
{
	bool bDebug = true;
	bool bPaint = false;
	bool bProxy = false;
	bool bChildren = false;
	bool bParent = false;
	FString OutputFilename;
	for (const FString& Arg : Args)
	{
		if (FParse::Value(*Arg, TEXT("File="), OutputFilename))
		{
			continue;
		}
		int32 FoundIndex = Arg.Find(TEXT("Debug="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bDebug, *Arg + FoundIndex + 6);
			continue;
		}

		FoundIndex = Arg.Find(TEXT("Paint="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bPaint, *Arg + FoundIndex + 6);
			continue;
		}

		FoundIndex = Arg.Find(TEXT("Proxy="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bProxy, *Arg + FoundIndex + 6);
			continue;
		}

		FoundIndex = Arg.Find(TEXT("Children="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bChildren, *Arg + FoundIndex + 9);
			continue;
		}
		
		FoundIndex = Arg.Find(TEXT("Parent="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bParent, *Arg + FoundIndex + 7);
			continue;
		}
	}

	if (OutputFilename.Len() > 0)
	{
		FOutputDeviceFile OutputDeviceFile {*FPaths::Combine(FPaths::ProjectSavedDir(), OutputFilename), true};
		OutputDeviceFile.SetSuppressEventTag(true);
		LogAllWidgetsDebugInfoImpl(OutputDeviceFile, bDebug, bPaint, bProxy, bChildren, bParent);
	}
	else
	{
		LogAllWidgetsDebugInfoImpl(Ar, bDebug, bPaint, bProxy, bChildren, bParent);
	}
}

FAutoConsoleCommandWithWorldArgsAndOutputDevice ConsoleCommandLogAllWidgets(
	TEXT("Slate.Debug.LogAllWidgets"),
	TEXT("Prints all the SWidgets type, debug info, path, painted, ...\n")
	TEXT("If a file name is not provided, it will output to the log console.\n")
	TEXT("Slate.Debug.LogAllWidgets [File=MyFile.csv] [Debug=true] [Paint=false] [Proxy=false] [Children=false] [Parent=false]"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&LogAllWidgetsDebugInfo)
);


} //Slate
} //UE

#endif //UE_WITH_SLATE_DEBUG_WIDGETLIST
