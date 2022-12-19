// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VCamConnectionStructs.h"

class IDetailGroup;
class UVCamWidget;
struct FVCamConnectionTargetSettings;

namespace UE::VCamCoreEditor
{
	DECLARE_DELEGATE_OneParam(FOnTargetSettingsChanged, const FVCamConnectionTargetSettings& NewSettings);
	struct FAddConnectionArgs
	{
		IDetailGroup& DetailGroup;

		FName ConnectionName;

		/** Name of the property.  */
		FName PropertyName;
		/** A containing a FVCamConnectionTargetSettings property with name PropertyName */
		FStructOnScope StructData;

		/** Called when the settings are changed. Copy the passed settings to your UPROPERTY(). */
		FOnTargetSettingsChanged OnTargetSettingsChangedDelegate;

		/** The font to use for displaying property texts. */
		FSlateFontInfo Font;

		FAddConnectionArgs(IDetailGroup& DetailGroup, FName ConnectionName, FName PropertyName, FStructOnScope StructData, FOnTargetSettingsChanged OnTargetSettingsChangedDelegate, FSlateFontInfo Font)
			: DetailGroup(DetailGroup)
			, ConnectionName(ConnectionName)
			, PropertyName(PropertyName)
			, StructData(MoveTemp(StructData))
			, OnTargetSettingsChangedDelegate(MoveTemp(OnTargetSettingsChangedDelegate))
			, Font(MoveTemp(Font))
		{}
	};
	
	/** Passed to IConnectionTargetRemappingCustomizers to re-use functionality. */
	class VCAMCOREEDITOR_API IConnectionRemapUtils : public TSharedFromThis<IConnectionRemapUtils>
	{
	public:

		/** Adds a property row representing TargetSettings to DetailGroup. */
		virtual void AddConnection(FAddConnectionArgs Params) = 0;

		/** @return the font used for properties and details */
		virtual FSlateFontInfo GetRegularFont() const = 0;

		/**
		 * Refreshes the details view and regenerates all the customized layouts
		 * Use only when you need to remove or add complicated dynamic items
		 */
		virtual void ForceRefreshProperties() const = 0;
		
		virtual ~IConnectionRemapUtils() = default;
	};
}
