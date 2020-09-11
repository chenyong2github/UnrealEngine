// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraStackRowPerfWidget.h"

#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"

#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackScriptItemGroup.h"

void SNiagaraStackRowPerfWidget::Construct(const FArguments& InArgs, UNiagaraStackEntry* InStackEntry)
{
	StackEntry = InStackEntry;
	StatEnabledVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.DetailedVMScriptStats"));
	
	TSharedRef<SWidget> PerfWidget =
		SNew(SBox)
		.Visibility(this, &SNiagaraStackRowPerfWidget::IsVisible)
        .HeightOverride(16)
		.WidthOverride(70)
        .HAlign(HAlign_Right)
        .VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			  .HAlign(HAlign_Right)
			  .VAlign(VAlign_Top)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
	            [
	                // displaying evaluation type
		            SNew(STextBlock)
		            .Margin(FMargin(0, 0, 3, 1))
		            .Justification(ETextJustify::Right)
		            .Font(FNiagaraEditorWidgetsStyle::Get().GetFontStyle("NiagaraEditor.Stack.Stats.EvalTypeFont"))
		            .ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.EvalTypeColor"))
		            .Text(this, &SNiagaraStackRowPerfWidget::GetEvalTypeDisplayText)
	            ]
	            + SHorizontalBox::Slot()
                [
	                // displaying perf cost
		            SNew(STextBlock)
		            .Justification(ETextJustify::Right)
		            .Margin(FMargin(0, 0, 0, 1))
		            .Font(this, &SNiagaraStackRowPerfWidget::GetPerformanceDisplayTextFont)
		            .ColorAndOpacity(this, &SNiagaraStackRowPerfWidget::GetPerformanceDisplayTextColor)
		            .Text(this, &SNiagaraStackRowPerfWidget::GetPerformanceDisplayText)
                ]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(30, 0, 0, 2))
            .VAlign(VAlign_Bottom)
            [
				SNew(SWrapBox)
				.UseAllottedWidth(true)
			    +SWrapBox::Slot()
                [
                    // Placeholder brush to fill the remaining space
                    SNew(SBox)
                    .HeightOverride(2)
                    .WidthOverride(this, &SNiagaraStackRowPerfWidget::GetPlaceholderBrushWidth)
                    [
                        SNew(SColorBlock)
                        .Color(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.RuntimePlaceholderColor"))
                    ]
                ]
                +SWrapBox::Slot()
                [
                    // Colored visualization brush
                    SNew(SBox)
                    .HeightOverride(2)
                    .WidthOverride(this, &SNiagaraStackRowPerfWidget::GetVisualizationBrushWidth)
                    [
                        SNew(SColorBlock)
                        .Color(this, &SNiagaraStackRowPerfWidget::GetBrushUsageColor)
                    ]
                ]
            ]
		];

	ChildSlot
	[
		PerfWidget
	];
}

void SNiagaraStackRowPerfWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	GroupOverallTime = CalculateGroupOverallTime();
	StackEntryTime = CalculateStackEntryTime();
}

FOptionalSize SNiagaraStackRowPerfWidget::GetVisualizationBrushWidth() const
{
	if (IsGroupHeaderEntry())
	{
		return 40;
	}
	float PercentageFactor = GroupOverallTime == 0 ? 0 : StackEntryTime / GroupOverallTime;
	return 40 * FMath::Min(PercentageFactor, 1.0f);
}

FOptionalSize SNiagaraStackRowPerfWidget::GetPlaceholderBrushWidth() const
{
	return 40 - GetVisualizationBrushWidth().Get();
}

FLinearColor SNiagaraStackRowPerfWidget::GetBrushUsageColor() const
{
	ENiagaraScriptUsage Usage = GetUsage();
	if (Usage == ENiagaraScriptUsage::ParticleUpdateScript)
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.RuntimeUsageColorParticleUpdate");
	}
	if (Usage == ENiagaraScriptUsage::ParticleSpawnScript)
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.RuntimeUsageColorParticleSpawn");
	}
	return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.RuntimeUsageColorDefault");
}

bool SNiagaraStackRowPerfWidget::HasPerformanceData() const
{
#if STATS
	bool IsPerfCaptureEnabled = StatEnabledVar && StatEnabledVar->GetBool() && StackEntry->GetEmitterViewModel().IsValid();
	return IsPerfCaptureEnabled && (IsGroupHeaderEntry() || IsModuleEntry()) && (StackEntry->GetExecutionCategoryName()	== UNiagaraStackEntry::FExecutionCategoryNames::Particle);
#else
	return false;
#endif
}

EVisibility SNiagaraStackRowPerfWidget::IsVisible() const
{
	return HasPerformanceData() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SNiagaraStackRowPerfWidget::GetPerformanceDisplayText() const
{
	if (IsGroupHeaderEntry())
	{
		if (GroupOverallTime == 0)
		{
			return FText::FromString(FString("N/A"));
		}

		FNumberFormattingOptions Options;
		Options.MinimumIntegralDigits = 1;

		FString Format = "{0}ms";
		Options.MinimumFractionalDigits = 2;
		Options.MaximumFractionalDigits = 2;
		return FText::Format(FText::FromString(Format), FText::AsNumber(GroupOverallTime, &Options));
	}
	FNumberFormattingOptions Options;
	Options.MinimumIntegralDigits = 1;
	Options.MinimumFractionalDigits = 1;
	Options.MaximumFractionalDigits = 1;
	Options.RoundingMode = HalfToZero;
	float RuntimeFactor = GroupOverallTime == 0 ? 0 : StackEntryTime / GroupOverallTime;
	return FText::Format(FText::FromString(FString("{0}%")), FText::AsNumber(RuntimeFactor * 100, &Options));
}

FText SNiagaraStackRowPerfWidget::GetEvalTypeDisplayText() const
{
	if (!IsGroupHeaderEntry() || GroupOverallTime == 0)
	{
		return FText();
	}
	return FText::FromString(GetEvaluationType() == ENiagaraStatEvaluationType::Maximum ? "Max" : "Avg");
}

FSlateColor SNiagaraStackRowPerfWidget::GetPerformanceDisplayTextColor() const
{
	if (IsEntrySelected())
	{
		return FSlateColor(FLinearColor::Black);
	}
	if (IsGroupHeaderEntry())
	{
		return FSlateColor(FLinearColor::White);
	}
	float RuntimeFactor = GroupOverallTime == 0 ? 0 : StackEntryTime / GroupOverallTime;
	if (RuntimeFactor < 0.25)
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.LowCostColor");
	}
	if (RuntimeFactor < 0.5)
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.MediumCostColor");
	}
	if (RuntimeFactor < 0.75)
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.HighCostColor");
	}
	return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Stats.MaxCostColor");
}

FSlateFontInfo SNiagaraStackRowPerfWidget::GetPerformanceDisplayTextFont() const
{
	if (IsGroupHeaderEntry())
	{
		return FNiagaraEditorWidgetsStyle::Get().GetFontStyle("NiagaraEditor.Stack.Stats.GroupFont");
	}
	return FNiagaraEditorWidgetsStyle::Get().GetFontStyle("NiagaraEditor.Stack.Stats.DetailFont");
}

bool SNiagaraStackRowPerfWidget::IsGroupHeaderEntry() const
{
	return StackEntry.IsValid() && StackEntry->IsA(UNiagaraStackScriptItemGroup::StaticClass());
}

bool SNiagaraStackRowPerfWidget::IsModuleEntry() const
{
	return StackEntry.IsValid() && StackEntry->IsA(UNiagaraStackModuleItem::StaticClass());
}

bool SNiagaraStackRowPerfWidget::IsEntrySelected() const
{
	if (!StackEntry.IsValid())
	{
		return false;
	}
	if (!StackEntry->GetSystemViewModel()->GetSelectionViewModel())
	{
		return false;
	}
	return StackEntry->GetSystemViewModel()->GetSelectionViewModel()->ContainsEntry(StackEntry.Get());
}

UNiagaraEmitter* SNiagaraStackRowPerfWidget::GetEmitter() const
{
	if (!StackEntry->GetEmitterViewModel())
	{
		return nullptr;
	}
	return StackEntry->GetEmitterViewModel().Get()->GetEmitter();
}

ENiagaraScriptUsage SNiagaraStackRowPerfWidget::GetUsage() const
{
	UNiagaraEmitter* Emitter = GetEmitter();
	if (!Emitter)
	{
		return ENiagaraScriptUsage::Function;
	}
	FName SubcategoryName = StackEntry->GetExecutionSubcategoryName();
	if (SubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Event)
	{
		return ENiagaraScriptUsage::ParticleEventScript;
	}
	if (SubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Update)
	{
		return ENiagaraScriptUsage::ParticleUpdateScript;
	}
	if (SubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::SimulationStage)
	{
		return ENiagaraScriptUsage::ParticleSimulationStageScript;
	}
	return ENiagaraScriptUsage::ParticleSpawnScript;
}

ENiagaraStatEvaluationType SNiagaraStackRowPerfWidget::GetEvaluationType() const
{
	return StackEntry.IsValid()
		       ? StackEntry->GetSystemViewModel()->StatEvaluationType
		       : ENiagaraStatEvaluationType::Average;
}

float SNiagaraStackRowPerfWidget::CalculateGroupOverallTime() const
{
#if STATS
	if (!HasPerformanceData())
	{
		return 0;
	}
	UNiagaraEmitter* Emitter = GetEmitter();
	if (!Emitter)
	{
		return 0;
	}
	FString StatScopeName = "Main";
	TStatId StatId = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>(StatScopeName);
	return FPlatformTime::ToMilliseconds(Emitter->GetRuntimeStat(StatId.GetName(), GetUsage(), GetEvaluationType()));
#else
	return 0;
#endif
}

float SNiagaraStackRowPerfWidget::CalculateStackEntryTime() const
{
#if STATS
	if (!HasPerformanceData())
	{
		return 0;
	}
	if (IsGroupHeaderEntry())
	{
		return GroupOverallTime;
	}

	UNiagaraEmitter* Emitter = GetEmitter();
	if (!Emitter || !IsModuleEntry())
	{
		return 0;
	}

	UNiagaraStackModuleItem* ModuleItem = Cast<UNiagaraStackModuleItem>(StackEntry);
	UNiagaraNodeFunctionCall& FunctionNode = ModuleItem->GetModuleNode();
	ENiagaraScriptUsage Usage = GetUsage();
	FString StatScopeName = FunctionNode.GetFunctionName() + "_Emitter";
	TStatId StatId = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>(StatScopeName);
	FString StatName = StatId.GetName().ToString();
	return FPlatformTime::ToMilliseconds(Emitter->GetRuntimeStat(StatId.GetName(), Usage, GetEvaluationType()));
#else
	return 0;
#endif
}
