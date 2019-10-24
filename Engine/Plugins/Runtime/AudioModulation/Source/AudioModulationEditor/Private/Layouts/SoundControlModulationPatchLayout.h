// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "Misc/Attribute.h"
#include "PropertyHandle.h"
#include "SoundModulationControlComboBox.h"

// Forward Declarations
class SSearchableComboBox;
struct FSoundModulationSettings;


class FSoundModulationPatchLayoutCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSoundModulationPatchLayoutCustomization>();
	}

	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	//~ End IPropertyTypeCustomization

	virtual void CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder);
};

class FSoundVolumeModulationPatchLayoutCustomization : public FSoundModulationPatchLayoutCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSoundVolumeModulationPatchLayoutCustomization>();
	}

	virtual void CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder) override;
};

class FSoundPitchModulationPatchLayoutCustomization : public FSoundModulationPatchLayoutCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSoundPitchModulationPatchLayoutCustomization>();
	}

	virtual void CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder) override;
};

class FSoundLPFModulationPatchLayoutCustomization : public FSoundModulationPatchLayoutCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSoundLPFModulationPatchLayoutCustomization>();
	}

	virtual void CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder) override;
};

class FSoundHPFModulationPatchLayoutCustomization : public FSoundModulationPatchLayoutCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSoundHPFModulationPatchLayoutCustomization>();
	}

	virtual void CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder) override;
};

class FSoundControlModulationPatchLayoutCustomization : public FSoundModulationPatchLayoutCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FSoundControlModulationPatchLayoutCustomization>();
	}

	virtual void CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder) override;

protected:
	TSharedPtr<SSearchableComboBox> ControlComboBox;
};
