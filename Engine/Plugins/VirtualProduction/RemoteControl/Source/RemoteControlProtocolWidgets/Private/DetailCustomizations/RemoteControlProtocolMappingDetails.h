// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IDetailChildrenBuilder;
class IPropertyHandle;

#define CONSTRUCT_CUSTOMIZATION( ChildClass, ParentClass ) \
	typedef ParentClass Super;\
	ChildClass() \
		: ParentClass() \
		{}


class FRemoteControlProtocolMappingTypeCustomization
	: public IPropertyTypeCustomization
{
public:
	FRemoteControlProtocolMappingTypeCustomization()
	{}

	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}
	//~ IPropertyTypeCustomization interface end

	template<typename TTypeCustomizationType>
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<TTypeCustomizationType>();
	}
};


class FRemoteControlProtocolMappingDMXTypeCustomization final
	: public FRemoteControlProtocolMappingTypeCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FRemoteControlProtocolMappingDMXTypeCustomization, FRemoteControlProtocolMappingTypeCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end
};


class FRemoteControlProtocolMappingMIDITypeCustomization final
	: public FRemoteControlProtocolMappingTypeCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FRemoteControlProtocolMappingMIDITypeCustomization, FRemoteControlProtocolMappingTypeCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end
};


class FRemoteControlProtocolMappingOSCTypeCustomization final
	: public FRemoteControlProtocolMappingTypeCustomization
{
public:
	CONSTRUCT_CUSTOMIZATION(FRemoteControlProtocolMappingOSCTypeCustomization, FRemoteControlProtocolMappingTypeCustomization)

protected:
	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ IPropertyTypeCustomization interface end
};


#undef CONSTRUCT_CUSTOMIZATION