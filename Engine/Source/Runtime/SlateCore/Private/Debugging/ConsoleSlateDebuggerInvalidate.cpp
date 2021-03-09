// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debugging/ConsoleSlateDebuggerInvalidate.h"
#include "Debugging/ConsoleSlateDebugger.h"

#if WITH_SLATE_DEBUGGING

#include "Application/SlateApplicationBase.h"
#include "CoreGlobals.h"
#include "Debugging/SlateDebugging.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/StringBuilder.h"
#include "Styling/CoreStyle.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "ConsoleSlateDebuggerInvalidate"

FConsoleSlateDebuggerInvalidate::FConsoleSlateDebuggerInvalidate()
	: bEnabled(false)
	, bEnabledCVarValue(false)
	, bDisplayWidgetList(true)
	, bUseWidgetPathAsName(false)
	, bShowLegend(false)
	, bLogInvalidatedWidget(false)
	, InvalidateWidgetReasonFilter(static_cast<EInvalidateWidgetReason>(0xFF))
	, InvalidateRootReasonFilter(static_cast<ESlateDebuggingInvalidateRootReason>(0xFF))
	, DrawRootRootColor(FColorList::Red)
	, DrawRootChildOrderColor(FColorList::Blue)
	, DrawRootScreenPositionColor(FColorList::Green)
	, DrawWidgetLayoutColor(FColorList::Magenta)
	, DrawWidgetPaintColor(FColorList::Yellow)
	, DrawWidgetVolatilityColor(FColorList::Grey)
	, DrawWidgetChildOrderColor(FColorList::Cyan)
	, DrawWidgetRenderTransformColor(FColorList::Black)
	, DrawWidgetVisibilityColor(FColorList::White)
	, MaxNumberOfWidgetInList(20)
	, CacheDuration(2.0)
	, StartCommand(
		TEXT("SlateDebugger.Invalidate.Start"),
		TEXT("Start the Invalidation widget debug tool. It shows when widgets are invalidated."),
		FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::StartDebugging))
	, StopCommand(
		TEXT("SlateDebugger.Invalidate.Stop"),
		TEXT("Stop the Invalidation widget debug tool."),
		FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::StopDebugging))
	, EnabledRefCVar(
		TEXT("SlateDebugger.Invalidate.Enable")
		, bEnabledCVarValue
		, TEXT("Start/Stop the Invalidation widget debug tool. It shows when widgets are invalidated.")
		, FConsoleVariableDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::HandleEnabled))
	, ToggleLegendCommand(
		TEXT("SlateDebugger.Invalidate.ToggleLegend"),
		TEXT("Option to display the color legend."),
		FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::ToggleLegend))
	, ToogleWidgetsNameListCommand(
		TEXT("SlateDebugger.Invalidate.ToggleWidgetNameList"),
		TEXT("Option to display the name of the invalidated widget."),
		FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::ToggleWidgetNameList))
	, ToogleLogInvalidatedWidgetCommand(
		TEXT("SlateDebugger.Invalidate.ToggleLogInvalidatedWidget"),
		TEXT("Option to log to the console the invalidated widget."),
		FConsoleCommandDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::ToggleLogInvalidatedWidget))
	, SetInvalidateWidgetReasonFilterCommand(
		TEXT("SlateDebugger.Invalidate.SetInvalidateWidgetReasonFilter"),
		TEXT("Enable Invalidate Widget Reason filters. Usage: SetInvalidateWidgetReasonFilter [None] [Layout] [Paint] [Volatility] [ChildOrder] [RenderTransform] [Visibility] [Any]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::HandleSetInvalidateWidgetReasonFilter))
	, SetInvalidateRootReasonFilterCommand(
		TEXT("SlateDebugger.Invalidate.SetInvalidateRootReasonFilter"),
		TEXT("Enable Invalidate Root Reason filters. Usage: SetInvalidateRootReasonFilter [None] [ChildOrder] [Root] [ScreenPosition] [Any]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FConsoleSlateDebuggerInvalidate::HandleSetInvalidateRootReasonFilter))
{
	LoadConfig();
}

FConsoleSlateDebuggerInvalidate::~FConsoleSlateDebuggerInvalidate()
{
	StopDebugging();
}

void FConsoleSlateDebuggerInvalidate::LoadConfig()
{
	FColor TmpColor;
	auto GetColor = [&](const TCHAR* ColorText, FLinearColor& Color)
	{
		if (GConfig->GetColor(TEXT("SlateDebugger.Invalidate"), ColorText, TmpColor, *GEditorPerProjectIni))
		{
			Color = TmpColor;
		}
	};

	GConfig->GetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bDisplayWidgetList"), bDisplayWidgetList, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bUseWidgetPathAsName"), bUseWidgetPathAsName, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bShowLegend"), bShowLegend, *GEditorPerProjectIni);
	GConfig->GetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bLogInvalidatedWidget"), bLogInvalidatedWidget, *GEditorPerProjectIni);
	GetColor(TEXT("DrawRootRootColor"), DrawRootRootColor);
	GetColor(TEXT("DrawRootChildOrderColor"), DrawRootChildOrderColor);
	GetColor(TEXT("DrawRootScreenPositionColor"), DrawRootScreenPositionColor);
	GetColor(TEXT("DrawWidgetLayoutColor"), DrawWidgetLayoutColor);
	GetColor(TEXT("DrawWidgetPaintColor"), DrawWidgetPaintColor);
	GetColor(TEXT("DrawWidgetVolatilityColor"), DrawWidgetVolatilityColor);
	GetColor(TEXT("DrawWidgetChildOrderColor"), DrawWidgetChildOrderColor);
	GetColor(TEXT("DrawWidgetRenderTransformColor"), DrawWidgetRenderTransformColor);
	GetColor(TEXT("DrawWidgetVisibilityColor"), DrawWidgetVisibilityColor);
	GConfig->GetInt(TEXT("SlateDebugger.Invalidate"), TEXT("MaxNumberOfWidgetInList"), MaxNumberOfWidgetInList, *GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("SlateDebugger.Invalidate"), TEXT("CacheDuration"), CacheDuration, *GEditorPerProjectIni);
}

void FConsoleSlateDebuggerInvalidate::SaveConfig()
{
	auto SetColor = [](const TCHAR* ColorText, const FLinearColor& Color)
	{
		FColor TmpColor = Color.ToFColor(true);
		GConfig->SetColor(TEXT("SlateDebugger.Invalidate"), ColorText, TmpColor, *GEditorPerProjectIni);
	};

	GConfig->SetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bDisplayWidgetList"), bDisplayWidgetList, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bUseWidgetPathAsName"), bUseWidgetPathAsName, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bShowLegend"), bShowLegend, *GEditorPerProjectIni);
	GConfig->SetBool(TEXT("SlateDebugger.Invalidate"), TEXT("bLogInvalidatedWidget"), bLogInvalidatedWidget, *GEditorPerProjectIni);
	SetColor(TEXT("DrawRootRootColor"), DrawRootRootColor);
	SetColor(TEXT("DrawRootChildOrderColor"), DrawRootChildOrderColor);
	SetColor(TEXT("DrawRootScreenPositionColor"), DrawRootScreenPositionColor);
	SetColor(TEXT("DrawWidgetLayoutColor"), DrawWidgetLayoutColor);
	SetColor(TEXT("DrawWidgetPaintColor"), DrawWidgetPaintColor);
	SetColor(TEXT("DrawWidgetVolatilityColor"), DrawWidgetVolatilityColor);
	SetColor(TEXT("DrawWidgetChildOrderColor"), DrawWidgetChildOrderColor);
	SetColor(TEXT("DrawWidgetRenderTransformColor"), DrawWidgetRenderTransformColor);
	SetColor(TEXT("DrawWidgetVisibilityColor"), DrawWidgetVisibilityColor);
	GConfig->SetInt(TEXT("SlateDebugger.Invalidate"), TEXT("MaxNumberOfWidgetInList"), MaxNumberOfWidgetInList, *GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("SlateDebugger.Invalidate"), TEXT("CacheDuration"), CacheDuration, *GEditorPerProjectIni);
}

void FConsoleSlateDebuggerInvalidate::StartDebugging()
{
	if (!bEnabled)
	{
		bEnabled = true;
		InvalidationInfos.Empty();
		FrameInvalidationInfos.Empty();

		FSlateDebugging::PaintDebugElements.AddRaw(this, &FConsoleSlateDebuggerInvalidate::HandlePaintDebugInfo);
		FSlateDebugging::WidgetInvalidateEvent.AddRaw(this, &FConsoleSlateDebuggerInvalidate::HandleWidgetInvalidated);
		FCoreDelegates::OnEndFrame.AddRaw(this, &FConsoleSlateDebuggerInvalidate::HandleEndFrame);
	}
	bEnabledCVarValue = bEnabled;
}

void FConsoleSlateDebuggerInvalidate::StopDebugging()
{
	if (bEnabled)
	{
		FCoreDelegates::OnEndFrame.RemoveAll(this);
		FSlateDebugging::WidgetInvalidateEvent.RemoveAll(this);
		FSlateDebugging::PaintDebugElements.RemoveAll(this);

		InvalidationInfos.Empty();
		FrameInvalidationInfos.Empty();
		bEnabled = false;
	}
	bEnabledCVarValue = bEnabled;
}

void FConsoleSlateDebuggerInvalidate::HandleEnabled(IConsoleVariable* Variable)
{
	if (bEnabledCVarValue)
	{
		StartDebugging();
	}
	else
	{
		StopDebugging();
	}
}

void FConsoleSlateDebuggerInvalidate::ToggleLegend()
{
	bShowLegend = !bShowLegend;
	SaveConfig();
}

void FConsoleSlateDebuggerInvalidate::ToggleWidgetNameList()
{
	bDisplayWidgetList = !bDisplayWidgetList;
	SaveConfig();
}

void FConsoleSlateDebuggerInvalidate::ToggleLogInvalidatedWidget()
{
	bLogInvalidatedWidget = !bLogInvalidatedWidget;
	SaveConfig();
}

namespace ConsoleSlateDebuggerInvalidate
{
	template<class EnumType, int32 BuilderSize>
	bool CheckAndAddToMessageBuilder(TStringBuilder<BuilderSize>& MessageBuilder, EnumType Filter, EnumType Reason, const TCHAR* Message, bool bFirstFlag)
	{
		if (EnumHasAnyFlags(Filter, Reason))
		{
			if (!bFirstFlag)
			{
				MessageBuilder << TEXT("|");
			}
			bFirstFlag = false;
			MessageBuilder << Message;
		}
		return bFirstFlag;
	}

	template<class EnumType>
	bool TestAndSetEnum(const FString& Param, EnumType& NewFilter, EnumType Reason, const TCHAR* ReasonParam)
	{
		if (Param == ReasonParam)
		{
			NewFilter |= Reason;
			return true;
		}
		return false;
	}

	template<int32 BuilderSize>
	void BuildEnumMessage(TStringBuilder<BuilderSize>& MessageBuilder, EInvalidateWidgetReason InvalidateWidgetReasonFilter)
	{
		bool bFirstFlag = true;
		bFirstFlag = CheckAndAddToMessageBuilder(MessageBuilder, InvalidateWidgetReasonFilter, EInvalidateWidgetReason::Layout, TEXT("Layout"), bFirstFlag);
		bFirstFlag = CheckAndAddToMessageBuilder(MessageBuilder, InvalidateWidgetReasonFilter, EInvalidateWidgetReason::Paint, TEXT("Paint"), bFirstFlag);
		bFirstFlag = CheckAndAddToMessageBuilder(MessageBuilder, InvalidateWidgetReasonFilter, EInvalidateWidgetReason::Volatility, TEXT("Volatility"), bFirstFlag);
		bFirstFlag = CheckAndAddToMessageBuilder(MessageBuilder, InvalidateWidgetReasonFilter, EInvalidateWidgetReason::ChildOrder, TEXT("ChildOrder"), bFirstFlag);
		bFirstFlag = CheckAndAddToMessageBuilder(MessageBuilder, InvalidateWidgetReasonFilter, EInvalidateWidgetReason::RenderTransform, TEXT("RenderTransform"), bFirstFlag);
		bFirstFlag = CheckAndAddToMessageBuilder(MessageBuilder, InvalidateWidgetReasonFilter, EInvalidateWidgetReason::Visibility, TEXT("Visibility"), bFirstFlag);

		if (bFirstFlag)
		{
			MessageBuilder << TEXT("None");
		}
	}

	template<int32 BuilderSize>
	void BuildEnumMessage(TStringBuilder<BuilderSize>& MessageBuilder, ESlateDebuggingInvalidateRootReason InvalidateRootReasonFilter)
	{
		bool bFirstFlag = true;
		bFirstFlag = ConsoleSlateDebuggerInvalidate::CheckAndAddToMessageBuilder(MessageBuilder, InvalidateRootReasonFilter, ESlateDebuggingInvalidateRootReason::ChildOrder, TEXT("ChildOrder"), bFirstFlag);
		bFirstFlag = ConsoleSlateDebuggerInvalidate::CheckAndAddToMessageBuilder(MessageBuilder, InvalidateRootReasonFilter, ESlateDebuggingInvalidateRootReason::Root, TEXT("Root"), bFirstFlag);
		bFirstFlag = ConsoleSlateDebuggerInvalidate::CheckAndAddToMessageBuilder(MessageBuilder, InvalidateRootReasonFilter, ESlateDebuggingInvalidateRootReason::ScreenPosition, TEXT("ScreenPosition"), bFirstFlag);

		if (bFirstFlag)
		{
			MessageBuilder << TEXT("None");
		}
	}
}

void FConsoleSlateDebuggerInvalidate::HandleSetInvalidateWidgetReasonFilter(const TArray<FString>& Params)
{
	const TCHAR* UsageMessage = TEXT("Usage: SetInvalidateWidgetReasonFilter [None] [Layout] [Paint] [Volatility] [ChildOrder] [RenderTransform] [Visibility] [Any]");
	if (Params.Num() == 0)
	{
		UE_LOG(LogSlateDebugger, Log, TEXT("%s"), UsageMessage);

		TStringBuilder<128> MessageBuilder;
		MessageBuilder << TEXT("Current Invalidate Widget Reason set: ");
		ConsoleSlateDebuggerInvalidate::BuildEnumMessage(MessageBuilder, InvalidateWidgetReasonFilter);
		UE_LOG(LogSlateDebugger, Log, TEXT("%s"), MessageBuilder.GetData());
	}
	else
	{
		EInvalidateWidgetReason NewInvalidateWidgetReasonFilter = EInvalidateWidgetReason::None;
		bool bHasValidFlags = true;
		for (const FString& Param : Params)
		{
			if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::Layout, TEXT("Layout"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::Paint, TEXT("Paint"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::Volatility, TEXT("Volatility"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::ChildOrder, TEXT("ChildOrder"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::RenderTransform, TEXT("RenderTransform"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::Visibility, TEXT("Visibility"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, static_cast<EInvalidateWidgetReason>(0xFF), TEXT("Any"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateWidgetReasonFilter, EInvalidateWidgetReason::None, TEXT("None"))) {}
			else
			{
				bHasValidFlags = false;
				UE_LOG(LogSlateDebugger, Warning, TEXT("Param '%s' is invalid."), *Param);
				break;
			}
		}

		if (!bHasValidFlags)
		{
			UE_LOG(LogSlateDebugger, Log, TEXT("%s"), UsageMessage);
		}
		else
		{
			InvalidateWidgetReasonFilter = NewInvalidateWidgetReasonFilter;
			SaveConfig();
		}
	}
}

void FConsoleSlateDebuggerInvalidate::HandleSetInvalidateRootReasonFilter(const TArray<FString>& Params)
{
	const TCHAR* UsageMessage = TEXT("Usage: SetInvalidateRootReasonFilter [None] [ChildOrder] [Root] [ScreenPosition] [Any]");
	if (Params.Num() == 0)
	{
		UE_LOG(LogSlateDebugger, Log, TEXT("%s"), UsageMessage);

		TStringBuilder<128> MessageBuilder;
		MessageBuilder << TEXT("Current Invalidate Root Reason set: ");
		ConsoleSlateDebuggerInvalidate::BuildEnumMessage(MessageBuilder, InvalidateRootReasonFilter);
		UE_LOG(LogSlateDebugger, Log, TEXT("%s"), MessageBuilder.GetData());
	}
	else
	{
		ESlateDebuggingInvalidateRootReason NewInvalidateRoottReasonFilter = ESlateDebuggingInvalidateRootReason::None;
		bool bHasValidFlags = true;
		for (const FString& Param : Params)
		{
			if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateRoottReasonFilter, ESlateDebuggingInvalidateRootReason::ChildOrder, TEXT("ChildOrder"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateRoottReasonFilter, ESlateDebuggingInvalidateRootReason::Root, TEXT("Root"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateRoottReasonFilter, ESlateDebuggingInvalidateRootReason::ScreenPosition, TEXT("ScreenPosition"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateRoottReasonFilter, static_cast<ESlateDebuggingInvalidateRootReason>(0xFF), TEXT("Any"))) {}
			else if (ConsoleSlateDebuggerInvalidate::TestAndSetEnum(Param, NewInvalidateRoottReasonFilter, ESlateDebuggingInvalidateRootReason::None, TEXT("None"))) {}
			else
			{
				bHasValidFlags = false;
				UE_LOG(LogSlateDebugger, Warning, TEXT("Param '%s' is invalid."), *Param);
				break;
			}
		}

		if (!bHasValidFlags)
		{
			UE_LOG(LogSlateDebugger, Log, TEXT("%s"), UsageMessage);
		}
		else
		{
			InvalidateRootReasonFilter = NewInvalidateRoottReasonFilter;
			SaveConfig();
		}
	}
}

int32 FConsoleSlateDebuggerInvalidate::GetInvalidationPriority(EInvalidateWidgetReason InvalidationInfo, ESlateDebuggingInvalidateRootReason InvalidationRootReason) const
{
	InvalidationInfo &= InvalidateWidgetReasonFilter;
	InvalidationRootReason &= InvalidateRootReasonFilter;

	if (EnumHasAnyFlags(InvalidationRootReason, ESlateDebuggingInvalidateRootReason::Root))
	{
		return 100;
	}
	else if (EnumHasAnyFlags(InvalidationRootReason, ESlateDebuggingInvalidateRootReason::ChildOrder))
	{
		return 80;
	}
	else if (EnumHasAnyFlags(InvalidationRootReason, ESlateDebuggingInvalidateRootReason::ScreenPosition))
	{
		return 50;
	}

	if (EnumHasAnyFlags(InvalidationInfo, EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder | EInvalidateWidgetReason::Visibility | EInvalidateWidgetReason::RenderTransform))
	{
		return 40;
	}
	else if (EnumHasAnyFlags(InvalidationInfo, EInvalidateWidgetReason::Paint))
	{
		return 20;
	}
	else if (EnumHasAnyFlags(InvalidationInfo, EInvalidateWidgetReason::Volatility))
	{
		return 10;
	}
	return 0;
}

const FLinearColor& FConsoleSlateDebuggerInvalidate::GetColor(const FInvalidationInfo& InvalidationInfo) const
{
	if (EnumHasAnyFlags(InvalidationInfo.InvalidationRootReason, ESlateDebuggingInvalidateRootReason::Root))
	{
		return DrawRootRootColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.InvalidationRootReason, ESlateDebuggingInvalidateRootReason::ChildOrder))
	{
		return DrawRootChildOrderColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.InvalidationRootReason, ESlateDebuggingInvalidateRootReason::ScreenPosition))
	{
		return DrawRootScreenPositionColor;
	}

	if (EnumHasAnyFlags(InvalidationInfo.WidgetReason, EInvalidateWidgetReason::Layout))
	{
		return DrawWidgetLayoutColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.WidgetReason, EInvalidateWidgetReason::Paint))
	{
		return DrawWidgetPaintColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.WidgetReason, EInvalidateWidgetReason::Volatility))
	{
		return DrawWidgetVolatilityColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.WidgetReason, EInvalidateWidgetReason::ChildOrder))
	{
		return DrawWidgetChildOrderColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.WidgetReason, EInvalidateWidgetReason::RenderTransform))
	{
		return DrawWidgetRenderTransformColor;
	}
	else if (EnumHasAnyFlags(InvalidationInfo.WidgetReason, EInvalidateWidgetReason::Visibility))
	{
		return DrawWidgetVisibilityColor;
	}

	check(false);
	return FLinearColor::Black;
}

FConsoleSlateDebuggerInvalidate::FInvalidationInfo::FInvalidationInfo(const FSlateDebuggingInvalidateArgs& Args, int32 InInvalidationPriority, bool bBuildWidgetName, bool bUseWidgetPathAsName)
	: WidgetInvalidatedId(FConsoleSlateDebuggerUtility::GetId(Args.WidgetInvalidated))
	, WidgetInvalidatorId(FConsoleSlateDebuggerUtility::GetId(Args.WidgetInvalidateInvestigator))
	, WidgetInvalidated(Args.WidgetInvalidated->DoesSharedInstanceExist() ? Args.WidgetInvalidated->AsShared() : TWeakPtr<const SWidget>())
	, WidgetInvalidator(Args.WidgetInvalidateInvestigator && Args.WidgetInvalidateInvestigator->DoesSharedInstanceExist() ? Args.WidgetInvalidateInvestigator->AsShared() : TWeakPtr<const SWidget>())
	, WindowId(FConsoleSlateDebuggerUtility::InvalidWindowId)
	, WidgetReason(Args.InvalidateWidgetReason)
	, InvalidationRootReason(Args.InvalidateInvalidationRootReason)
	, InvalidationPriority(InInvalidationPriority)
	, InvalidationTime(0.0)
	, bIsInvalidatorPaintValid(false)
{
	if (bBuildWidgetName)
	{
		WidgetInvalidatedName = bUseWidgetPathAsName ? FReflectionMetaData::GetWidgetPath(Args.WidgetInvalidated) : FReflectionMetaData::GetWidgetDebugInfo(Args.WidgetInvalidated);
		WidgetInvalidatorName = bUseWidgetPathAsName ? FReflectionMetaData::GetWidgetPath(Args.WidgetInvalidateInvestigator) : FReflectionMetaData::GetWidgetDebugInfo(Args.WidgetInvalidateInvestigator);
	}
}

void FConsoleSlateDebuggerInvalidate::FInvalidationInfo::ReplaceInvalidated(const FSlateDebuggingInvalidateArgs& InArgs, int32 InInvalidationPriority, bool bInBuildWidgetName, bool bInUseWidgetPathAsName)
{
	if (WidgetInvalidatorId == FConsoleSlateDebuggerUtility::InvalidWidgetId)
	{
		WidgetInvalidatorId = WidgetInvalidatedId;
		WidgetInvalidator = MoveTemp(WidgetInvalidated);
		WidgetInvalidatorName = MoveTemp(WidgetInvalidatedName);
	}

	check(InArgs.WidgetInvalidated);
	WidgetInvalidatedId = FConsoleSlateDebuggerUtility::GetId(InArgs.WidgetInvalidated);
	WidgetInvalidated = InArgs.WidgetInvalidated->DoesSharedInstanceExist() ? InArgs.WidgetInvalidated->AsShared() : TWeakPtr<const SWidget>();
	if (bInBuildWidgetName)
	{
		WidgetInvalidatedName = bInUseWidgetPathAsName ? FReflectionMetaData::GetWidgetPath(InArgs.WidgetInvalidated) : FReflectionMetaData::GetWidgetDebugInfo(InArgs.WidgetInvalidated);
	}
	WidgetReason |= InArgs.InvalidateWidgetReason;
	InvalidationRootReason |= InArgs.InvalidateInvalidationRootReason;
	InvalidationPriority = InInvalidationPriority;

}

void FConsoleSlateDebuggerInvalidate::FInvalidationInfo::ReplaceInvalidator(const FSlateDebuggingInvalidateArgs& InArgs, int32 InInvalidationPriority, bool bInBuildWidgetName, bool bInUseWidgetPathAsName)
{
	WidgetInvalidatorId = FConsoleSlateDebuggerUtility::GetId(InArgs.WidgetInvalidateInvestigator);
	if (bInBuildWidgetName)
	{
		WidgetInvalidatorName = bInUseWidgetPathAsName ? FReflectionMetaData::GetWidgetPath(InArgs.WidgetInvalidateInvestigator) : FReflectionMetaData::GetWidgetDebugInfo(InArgs.WidgetInvalidateInvestigator);
	}
	WidgetReason |= InArgs.InvalidateWidgetReason;
	InvalidationRootReason |= InArgs.InvalidateInvalidationRootReason;
	InvalidationPriority = InInvalidationPriority;
}

void FConsoleSlateDebuggerInvalidate::FInvalidationInfo::UpdateInvalidationReason(const FSlateDebuggingInvalidateArgs& Args, int32 InInvalidationPriority)
{
	WidgetReason |= Args.InvalidateWidgetReason;
	InvalidationRootReason |= Args.InvalidateInvalidationRootReason;
	InvalidationPriority = InInvalidationPriority;
}

void FConsoleSlateDebuggerInvalidate::HandleEndFrame()
{
	double LastTime = FSlateApplicationBase::Get().GetCurrentTime() - CacheDuration;
	for (int32 Index = InvalidationInfos.Num() - 1; Index >= 0; --Index)
	{
		if (InvalidationInfos[Index].InvalidationTime < LastTime)
		{
			InvalidationInfos.RemoveAtSwap(Index);
		}
	}

	ProcessFrameList();
}

void FConsoleSlateDebuggerInvalidate::HandleWidgetInvalidated(const FSlateDebuggingInvalidateArgs& Args)
{
	// Reduce the invalidation tree to single child.
	//Tree:
	//A->B->C [Paint]
	//A->B->C->D [Layout]
	//Z->Y->C->D [Volatility]
	//X->W->C->D [Layout]
	//I->J->K [Paint]
	//Reduce to:
	//A->D [Layout] (ignore X->D because of the incoming order)
	//I->K [Paint]
	//~ depending of the incoming order, it's possible that we have A->C(Paint) and then A->D(Layout)

	if (Args.WidgetInvalidated == nullptr)
	{
		return;
	}

	if (!Args.WidgetInvalidated->GetProxyHandle().IsValid(Args.WidgetInvalidated))
	{
		return;
	}

	const FConsoleSlateDebuggerUtility::TSWidgetId WidgetInvalidatedId = FConsoleSlateDebuggerUtility::GetId(Args.WidgetInvalidated);
	const FConsoleSlateDebuggerUtility::TSWidgetId WidgetInvalidatorId = FConsoleSlateDebuggerUtility::GetId(Args.WidgetInvalidateInvestigator);

	const int32 InvalidationPriority = GetInvalidationPriority(Args.InvalidateWidgetReason, Args.InvalidateInvalidationRootReason);
	if (InvalidationPriority == 0)
	{
		// The invalidation is filtered.
		return;
	}

	FInvalidationInfo* FoundInvalidated = FrameInvalidationInfos.FindByPredicate([WidgetInvalidatedId, WidgetInvalidatorId](const FInvalidationInfo& InvalidationInfo)
		{
			return InvalidationInfo.WidgetInvalidatedId == WidgetInvalidatedId
				&& InvalidationInfo.WidgetInvalidatorId == WidgetInvalidatorId;
		});

	// Is the same invalidation couple already in the list
	if (FoundInvalidated)
	{
		// Is this invalidation is more important for the display?
		//Z->C[RenderTransform] to B->C[Layout]
		//NB we use < instead of <= so only the first incoming invalidation will be considered 
		FoundInvalidated->UpdateInvalidationReason(Args, InvalidationPriority);
	}
	else
	{
		FoundInvalidated = FrameInvalidationInfos.FindByPredicate([WidgetInvalidatedId](const FInvalidationInfo& InvalidationInfo)
			{
				return InvalidationInfo.WidgetInvalidatedId == WidgetInvalidatedId;
			});

		if (FoundInvalidated)
		{
			// Same invalidated with a better priority, replace the invalidator
			//A->D [Paint] to A->D [Layout]. 
			if (FoundInvalidated->InvalidationPriority < InvalidationPriority)
			{
				FoundInvalidated->ReplaceInvalidator(Args, InvalidationPriority, bDisplayWidgetList, bUseWidgetPathAsName);
			}
		}
		else
		{
			FoundInvalidated = FrameInvalidationInfos.FindByPredicate([WidgetInvalidatorId](const FInvalidationInfo& InvalidationInfo)
				{
					return InvalidationInfo.WidgetInvalidatedId == WidgetInvalidatorId;
				});

			if (FoundInvalidated)
			{
				// is this a continuation of an existing chain
				if (FoundInvalidated->InvalidationPriority <= InvalidationPriority)
				{
					FoundInvalidated->ReplaceInvalidated(Args, InvalidationPriority, bDisplayWidgetList, bUseWidgetPathAsName);
				}
			}
			else
			{
				// New element in the chain
				FrameInvalidationInfos.Emplace(Args, InvalidationPriority, bDisplayWidgetList, bUseWidgetPathAsName);
			}
		}
	}
}

void FConsoleSlateDebuggerInvalidate::ProcessFrameList()
{
	const double CurrentTime = FSlateApplicationBase::Get().GetCurrentTime();

	for (FInvalidationInfo& FrameInvalidationInfo : FrameInvalidationInfos)
	{
		if (bLogInvalidatedWidget)
		{
			TStringBuilder<255> MessageBuilder;
			MessageBuilder << TEXT("Invalidator: '");
			MessageBuilder << FrameInvalidationInfo.WidgetInvalidatorName;
			MessageBuilder << TEXT("' Invalidated: '");
			MessageBuilder << FrameInvalidationInfo.WidgetInvalidatedName;
			MessageBuilder << TEXT("' Root Reason: '");
			ConsoleSlateDebuggerInvalidate::BuildEnumMessage(MessageBuilder, FrameInvalidationInfo.InvalidationRootReason);
			MessageBuilder << TEXT("' Widget Reason: '");
			ConsoleSlateDebuggerInvalidate::BuildEnumMessage(MessageBuilder, FrameInvalidationInfo.WidgetReason);
			MessageBuilder << TEXT("'");

			UE_LOG(LogSlateDebugger, Log, TEXT("%s"), MessageBuilder.GetData());
		}

		if (TSharedPtr<const SWidget> Invalidated = FrameInvalidationInfo.WidgetInvalidated.Pin())
		{
			FrameInvalidationInfo.WindowId = FConsoleSlateDebuggerUtility::FindWindowId(Invalidated.Get());
			if (FrameInvalidationInfo.WindowId != FConsoleSlateDebuggerUtility::InvalidWindowId)
			{
				FrameInvalidationInfo.DisplayColor = GetColor(FrameInvalidationInfo);
				FrameInvalidationInfo.InvalidationTime = CurrentTime;
				FrameInvalidationInfo.InvalidatedPaintLocation = Invalidated->GetPersistentState().AllottedGeometry.GetAbsolutePosition();
				FrameInvalidationInfo.InvalidatedPaintSize = Invalidated->GetPersistentState().AllottedGeometry.GetAbsoluteSize();

				if (TSharedPtr<const SWidget> Invalidator = FrameInvalidationInfo.WidgetInvalidator.Pin())
				{
					FrameInvalidationInfo.bIsInvalidatorPaintValid = true;
					FrameInvalidationInfo.InvalidatorPaintLocation = Invalidator->GetPersistentState().AllottedGeometry.GetAbsolutePosition();
					FrameInvalidationInfo.InvalidatorPaintSize = Invalidator->GetPersistentState().AllottedGeometry.GetAbsoluteSize();
				}
				InvalidationInfos.Emplace(MoveTemp(FrameInvalidationInfo));
			}
		}
	}
	FrameInvalidationInfos.Reset();
}

void FConsoleSlateDebuggerInvalidate::HandlePaintDebugInfo(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry, FSlateWindowElementList& InOutDrawElements, int32& InOutLayerId)
{
	++InOutLayerId;

	const FConsoleSlateDebuggerUtility::TSWindowId PaintWindow = FConsoleSlateDebuggerUtility::GetId(InOutDrawElements.GetPaintWindow());
	FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle("SmallFont");
	const FSlateBrush* BoxBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateBrush* CheckerboardBrush = FCoreStyle::Get().GetBrush("Checkerboard");
	FontInfo.OutlineSettings.OutlineSize = 1;

	int32 NumberOfWidget = 0;
	CacheDuration = FMath::Max(CacheDuration, 0.01f);
	const double SlateApplicationCurrentTime = FSlateApplicationBase::Get().GetCurrentTime();

	float TextElementY = 48.f;
	auto MakeText = [&](const FString& Text, const FVector2D& Location, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(
			InOutDrawElements
			, InOutLayerId
			, InAllottedGeometry.ToPaintGeometry(Location, FVector2D(1.f, 1.f))
			, Text
			, FontInfo
			, ESlateDrawEffect::None
			, Color);
	};

	if (bShowLegend)
	{
		MakeText(TEXT("Invalidation Root - Root"), FVector2D(10.f, 10.f+0.f), DrawRootRootColor);
		MakeText(TEXT("Invalidation Root - Child Order"), FVector2D(10.f, 10.f+12.f), DrawRootChildOrderColor);
		MakeText(TEXT("Invalidation Root - Screen Position"), FVector2D(10.f, 10.f+24.f), DrawRootScreenPositionColor);
		MakeText(TEXT("Widget - Layout"), FVector2D(10.f, 10.f+36.f), DrawWidgetLayoutColor);
		MakeText(TEXT("Widget - Paint"), FVector2D(10.f, 10.f+48.f), DrawWidgetPaintColor);
		MakeText(TEXT("Widget - Volatility"), FVector2D(10.f, 10.f+60.f), DrawWidgetVolatilityColor);
		MakeText(TEXT("Widget - Child Order"), FVector2D(10.f, 10.f+72.f), DrawWidgetChildOrderColor);
		MakeText(TEXT("Widget - Render Transform"), FVector2D(10.f, 10.f+84.f), DrawWidgetRenderTransformColor);
		MakeText(TEXT("Widget - Visibility"), FVector2D(10.f, 10.f+96.f), DrawWidgetVisibilityColor);
		TextElementY += 108.f;
	}

	TArray<FConsoleSlateDebuggerUtility::TSWidgetId, TInlineAllocator<32>> AlreadyProcessedInvalidatedId;
	for (const FInvalidationInfo& InvalidationInfo : InvalidationInfos)
	{
		if (InvalidationInfo.WindowId != PaintWindow)
		{
			continue;
		}
		if (AlreadyProcessedInvalidatedId.Contains(InvalidationInfo.WidgetInvalidatedId))
		{
			continue;
		}
		if (InvalidationInfo.WidgetInvalidatorId != FConsoleSlateDebuggerUtility::InvalidWidgetId && AlreadyProcessedInvalidatedId.Contains(InvalidationInfo.WidgetInvalidatorId))
		{
			continue;
		}
		AlreadyProcessedInvalidatedId.Add(InvalidationInfo.WidgetInvalidatedId);
		if (InvalidationInfo.WidgetInvalidatorId != FConsoleSlateDebuggerUtility::InvalidWidgetId)
		{
			AlreadyProcessedInvalidatedId.Add(InvalidationInfo.WidgetInvalidatorId);
		}

		const double LerpValue = FMath::Clamp((SlateApplicationCurrentTime - InvalidationInfo.InvalidationTime) / CacheDuration, 0.0, 1.0);
		const FLinearColor ColorWithOpacity = InvalidationInfo.DisplayColor.CopyWithNewOpacity(FMath::InterpExpoOut(1.0f, 0.2f, LerpValue));

		{
			const FGeometry InvalidatedGeometry = FGeometry::MakeRoot(InvalidationInfo.InvalidatedPaintSize, FSlateLayoutTransform(1.f, InvalidationInfo.InvalidatedPaintLocation));
			const FPaintGeometry InvalidatedPaintGeometry = InvalidatedGeometry.ToPaintGeometry();

			FSlateDrawElement::MakeBox(
				InOutDrawElements,
				InOutLayerId,
				InvalidatedPaintGeometry,
				BoxBrush,
				ESlateDrawEffect::None,
				ColorWithOpacity);
		}

		if (InvalidationInfo.bIsInvalidatorPaintValid)
		{
			const FGeometry InvalidatorGeometry = FGeometry::MakeRoot(InvalidationInfo.InvalidatorPaintSize, FSlateLayoutTransform(1.f, InvalidationInfo.InvalidatorPaintLocation));
			const FPaintGeometry InvalidatorPaintGeometry = InvalidatorGeometry.ToPaintGeometry();

			FSlateDrawElement::MakeDebugQuad(
				InOutDrawElements,
				InOutLayerId,
				InvalidatorPaintGeometry,
				ColorWithOpacity);
			FSlateDrawElement::MakeBox(
				InOutDrawElements,
				InOutLayerId,
				InvalidatorPaintGeometry,
				CheckerboardBrush,
				ESlateDrawEffect::None,
				ColorWithOpacity);
		}

		if (bDisplayWidgetList)
		{
			if (NumberOfWidget < MaxNumberOfWidgetInList)
			{
				FString WidgetDisplayName = FString::Printf(TEXT("'%s' -> '%s'"), *InvalidationInfo.WidgetInvalidatorName, *InvalidationInfo.WidgetInvalidatedName);
				MakeText(WidgetDisplayName, FVector2D(0.f, (12.f * NumberOfWidget) + TextElementY), InvalidationInfo.DisplayColor);
			}
		}
		++NumberOfWidget;
	}

	if (bDisplayWidgetList && NumberOfWidget > MaxNumberOfWidgetInList)
	{
		FString WidgetDisplayName = FString::Printf(TEXT("   %d more invalidations"), NumberOfWidget - MaxNumberOfWidgetInList);
		MakeText(WidgetDisplayName, FVector2D(0.f, (12.f * NumberOfWidget) + TextElementY), FLinearColor::White);
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_SLATE_DEBUGGING