// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVariableDetailCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Internationalization/Text.h"
#include "MetasoundNodeDetailCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		const FText FMetasoundVariableDetailCustomization::MemberNameText = LOCTEXT("Variable_Name", "Variable");
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
