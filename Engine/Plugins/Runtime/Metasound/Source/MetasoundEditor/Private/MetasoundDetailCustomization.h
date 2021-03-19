// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IDetailCustomization.h"
#include "SGraphActionMenu.h"
#include "Types/SlateEnums.h"
#include "UObject/NameTypes.h"

// Forward Declarations
class FPropertyRestriction;
class IDetailLayoutBuilder;
struct FPointerEvent;

namespace Metasound
{
	namespace Editor
	{
		class FMetasoundDetailCustomization : public IDetailCustomization
		{
		public:
			FMetasoundDetailCustomization(FName InDocumentPropertyName);

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface

		private:
			FName GetMetadataRootClassPath() const;
			FName GetMetadataPropertyPath() const;

			FName DocumentPropertyName;

			TSharedPtr<SGraphActionMenu> GraphActionMenu;
		};
	} // namespace Editor
} // namespace Metasound