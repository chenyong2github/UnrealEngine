// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingEditorCommon.h"

class ITableRow;
class STableViewBase;

class FDMXPixelMappingPalatteViewModel
	: public TSharedFromThis<FDMXPixelMappingPalatteViewModel>
{
public:
	FDMXPixelMappingPalatteViewModel(TSharedPtr<FDMXPixelMappingToolkit> InToolkit)
		: ToolkitWeakPtr(InToolkit)
		, bRebuildRequested(true)
	{}

	FDMXPixelMappingPreviewWidgetViewModelArray& GetWidgetViewModels() { return WidgetViewModels; }

	void Update();

	void BuildWidgetList();

	void BuildClassWidgetList();

	void AddWidgetTemplate(FDMXPixelMappingComponentTemplatePtr Template);

private:
	FDMXPixelMappingPreviewWidgetViewModelArray WidgetViewModels;

	TMap<FString, FDMXPixelMappingComponentTemplateArray> WidgetTemplateCategories;

	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

	bool bRebuildRequested;
};

class FDMXPixelMappingPalatteWidgetViewModel
	: public TSharedFromThis<FDMXPixelMappingPalatteWidgetViewModel>
{
public:
	virtual ~FDMXPixelMappingPalatteWidgetViewModel() { }

	virtual FText GetName() const = 0;

	virtual TSharedPtr<FDMXPixelMappingComponentTemplate> GetTemplate() const { return nullptr; };

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) = 0;

	virtual void GetChildren(FDMXPixelMappingPreviewWidgetViewModelArray& OutChildren) {}
};

class FDMXPixelMappingPalatteWidgetViewModelHeader
	: public FDMXPixelMappingPalatteWidgetViewModel
{
public:
	virtual FText GetName() const override { return GroupName; }

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) override;

	virtual void GetChildren(FDMXPixelMappingPreviewWidgetViewModelArray& OutChildren) override;

	FDMXPixelMappingPreviewWidgetViewModelArray Children;

public:
	FText GroupName;
};

class FDMXPixelMappingPalatteWidgetViewModelTemplate
	: public FDMXPixelMappingPalatteWidgetViewModel
{
public:
	virtual FText GetName() const override;

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) override;

	virtual TSharedPtr<FDMXPixelMappingComponentTemplate> GetTemplate() const { return Template.Pin(); };

public:
	TWeakPtr<FDMXPixelMappingComponentTemplate> Template;
};
