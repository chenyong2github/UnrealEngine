// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingRuntimeCommon.h"

class UDMXPixelMapping;
class FDMXPixelMappingToolkit;
class FDMXPixelMappingComponentReference;
class FMenuBuilder;

/**
 * Shared Pixel Mapping editor functions
 */
class FDMXPixelMappingEditorUtils
{
public:
	/**
	 * Check ability to rename the component.
	 *
	 * @param InToolkit				Pixel Mappint editor toolkit
	 * @param InComponent			Component reference to rename
	 * @param NewName				New name to check
	 * @param OutErrorMessage		Output parameter in case of failed reaming
	 *
	 * @return true if the component can be renamed
	 */
	static bool VerifyComponentRename(TSharedRef<FDMXPixelMappingToolkit> InToolkit, const FDMXPixelMappingComponentReference& InComponent, const FText& NewName, FText& OutErrorMessage);

	/**
	 * Rename Pixel Maping component.
	 *
	 * @param InToolkit				Pixel Mappint editor toolkit
	 * @param NewName				Old name
	 * @param NewName				New name
	 */
	static void RenameComponent(TSharedRef<FDMXPixelMappingToolkit> InToolkit, const FName& OldObjectName, const FString& NewDisplayName);

	/**
	 * Rename Pixel Maping component.
	 *
	 * @param InToolkit				Pixel Mappint editor toolkit
	 * @param InDMXPixelMapping		Pixel Mapping object
	 * @param InComponent			Component references to delete
	 * @param bCreateTransaction	If true, creates a scoped transaction for undo. Defaults to true.
	 */
	static void DeleteComponents(TSharedRef<FDMXPixelMappingToolkit> InToolkit, UDMXPixelMapping* InDMXPixelMapping, const TSet<FDMXPixelMappingComponentReference>& InComponents, bool bCreateTransaction = true );

	/**
	 * Add renderer to Pixel Mapping object
	 *
	 * @param InDMXPixelMapping		Pixel Mapping object
	 *
	 * @return Render Component pointer
	 */
	static UDMXPixelMappingRendererComponent* AddRenderer(UDMXPixelMapping* InPixelMapping);

	/**
	 * Create components commands menu
	 *
	 * @param MenuBuilder			Vertical menu builder
	 * @param InToolkit				Pixel Mappint editor toolkit
	 */
	static void CreateComponentContextMenu(FMenuBuilder& MenuBuilder, TSharedRef<FDMXPixelMappingToolkit> InToolkit);
};