// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDMXPixelMappingEditor, All, All);

class UDMXPixelMappingBaseComponent;

class FDMXPixelMappingToolkit;
class FDMXPixelMappingHierarchyItem;
class FDMXPixelMappingPaletteWidgetViewModel;
class FDMXPixelMappingComponentTemplate;

using FDMXPixelMappingToolkitPtr = TSharedPtr<FDMXPixelMappingToolkit>;
using FDMXPixelMappingToolkitWeakPtr = TWeakPtr<FDMXPixelMappingToolkit>;
using FDMXPixelMappingHierarchyItemWidgetModelPtr = TSharedPtr<FDMXPixelMappingHierarchyItem>;
using FDMXPixelMappingHierarchyWidgetModelWeakPtr = TWeakPtr<FDMXPixelMappingHierarchyItem>;
using FDMXPixelMappingHierarchyItemWidgetModelArr = TArray<FDMXPixelMappingHierarchyItemWidgetModelPtr>;
using FDMXPixelMappingPreviewWidgetViewModelPtr = TSharedPtr<FDMXPixelMappingPaletteWidgetViewModel>;
using FDMXPixelMappingPreviewWidgetViewModelArray = TArray<FDMXPixelMappingPreviewWidgetViewModelPtr>;
using FDMXPixelMappingComponentTemplatePtr = TSharedPtr<FDMXPixelMappingComponentTemplate>;
using FDMXPixelMappingComponentTemplateArray = TArray<FDMXPixelMappingComponentTemplatePtr>;
