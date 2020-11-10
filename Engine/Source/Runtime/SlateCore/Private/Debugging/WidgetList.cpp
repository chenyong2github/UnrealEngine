// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetList.h"

#if UE_WITH_SLATE_DEBUG_WIDGETLIST

#include "Application/SlateApplicationBase.h"
#include "CoreGlobals.h"
#include "FastUpdate/WidgetProxy.h"
#include "FastUpdate/SlateInvalidationRoot.h"
#include "GenericPlatform/ICursor.h"
#include "Layout/Children.h"
#include "Misc/OutputDeviceFile.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "Types/CursorMetaData.h"
#include "Types/MouseEventsMetaData.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/IToolTip.h"
#include "Widgets/SWidget.h"

namespace UE
{
namespace Slate
{

// ------------------------------------------------

TArray<const SWidget*> FWidgetList::AllWidgets;

// ------------------------------------------------

struct FLogAllWidgetsDebugInfoFlags
{
	bool bDebug;
	bool bPaint;
	bool bProxy;
	bool bChildren;
	bool bParent;
	bool bToolTip;
	bool bCursor;
	bool bMouseEventsHandler;

	void Parse(const FString& Arg)
	{
		int32 FoundIndex = Arg.Find(TEXT("Debug="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bDebug, *Arg + FoundIndex + 6);
			return;
		}

		FoundIndex = Arg.Find(TEXT("Paint="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bPaint, *Arg + FoundIndex + 6);
			return;
		}

		FoundIndex = Arg.Find(TEXT("Proxy="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bProxy, *Arg + FoundIndex + 6);
			return;
		}

		FoundIndex = Arg.Find(TEXT("Children="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bChildren, *Arg + FoundIndex + 9);
			return;
		}

		FoundIndex = Arg.Find(TEXT("Parent="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bParent, *Arg + FoundIndex + 7);
			return;
		}

		FoundIndex = Arg.Find(TEXT("ToolTip="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bToolTip, *Arg + FoundIndex + 8);
			return;
		}

		FoundIndex = Arg.Find(TEXT("Cursor="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bCursor, *Arg + FoundIndex + 7);
			return;
		}

		FoundIndex = Arg.Find(TEXT("MouseEvents="));
		if (FoundIndex != INDEX_NONE)
		{
			LexFromString(bMouseEventsHandler, *Arg + FoundIndex + 12);
			return;
		}
	}
};

void LogAllWidgetsDebugInfoImpl(FOutputDevice& Ar, const FLogAllWidgetsDebugInfoFlags& DebugInfoFlags)
{
	TStringBuilder<1024> MessageBuilder;

	MessageBuilder << TEXT("Pointer;DebugInfo");
	if (DebugInfoFlags.bDebug)
	{
		MessageBuilder << TEXT(";Type;WidgetPath;ReadableLocation");
	}
	if (DebugInfoFlags.bPaint)
	{
		MessageBuilder << TEXT(";LastPaintFrame;LayerId;AllottedGeometryAbsoluteSizeX;AllottedGeometryAbsoluteSizeY");
	}
	if (DebugInfoFlags.bProxy)
	{
		MessageBuilder << TEXT(";InvalidationRootPointer;InvalidationRootDebugInfo;ProxyIndex");
	}
	if (DebugInfoFlags.bChildren)
	{
		MessageBuilder << TEXT(";NumAllChildren;NumChildren");
	}
	if (DebugInfoFlags.bParent)
	{
		MessageBuilder << TEXT(";ParentPointer;ParentDebugInfo");
	}
	if (DebugInfoFlags.bToolTip)
	{
		MessageBuilder << TEXT(";ToolTipIsSet;ToolTipIsEmpty");
	}
	if (DebugInfoFlags.bCursor)
	{
		MessageBuilder << TEXT(";CursorIsSet;CursorValue");
	}
	if (DebugInfoFlags.bMouseEventsHandler)
	{
		MessageBuilder << TEXT(";MouseButtonDown;MouseButtonUp;MouseMove;MouseDblClick;MouseEnter;MouseLeave");
	}
	Ar.Log(MessageBuilder.ToString());

	const TArray<const SWidget*>& WidgetList = FWidgetList::GetAllWidgets();
	for (const SWidget* Widget : WidgetList)
	{
		MessageBuilder.Reset();

		MessageBuilder.Appendf(TEXT("%p"), (void*)Widget);
		MessageBuilder << TEXT(";");
		MessageBuilder << FReflectionMetaData::GetWidgetDebugInfo(Widget);

		if (DebugInfoFlags.bDebug)
		{
			MessageBuilder << TEXT(";");
			MessageBuilder << Widget->GetTypeAsString();
			MessageBuilder << TEXT(";");
			MessageBuilder << FReflectionMetaData::GetWidgetPath(Widget);
			MessageBuilder << TEXT(";");
			MessageBuilder << Widget->GetReadableLocation();
		}

		if (DebugInfoFlags.bPaint)
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

		if (DebugInfoFlags.bProxy)
		{
			const FWidgetProxyHandle ProxyHandle = Widget->GetProxyHandle();
			if (ProxyHandle.IsValid())
			{
				MessageBuilder << TEXT(";");
				MessageBuilder.Appendf(TEXT("%p"), (void*)ProxyHandle.GetInvalidationRoot());
				MessageBuilder << TEXT(";");
				MessageBuilder << FReflectionMetaData::GetWidgetDebugInfo(ProxyHandle.GetInvalidationRoot()->GetInvalidationRootWidget());
				MessageBuilder << TEXT(";");
				MessageBuilder << ProxyHandle.GetIndex(true);
			}
			else
			{
				MessageBuilder << TEXT(";;;");
			}
		}

		if (DebugInfoFlags.bChildren)
		{
			MessageBuilder << TEXT(";");
			MessageBuilder << const_cast<SWidget*>(Widget)->GetAllChildren()->Num();
			MessageBuilder << TEXT(";");
			MessageBuilder << const_cast<SWidget*>(Widget)->GetChildren()->Num();
		}

		if (DebugInfoFlags.bParent)
		{
			const SWidget* ParentWidget = Widget->GetParentWidget().Get();
			MessageBuilder << TEXT(";");
			MessageBuilder.Appendf(TEXT("%p"), (void*)ParentWidget);
			MessageBuilder << TEXT(";");
			MessageBuilder << FReflectionMetaData::GetWidgetDebugInfo(ParentWidget);
		}

		if (DebugInfoFlags.bToolTip)
		{
			if (TSharedPtr<IToolTip> ToolTip = const_cast<SWidget*>(Widget)->GetToolTip())
			{
				MessageBuilder << TEXT(";true");
				if (ToolTip->IsEmpty())
				{
					MessageBuilder << TEXT(";true");
				}
				else
				{
					MessageBuilder << TEXT(";false");
				}
			}
			else
			{
				MessageBuilder << TEXT(";false;false");
			}
		}
		
		if (DebugInfoFlags.bCursor)
		{
			if (TSharedPtr<FCursorMetaData> Data = Widget->GetMetaData<FCursorMetaData>())
			{
				if (Data->Cursor.IsSet())
				{
					TOptional<EMouseCursor::Type> Cursor = Data->Cursor.Get();
					if (Cursor.IsSet())
					{
						MessageBuilder << TEXT(";Set;");
						MessageBuilder << static_cast<int32>(Data->Cursor.Get().GetValue());
					}
					else
					{
						MessageBuilder << TEXT(";Optional;0");
					}
				}
				else
				{
					MessageBuilder << TEXT(";MetaData;0");
				}
			}
			else
			{
				MessageBuilder << TEXT(";None;0");
			}
		}

		if (DebugInfoFlags.bMouseEventsHandler)
		{
			if (TSharedPtr<FMouseEventsMetaData> Data = Widget->GetMetaData<FMouseEventsMetaData>())
			{
				MessageBuilder << (Data->MouseButtonDownHandle.IsBound() ? TEXT(";bound") : TEXT(";"));
				MessageBuilder << (Data->MouseButtonUpHandle.IsBound() ? TEXT(";bound") : TEXT(";"));
				MessageBuilder << (Data->MouseMoveHandle.IsBound() ? TEXT(";bound") : TEXT(";"));
				MessageBuilder << (Data->MouseDoubleClickHandle.IsBound() ? TEXT(";bound") : TEXT(";"));
				MessageBuilder << (Data->MouseEnterHandler.IsBound() ? TEXT(";bound") : TEXT(";"));
				MessageBuilder << (Data->MouseLeaveHandler.IsBound() ? TEXT(";bound") : TEXT(";"));
			}
			else
			{
				MessageBuilder << TEXT(";;;;;;");
			}
		}

		Ar.Log(MessageBuilder.ToString());
	}
}

void LogAllWidgetsDebugInfo(const TArray< FString >& Args, UWorld*, FOutputDevice& Ar)
{
	FLogAllWidgetsDebugInfoFlags DebugInfoFlags;
	FString OutputFilename;
	for (const FString& Arg : Args)
	{
		if (FParse::Value(*Arg, TEXT("File="), OutputFilename))
		{
			continue;
		}

		DebugInfoFlags.Parse(Arg);
	}

	if (OutputFilename.Len() > 0)
	{
		FOutputDeviceFile OutputDeviceFile {*FPaths::Combine(FPaths::ProjectSavedDir(), OutputFilename), true};
		OutputDeviceFile.SetSuppressEventTag(true);
		LogAllWidgetsDebugInfoImpl(OutputDeviceFile, DebugInfoFlags);
	}
	else
	{
		LogAllWidgetsDebugInfoImpl(Ar, DebugInfoFlags);
	}
}

FAutoConsoleCommandWithWorldArgsAndOutputDevice ConsoleCommandLogAllWidgets(
	TEXT("Slate.Debug.LogAllWidgets"),
	TEXT("Prints all the SWidgets type, debug info, path or painted.\n")
	TEXT("If a file name is not provided, it will output to the log console.\n")
	TEXT("Slate.Debug.LogAllWidgets [File=MyFile.csv] [Debug=true] [Paint=false] [Proxy=false] [Children=false] [Parent=false]"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&LogAllWidgetsDebugInfo)
);


} //Slate
} //UE

#endif //UE_WITH_SLATE_DEBUG_WIDGETLIST
