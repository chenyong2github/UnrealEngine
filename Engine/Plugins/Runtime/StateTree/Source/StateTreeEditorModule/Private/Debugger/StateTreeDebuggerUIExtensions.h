// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"

struct FStateTreeEditorNode;
enum class EStateTreeConditionEvaluationMode : uint8;

class SWidget;
class IPropertyHandle;
class IDetailLayoutBuilder;
class FDetailWidgetRow;
class FStateTreeViewModel;

namespace UE::StateTreeEditor::DebuggerExtensions
{

TSharedRef<SWidget> CreateStateWidget(TSharedPtr<IPropertyHandle> EnabledProperty);
TSharedRef<SWidget> CreateEditorNodeWidget(TSharedPtr<IPropertyHandle> Shared);

void OnConditionEvaluationModeChanged(TSharedPtr<IPropertyHandle> StructProperty, EStateTreeConditionEvaluationMode Mode);
void OnStateEnableToggled(TSharedPtr<IPropertyHandle> StructProperty);
void OnTaskEnableToggled(TSharedPtr<IPropertyHandle> StructProperty);

}; // UE::StateTreeEditor::DebuggerExtensions
