// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/DMXPixelMappingPalatteViewModel.h"
#include "Components/DMXPixelMappingOutputComponent.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Widgets/SDMXPixelMappingPaletteItem.h"
#include "DMXPixelMappingComponentReference.h"

#include "UObject/UObjectIterator.h"

void FDMXPixelMappingPalatteViewModel::Update()
{
	if (bRebuildRequested)
	{
		BuildWidgetList();

		bRebuildRequested = false;
	}
}

void FDMXPixelMappingPalatteViewModel::BuildWidgetList()
{
	WidgetViewModels.Reset();
	WidgetTemplateCategories.Reset();

	BuildClassWidgetList();

	for (TPair<FString, FDMXPixelMappingComponentTemplateArray>& Category : WidgetTemplateCategories)
	{
		TSharedPtr<FDMXPixelMappingPalatteWidgetViewModelHeader> Header = MakeShared<FDMXPixelMappingPalatteWidgetViewModelHeader>();
		Header->GroupName = FText::FromString(Category.Key);

		for (FDMXPixelMappingComponentTemplatePtr& ComponentTemplate : Category.Value)
		{
			TSharedPtr<FDMXPixelMappingPalatteWidgetViewModelTemplate> WidgetViewModelTemplate = MakeShared<FDMXPixelMappingPalatteWidgetViewModelTemplate>();
			WidgetViewModelTemplate->Template = ComponentTemplate;
			Header->Children.Add(WidgetViewModelTemplate);
		}

		WidgetViewModels.Add(Header);
	}
}

void FDMXPixelMappingPalatteViewModel::BuildClassWidgetList()
{
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* WidgetClass = *ClassIt;

		if (WidgetClass->IsChildOf(UDMXPixelMappingOutputComponent::StaticClass()))
		{
			UDMXPixelMappingOutputComponent* OutputComponent = WidgetClass->GetDefaultObject<UDMXPixelMappingOutputComponent>();

			if (OutputComponent->IsExposedToTemplate())
			{
				TSharedPtr<FDMXPixelMappingComponentTemplate> Template = MakeShared<FDMXPixelMappingComponentTemplate>(WidgetClass);
				AddWidgetTemplate(Template);
			}
		}
	}
}

void FDMXPixelMappingPalatteViewModel::AddWidgetTemplate(FDMXPixelMappingComponentTemplatePtr Template)
{
	FString Category = Template->GetCategory().ToString();

	FDMXPixelMappingComponentTemplateArray& Group = WidgetTemplateCategories.FindOrAdd(Category);
	Group.Add(Template);
}

TSharedRef<ITableRow> FDMXPixelMappingPalatteWidgetViewModelHeader::BuildRow(const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDMXPixelMappingHierarchyItemHeader, OwnerTable, SharedThis(this));
}

void FDMXPixelMappingPalatteWidgetViewModelHeader::GetChildren(FDMXPixelMappingPreviewWidgetViewModelArray& OutChildren)
{
	for (FDMXPixelMappingPreviewWidgetViewModelPtr& Child : Children)
	{
		OutChildren.Add(Child);
	}
}

FText FDMXPixelMappingPalatteWidgetViewModelTemplate::GetName() const
{ 
	return Template.Pin()->Name; 
}

TSharedRef<ITableRow> FDMXPixelMappingPalatteWidgetViewModelTemplate::BuildRow(const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDMXPixelMappingHierarchyItemTemplate, OwnerTable, SharedThis(this));
}
