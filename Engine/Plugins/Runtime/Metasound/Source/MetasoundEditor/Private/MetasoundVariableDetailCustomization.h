// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Internationalization/Text.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundNodeDetailCustomization.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class IDetailLayoutBuilder;
class IMetaSoundInputLiteralCustomization;

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		class FMetasoundVariableDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable>
		{
		public:
			FMetasoundVariableDetailCustomization();
			virtual ~FMetasoundVariableDetailCustomization() = default;

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface

		private:
			void SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const;

			FName GetLiteralDataType() const;

			TUniquePtr<IMetaSoundInputLiteralCustomization> LiteralCustomization;
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
