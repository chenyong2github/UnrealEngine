// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomNodeBuilder.h"
#include "Fonts/SlateFontInfo.h"
#include "Misc/Attribute.h"

class IPropertyTypeCustomizationUtils;
class IPropertyUtilities;

namespace UE::VCamCoreEditor::Private
{
	/**
	 * Customizes the FWidgetConnectionConfig::ConnectionTargets property.
	 * Makes every key widget be a drop-down to the connection exposed its corresponding VCamWidget.
	 */
	class FConnectionTargetNodeBuilder
		: public IDetailCustomNodeBuilder
		, public TSharedFromThis<FConnectionTargetNodeBuilder>
	{
	public:

		FConnectionTargetNodeBuilder(TSharedRef<IPropertyHandle> ConnectionTargets, TAttribute<TArray<FName>> ChooseableConnections, IPropertyTypeCustomizationUtils& CustomizationUtils);

		//~ Begin IDetailCustomNodeBuilder Interface
		virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;
		virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
		virtual FName GetName() const override { return TEXT("Connection Targets"); }
		//~ End IDetailCustomNodeBuilder Interface

	private:
		
		/** Handle to FWidgetConnectionConfig::ConnectionTargets */
		TSharedRef<IPropertyHandle> ConnectionTargets;

		/** Gets the list of connections on the VCamWidget */
		TAttribute<TArray<FName>> ChooseableConnections;

		FSlateFontInfo RegularFont;
		TSharedPtr<IPropertyUtilities> PropertyUtilities;

		TArray<FString> GetChooseableConnectionsAsStringArray() const;
	};
}


