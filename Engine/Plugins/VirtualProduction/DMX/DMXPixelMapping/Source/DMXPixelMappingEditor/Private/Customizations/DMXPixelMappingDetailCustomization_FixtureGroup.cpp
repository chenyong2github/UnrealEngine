// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_FixtureGroup.h"

#include "DMXPixelMapping.h"
#include "DMXPixelMappingLayoutSettings.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Algo/Find.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Input/SButton.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingDetailCustomization_FixtureGroup"

void FDMXPixelMappingDetailCustomization_FixtureGroup::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	// Hide the Layout Script property (shown in its own panel, see SDMXPixelMappingLayoutView)
	InDetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupComponent, LayoutScript));

	// Handle size changes
	UpdateCachedScaleChildrenWithParent();
	SizeXHandle = InDetailLayout.GetProperty(UDMXPixelMappingOutputComponent::GetSizeXPropertyName(), UDMXPixelMappingOutputComponent::StaticClass());
	SizeXHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnSizePropertyPreChange));
	SizeXHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnSizePropertyChanged));

	SizeYHandle = InDetailLayout.GetProperty(UDMXPixelMappingOutputComponent::GetSizeYPropertyName(), UDMXPixelMappingOutputComponent::StaticClass());
	SizeYHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnSizePropertyPreChange));
	SizeYHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnSizePropertyChanged));

	// Remember the group being edited
	WeakFixtureGroupComponent = GetSelectedFixtureGroupComponent(InDetailLayout);

	// Listen to component changes
	UDMXPixelMappingBaseComponent::GetOnComponentAdded().AddSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentAdded);
	UDMXPixelMappingBaseComponent::GetOnComponentRemoved().AddSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentRemoved);
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	if (!RequestForceRefreshHandle.IsValid())
	{
		RequestForceRefreshHandle = FCoreDelegates::OnEndFrame.AddSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::ForceRefresh);
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component)
{
	if (!RequestForceRefreshHandle.IsValid())
	{
		RequestForceRefreshHandle = FCoreDelegates::OnEndFrame.AddSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::ForceRefresh);
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnSizePropertyPreChange()
{
	UpdateCachedScaleChildrenWithParent();
	if (!bCachedScaleChildrenWithParent)
	{
		return;
	}

	const TArray<TWeakObjectPtr<UObject>> SelectedObjects = PropertyUtilities->GetSelectedObjects();
	TArray<UDMXPixelMappingFixtureGroupComponent*> FixtureGroupComponents;
	for (TWeakObjectPtr<UObject> Object : SelectedObjects)
	{
		if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Object))
		{
			FixtureGroupComponents.Add(FixtureGroupComponent);
		}
	}
	if (FixtureGroupComponents.IsEmpty())
	{
		return;
	}

	PreEditChangeComponentToSizeMap.Reset();
	for (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent : FixtureGroupComponents)
	{
		PreEditChangeComponentToSizeMap.Add(FixtureGroupComponent, FixtureGroupComponent->GetSize());
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::OnSizePropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if(PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroup::HandleSizePropertyChanged));
	}
	else
	{
		HandleSizePropertyChanged();
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::HandleSizePropertyChanged()
{
	// Scale children if desired
	if (!bCachedScaleChildrenWithParent || 
		PreEditChangeComponentToSizeMap.IsEmpty())
	{
		return;
	}

	const TArray<TWeakObjectPtr<UObject>> SelectedObjects = PropertyUtilities->GetSelectedObjects();
	TArray<UDMXPixelMappingFixtureGroupComponent*> FixtureGroupComponents;
	for (TWeakObjectPtr<UObject> Object : SelectedObjects)
	{
		if (UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(Object))
		{
			FixtureGroupComponents.Add(FixtureGroupComponent);
		}
	}
	if (FixtureGroupComponents.IsEmpty())
	{
		return;
	}

	for (const TTuple<TWeakObjectPtr<UDMXPixelMappingFixtureGroupComponent>, FVector2D>& PreEditChangeComponentToSizeXPair : PreEditChangeComponentToSizeMap)
	{
		UDMXPixelMappingFixtureGroupComponent* const* ComponentPtr = Algo::Find(FixtureGroupComponents, PreEditChangeComponentToSizeXPair.Key.Get());
		if (!ComponentPtr)
		{
			continue;
		}

		const FVector2D GroupPosition = (*ComponentPtr)->GetPosition();
		const FVector2D OldSize = PreEditChangeComponentToSizeXPair.Value;
		const FVector2D NewSize = (*ComponentPtr)->GetSize();
		if (NewSize == FVector2D::ZeroVector || OldSize == NewSize)
		{
			// No division by zero, no unchanged values
			return;
		}

		const FVector2D RatioVector = NewSize / OldSize;
		for (UDMXPixelMappingBaseComponent* BaseChild : (*ComponentPtr)->GetChildren())
		{
			if (UDMXPixelMappingOutputComponent* Child = Cast<UDMXPixelMappingOutputComponent>(BaseChild))
			{
				Child->Modify();

				// Scale size (SetSize already clamps)
				Child->SetSize(Child->GetSize() * RatioVector);

				// Scale position
				const FVector2D ChildPosition = Child->GetPosition();
				const FVector2D NewPositionRelative = (ChildPosition - GroupPosition) * RatioVector;
				Child->SetPosition(GroupPosition + NewPositionRelative);
			}
		}
	}
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::ForceRefresh()
{
	// Reset the handles so they won't fire any changes after refreshing
	if (ensure(PropertyUtilities.IsValid()))
	{
		PropertyUtilities->ForceRefresh();
	}

	RequestForceRefreshHandle.Reset();
}

void FDMXPixelMappingDetailCustomization_FixtureGroup::UpdateCachedScaleChildrenWithParent()
{
	const UDMXPixelMappingLayoutSettings* LayoutSettings = GetDefault<UDMXPixelMappingLayoutSettings>();
	if (LayoutSettings)
	{
		bCachedScaleChildrenWithParent = LayoutSettings->bScaleChildrenWithParent;
	}
}

UDMXLibrary* FDMXPixelMappingDetailCustomization_FixtureGroup::GetSelectedDMXLibrary(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent) const
{
	if (FixtureGroupComponent)
	{
		return FixtureGroupComponent->DMXLibrary;
	}

	return nullptr;
}

UDMXPixelMappingFixtureGroupComponent* FDMXPixelMappingDetailCustomization_FixtureGroup::GetSelectedFixtureGroupComponent(const IDetailLayoutBuilder& InDetailLayout) const
{
	const TArray<TWeakObjectPtr<UObject> >& SelectedSelectedObjects = InDetailLayout.GetSelectedObjects();
	TArray<UDMXPixelMappingFixtureGroupComponent*> SelectedSelectedComponents;

	for (TWeakObjectPtr<UObject> SelectedObject : SelectedSelectedObjects)
	{
		if (UDMXPixelMappingFixtureGroupComponent* SelectedComponent = Cast<UDMXPixelMappingFixtureGroupComponent>(SelectedObject.Get()))
		{
			SelectedSelectedComponents.Add(SelectedComponent);
		}
	}

	// we only support 1 uobject editing for now
	// we set singe UObject here SDMXPixelMappingDetailsView::OnSelectedComponenetChanged()
	// and that is why we getting only one UObject from here TArray<TWeakObjectPtr<UObject>>& SDetailsView::GetSelectedObjects()
	check(SelectedSelectedComponents.Num());
	return SelectedSelectedComponents[0];
}

#undef LOCTEXT_NAMESPACE
