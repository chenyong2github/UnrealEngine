// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Internationalization/Text.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundNodeDetailCustomization.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class IDetailLayoutBuilder;
class IMemberDefaultLiteralCustomization;

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		class FMetasoundVariableDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable, FMetasoundVariableDetailCustomization>
		{
		public:
			static const FText MemberNameText;

			FMetasoundVariableDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable, FMetasoundVariableDetailCustomization>()
			{
			}

			virtual ~FMetasoundVariableDetailCustomization() = default;

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
