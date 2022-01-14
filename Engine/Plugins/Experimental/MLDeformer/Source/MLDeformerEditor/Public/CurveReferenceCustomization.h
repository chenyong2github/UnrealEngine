// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "IDetailCustomization.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "IDetailPropertyExtensionHandler.h"
#include "UObject/Object.h"

class USkeleton;

class FCurveReferenceCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

protected:
	void SetSkeleton(TSharedRef<IPropertyHandle> StructPropertyHandle);
	virtual void SetPropertyHandle(TSharedRef<IPropertyHandle> StructPropertyHandle);
	TSharedPtr<IPropertyHandle> FindStructMemberProperty(TSharedRef<IPropertyHandle> PropertyHandle, const FName& PropertyName);

	// Property to change after curve has been picked.
	TSharedPtr<IPropertyHandle> CurveNameProperty;

	// The Skeleton we get the curves from.
	TObjectPtr<USkeleton> Skeleton;

private:
	// Curve widget delegates
	virtual void OnCurveSelectionChanged(const FString& Name);
	virtual FString OnGetSelectedCurve() const;
	virtual USkeleton* OnGetSkeleton() const;
};
