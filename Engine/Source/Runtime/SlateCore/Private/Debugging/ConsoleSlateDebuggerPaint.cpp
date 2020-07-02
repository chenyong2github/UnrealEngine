// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleSlateDebuggerPaint.h"
#include "ConsoleSlateDebugger.h"

#if WITH_SLATE_DEBUGGING

#include "CoreGlobals.h"
#include "Debugging/SlateDebugging.h"
#include "Layout/WidgetPath.h"
#include "Types/ReflectionMetadata.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "ConsoleSlateDebuggerPaint"

FConsoleSlateDebuggerPaint::FConsoleSlateDebuggerPaint()
	: bEnabled(false)
	, bDisplayWidgetsNameList(false)
	, bUseWidgetPathAsName(false)
	, bDrawBox(false)
	, bDrawQuad(true)
	, bLogWidgetName(false)
	, bLogWidgetNameOnce(false)
	, bLogWarningIfWidgetIsPaintedMoreThanOnce(true)
	, DrawBoxColor(1.0f, 1.0f, 0.0f, 0.2f)
	, DrawWidgetNameColor(FColorList::SpicyPink)
	, MaxNumberOfWidgetInList(20)
	, CacheDuration(2.0)
	, ShowPaintWidgetCommand(
		TEXT("SlateDebugger.Paint.Start")
		, TEXT("Start the painted widget debug tool. Use to show widget that have been painted this frame.")
		, FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerPaint::StartDebugging))
	, HidePaintWidgetCommand(
		TEXT("SlateDebugger.Paint.Stop")
		, TEXT("Stop the painted widget debug tool.")
		, FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerPaint::StopDebugging))
	, LogPaintedWidgetOnceCommand(
		TEXT("SlateDebugger.Paint.LogOnce")
		, TEXT("Log the widgets that has been painted during the last duration (default 2 secs) once")
		, FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerPaint::HandleLogOnce))
	, DisplayWidgetsNameListRefCVar(
		TEXT("SlateDebugger.Paint.DisplayWidgetNameList")
		, bDisplayWidgetsNameList
		, TEXT("Option to display the name of the widgets that are painted."))
	, MaxNumberOfWidgetInListtRefCVar(
		TEXT("SlateDebugger.Paint.MaxNumberOfWidgetDisplayedInList")
		, MaxNumberOfWidgetInList
		, TEXT("The max number of widget that will be displayed when DisplayWidgetNameList is active."))
	, DrawBoxRefCVar(
		TEXT("SlateDebugger.Paint.DrawBox")
		, bDrawBox
		, TEXT("Option to draw a box at the location of the painted widget."))
	, DrawQuadRefCVar(
		TEXT("SlateDebugger.Paint.DrawQuad")
		, bDrawQuad
		, TEXT("Option to draw a quad (debug rectangle) at the location of the painted widget."))
	, CacheDurationRefCVar(
		TEXT("SlateDebugger.Paint.DrawDuration")
		, CacheDuration
		, TEXT("For how long the debug info will be draw/displayed on screen."))
	, LogWarningIfWidgetIsPaintedMoreThanOnceRefCVar(
		TEXT("SlateDebugger.Paint.LogWarningIfWidgetIsPaintedMoreThanOnce")
		, bLogWarningIfWidgetIsPaintedMoreThanOnce
		, TEXT("Option to log a warning if a widget is painted more than once in the same frame."))
{
	GConfig->GetBool(TEXT("SlateDebugger.Paint"), TEXT("bDisplayWidgetsNameList"), bDisplayWidgetsNameList, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Paint"), TEXT("bUseWidgetPathAsName"), bUseWidgetPathAsName, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Paint"), TEXT("bDrawBox"), bDrawBox, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Paint"), TEXT("bDrawQuad"), bDrawQuad, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Paint"), TEXT("bLogWidgetName"), bLogWidgetName, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Paint"), TEXT("bLogWarningIfWidgetIsPaintedMoreThanOnce"), bLogWarningIfWidgetIsPaintedMoreThanOnce, *GEditorPerProjectIni);
	FColor TmpColor;
	if (GConfig->GetColor(TEXT("SlateDebugger.Paint"), TEXT("DrawBoxColor"), TmpColor, *GEditorPerProjectIni))
	{
		DrawBoxColor = TmpColor;
	}
	if (GConfig->GetColor(TEXT("SlateDebugger.Paint"), TEXT("DrawWidgetNameColor"), TmpColor, *GEditorPerProjectIni))
	{
		DrawWidgetNameColor = TmpColor;
	}
	GConfig->GetInt(TEXT("SlateDebugger.Paint"), TEXT("MaxNumberOfWidgetInList"), MaxNumberOfWidgetInList, *GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("SlateDebugger.Paint"), TEXT("CacheDuration"), CacheDuration, *GEditorPerProjectIni);
}

FConsoleSlateDebuggerPaint::~FConsoleSlateDebuggerPaint()
{
	StopDebugging();
}

void FConsoleSlateDebuggerPaint::SaveConfig()
{
	GConfig->SetBool(TEXT("SlateDebugger.Paint"), TEXT("bDisplayWidgetsNameList"), bDisplayWidgetsNameList, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Paint"), TEXT("bUseWidgetPathAsName"), bUseWidgetPathAsName, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Paint"), TEXT("bDrawBox"), bDrawBox, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Paint"), TEXT("bDrawQuad"), bDrawQuad, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Paint"), TEXT("bLogWidgetName"), bLogWidgetName, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Paint"), TEXT("bLogWarningIfWidgetIsPaintedMoreThanOnce"), bLogWarningIfWidgetIsPaintedMoreThanOnce, *GEditorPerProjectIni);
	FColor TmpColor = DrawBoxColor.ToFColor(true);
	GConfig->SetColor(TEXT("SlateDebugger.Paint"), TEXT("DrawBoxColor"), TmpColor, *GEditorPerProjectIni);
	TmpColor = DrawWidgetNameColor.ToFColor(true);
	GConfig->SetColor(TEXT("SlateDebugger.Paint"), TEXT("DrawWidgetNameColor"), TmpColor, *GEditorPerProjectIni);
	GConfig->SetInt(TEXT("SlateDebugger.Paint"), TEXT("MaxNumberOfWidgetInList"), MaxNumberOfWidgetInList, *GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("SlateDebugger.Paint"), TEXT("CacheDuration"), CacheDuration, *GEditorPerProjectIni);
}

void FConsoleSlateDebuggerPaint::StartDebugging()
{
	if (!bEnabled)
	{
		bEnabled = true;
		PaintedWidgets.Empty();

		FSlateDebugging::EndWidgetPaint.AddRaw(this, &FConsoleSlateDebuggerPaint::HandleEndWidgetPaint);
		FSlateDebugging::PaintDebugElements.AddRaw(this, &FConsoleSlateDebuggerPaint::HandlePaintDebugInfo);
		FCoreDelegates::OnEndFrame.AddRaw(this, &FConsoleSlateDebuggerPaint::HandleEndFrame);
	}
}

void FConsoleSlateDebuggerPaint::StopDebugging()
{
	if (bEnabled)
	{
		FCoreDelegates::OnEndFrame.RemoveAll(this);
		FSlateDebugging::PaintDebugElements.RemoveAll(this);
		FSlateDebugging::EndWidgetPaint.RemoveAll(this);

		PaintedWidgets.Empty();
		bEnabled = false;
	}
}

void FConsoleSlateDebuggerPaint::HandleLogOnce()
{
	bLogWidgetNameOnce = true;
}

void FConsoleSlateDebuggerPaint::HandleEndFrame()
{
	double LastTime = FApp::GetCurrentTime() - CacheDuration;
	for (TPaintedWidgetMap::TIterator It(PaintedWidgets); It; ++It)
	{
		It.Value().PaintCount = 0;
		if (It.Value().LastPaint < LastTime)
		{
			It.RemoveCurrent();
		}
	}
}

void FConsoleSlateDebuggerPaint::HandleEndWidgetPaint(const SWidget* Widget, const FSlateWindowElementList& OutDrawElements, int32 LayerId)
{
	// Use the Widget pointer for the id.
	//That may introduce bug when a widget is destroyed and the same memory is reused for another widget. We do not care for this debug tool.
	//We do not keep the widget alive or reuse it later, cache all the info that we need.
	const TSWidgetId WidgetId = reinterpret_cast<TSWidgetId>(Widget);
	const TSWindowId WindowId = reinterpret_cast<TSWindowId>(OutDrawElements.GetPaintWindow());

	FPaintInfo* FoundItem = PaintedWidgets.Find(WidgetId);
	if (FoundItem == nullptr)
	{
		FoundItem = &PaintedWidgets.Add(WidgetId);
		FoundItem->PaintCount = 0;
		FoundItem->Window = WindowId;
		FoundItem->WidgetName = bUseWidgetPathAsName ? FReflectionMetaData::GetWidgetPath(Widget) : FReflectionMetaData::GetWidgetDebugInfo(Widget);
	}
	else
	{
		ensureAlways(FoundItem->Window == WindowId);
		if (bLogWarningIfWidgetIsPaintedMoreThanOnce && FoundItem->PaintCount != 0)
		{
			UE_LOG(LogSlateDebugger, Warning, TEXT("'%s' got painted more than once."), *(FoundItem->WidgetName));
		}
	}

	if (bLogWidgetName)
	{
		UE_LOG(LogSlateDebugger, Log, TEXT("%s"), *(FoundItem->WidgetName));
	}
	FoundItem->PaintLocation = Widget->GetPersistentState().AllottedGeometry.GetAbsolutePosition();
	FoundItem->PaintSize = Widget->GetPersistentState().AllottedGeometry.GetAbsoluteSize();
	FoundItem->LastPaint = FApp::GetCurrentTime(); // Should use slate application's time but it's only available in the BeginWidgetPaint.
	++FoundItem->PaintCount;
}

void FConsoleSlateDebuggerPaint::HandlePaintDebugInfo(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry, FSlateWindowElementList& InOutDrawElements, int32& InOutLayerId)
{
	int32 PreviousLayerId = InOutLayerId;
	++InOutLayerId;

	TSWindowId PaintWindow = reinterpret_cast<TSWindowId>(InOutDrawElements.GetPaintWindow());
	int32 NumberOfWidget = 0;
	TArray<const FString*, TInlineAllocator<100>> NamesToDisplay;
	const FSlateBrush* BoxBrush = bDrawBox ? FCoreStyle::Get().GetBrush("WhiteBrush") : nullptr;

	for (const auto& Itt : PaintedWidgets)
	{
		if (Itt.Value.Window == PaintWindow)
		{
			FGeometry Geometry = FGeometry::MakeRoot(Itt.Value.PaintSize, FSlateLayoutTransform(1.f, Itt.Value.PaintLocation));
			if (BoxBrush)
			{
				FSlateDrawElement::MakeBox(InOutDrawElements, InOutLayerId, Geometry.ToPaintGeometry(), BoxBrush, ESlateDrawEffect::None, DrawBoxColor);
			}
			if (bDrawQuad)
			{
				FSlateDrawElement::MakeDebugQuad(InOutDrawElements, InOutLayerId, Geometry.ToPaintGeometry());
			}

			if (bLogWidgetNameOnce)
			{
				UE_LOG(LogSlateDebugger, Log, TEXT("%s"), *(Itt.Value.WidgetName));
			}

			++NumberOfWidget;
			if (NumberOfWidget <= MaxNumberOfWidgetInList)
			{
				NamesToDisplay.Emplace(&(Itt.Value.WidgetName));
			}
		}
	}
	bLogWidgetNameOnce = false;

	{
		FString NumberOfWidgetDrawn = FString::Printf(TEXT("Number of Widget Painted: %d"), NumberOfWidget);
		FSlateDrawElement::MakeText(
			InOutDrawElements
			, InOutLayerId
			, InAllottedGeometry.ToPaintGeometry()
			, MoveTemp(NumberOfWidgetDrawn)
			, FCoreStyle::GetDefaultFontStyle("Bold", 12)
			, ESlateDrawEffect::None
			, DrawWidgetNameColor);
	}

	if (bDisplayWidgetsNameList)
	{
		FSlateFontInfo FontInfo = FCoreStyle::GetDefaultFontStyle("Mono", 8);
		int32 Index = 0;
		for (const FString* Name : NamesToDisplay)
		{
			FSlateDrawElement::MakeText(
				InOutDrawElements
				, InOutLayerId
				, InAllottedGeometry.ToPaintGeometry(FVector2D(0, (12 * Index) + 36), FVector2D(1.f, 1.f))
				, *Name
				, FontInfo
				, ESlateDrawEffect::None
				, DrawWidgetNameColor);
			++Index;
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_SLATE_DEBUGGING
