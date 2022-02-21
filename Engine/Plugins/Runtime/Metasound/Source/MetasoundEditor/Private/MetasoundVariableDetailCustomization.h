// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Internationalization/Text.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundNodeDetailCustomization.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class FMetasoundDefaultLiteralCustomizationBase;
class IDetailLayoutBuilder;

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		class FMetasoundVariableDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable>
		{
		public:
			static const FText MemberNameText;

			FMetasoundVariableDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable>()
			{
			}

			virtual ~FMetasoundVariableDetailCustomization() = default;
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
