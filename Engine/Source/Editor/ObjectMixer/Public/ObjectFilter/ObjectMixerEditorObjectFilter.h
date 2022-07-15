// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/LightComponent.h"
#include "UObject/Class.h"

#include "ObjectMixerEditorObjectFilter.generated.h"

UENUM(BlueprintType)
enum class EObjectMixerPropertyInheritanceInclusionOptions : uint8
{
	None, // Get only the properties in the specified classes without considering parent or child classes + Specified Class
	IncludeOnlyImmediateParent, // Get properties from the class the specified class immediately derives from, but not their parents + Specified Class
	IncludeOnlyImmediateChildren, // Get properties from child classes but not child classes of child classes + Specified Class
	IncludeOnlyImmediateParentAndChildren, // IncludeOnlyImmediateParent + IncludeOnlyImmediateChildren + Specified Class
	IncludeAllParents, // Go up the chain of Super classes to get all properties in the class' ancestry + Specified Class
	IncludeAllChildren, // Get properties from all derived classes recursively + Specified Class
	IncludeAllParentsAndChildren, // IncludeAllParents + IncludeAllChildren + Specified Class
	IncludeAllParentsAndOnlyImmediateChildren, // IncludeAllParents + IncludeOnlyImmediateChildren + Specified Class
	IncludeOnlyImmediateParentAndAllChildren // IncludeOnlyImmediateParent + IncludeAllChildren + Specified Class
};

/**
 * Native class for filtering object types to Object Mixer.
 * Native C++ classes should inherit directly from this class.
 */
UCLASS(Abstract, Blueprintable)
class OBJECTMIXEREDITOR_API UObjectMixerObjectFilter : public UObject
{
	GENERATED_BODY()
public:
	
	/*
	 * Return the basic object types you want to filter for in your level.
	 * For example, if you want to work with Lights, return ULightComponentBase.
	 * If you also want to see the properties for parent or child classes,
	 * override the GetObjectMixerPropertyInheritanceInclusionOptions and GetForceAddedColumns functions.
	 */
	virtual TArray<UClass*> GetObjectClassesToFilter() const { return {}; }

	/*
	 * Get the text to display for the object name/label.
	 * This is useful if one of your classes is a component type and you want the label of the component's owning actor, for example.
	 * If not overridden, this returns the object's name.
	 */
	virtual FText GetRowDisplayName(UObject* InObject) const;

	/*
	 * Controls how to display the row's visibility icon. Return true if the object should be visible.
	 * Generally this should work like the Scene Outliner does.
	 * If not overridden, we use the Editor Visibility of the object's AActor outer (unless it's an actor itself).
	 */
	virtual bool GetRowEditorVisibility(UObject* InObject) const;

	/*
	 * Controls what happens when the row's visibility icon is clicked.
	 * Generally this should work like the Scene Outliner does.
	 * If not overridden, we set the Editor Visibility of the object's AActor outer (unless it's an actor itself).
	 */
	virtual void OnSetRowEditorVisibility(UObject* InObject, bool bNewIsVisible) const;

	/*
	 * Specify a list of property names corresponding to columns you want to show by default.
	 * For example, you can specify "Intensity" and "LightColor" to show only those property columns by default in the UI.
	 * Columns not specified will not be shown by default but can be enabled by the user in the UI.
	 */
	virtual TArray<FName> GetColumnsToShowByDefault() const;

	/*
	 * Specify a list of property names corresponding to columns you don't want to ever show.
	 * For example, you can specify "Intensity" and "LightColor" to ensure that they can't be enabled or shown in the UI.
	 * Columns not specified can be enabled by the user in the UI.
	 */
	virtual TArray<FName> GetColumnsFilter() const;

	/*
	 * Specify a list of property names found in parent classes you want to show that aren't in the specified classes.
	 * Note that properties specified here do not override the properties specified in GetColumnsFilter(),
	 * but do override the supported property tests so these will appear even if ShouldIncludeUnsupportedProperties returns false.
	 * For example, a ULightComponent displays "LightColor" in the editor's details panel,
	 * but ULightComponent itself doesn't have a property named "LightColor". Instead it's in its parent class, ULightComponentBase.
	 * In this scenario, ULightComponent is specified and PropertyInheritanceInclusionOptions is None, so "LightColor" won't appear by default.
	 * Specify "LightColor" in this function to ensure that "LightColor" will appear as a column as long as
	 * the property is accessible to one of the specified classes regardless of which parent class it comes from.
	 */
	virtual TArray<FName> GetForceAddedColumns() const { return {}; }

	/*
	 * Specify whether we should return only the properties of the specified classes or the properties of parent and child classes.
	 * Defaults to 'None' which only considers the properties of the specified classes.
	 * If you're not seeing all the properties you expected, try overloading this function.
	 */
	virtual EObjectMixerPropertyInheritanceInclusionOptions GetObjectMixerPropertyInheritanceInclusionOptions() const;

	/*
	 * If true, properties that are not visible in the details panel and properties not supported by SSingleProperty will be selectable.
	 * Defaults to false.
	 */
	virtual bool ShouldIncludeUnsupportedProperties() const;

	static TArray<UClass*> GetParentAndChildClassesFromSpecifiedClasses(
		const TArray<UClass*>& InSpecifiedClasses, EObjectMixerPropertyInheritanceInclusionOptions Options);

	static EFieldIterationFlags GetDesiredFieldIterationFlags(const bool bIncludeInheritedProperties);

	// Folder view should reflect outliner folder structure if possible
	// Mute/solo lights

protected:

	/*
	 * Given a set of property names you wish to include, returns a list of all other properties on InObject not found in ExcludeList.
	 * Useful when defining default visible columns in a list view.
	 */
	TArray<FName> GenerateIncludeListFromExcludeList(const TSet<FName>& ExcludeList) const;
};

/**
 * Script class for filtering object types to Object Mixer.
 * Blueprint classes should inherit directly from this class.
 */
UCLASS(Abstract, Blueprintable)
class OBJECTMIXEREDITOR_API UObjectMixerBlueprintObjectFilter : public UObjectMixerObjectFilter
{
	GENERATED_BODY()
public:
	
	/*
	 * Return the basic object types you want to filter for in your level.
	 * For example, if you want to work with Lights, return ULightComponentBase.
	 * If you also want to see the properties for parent or child classes,
	 * override the GetObjectMixerPropertyInheritanceInclusionOptions and GetForceAddedColumns functions.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	TArray<UClass*> GetObjectClassesToFilter() const override;

	TArray<UClass*> GetObjectClassesToFilter_Implementation() const
	{
		return Super::GetObjectClassesToFilter();
	}

	/*
	 * Get the text to display for the object name/label.
	 * This is useful if one of your classes is a component type and you want the label of the component's owning actor, for example.
	 * If not overridden, this returns the object's name.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	FText GetRowDisplayName(UObject* InObject) const override;
	
	FText GetRowDisplayName_Implementation(UObject* InObject) const
	{
		return Super::GetRowDisplayName(InObject);
	}
	
	/*
	 * Controls how to display the row's visibility icon. Return true if the object should be visible.
	 * Generally this should work like the Scene Outliner does.
	 * If not overridden, we use the Editor Visibility of the object's AActor outer (unless it's an actor itself).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	bool GetRowEditorVisibility(UObject* InObject) const override;

	bool GetRowEditorVisibility_Implementation(UObject* InObject) const
	{
		return Super::GetRowEditorVisibility(InObject);
	}
	
	/*
	 * Controls what happens when the row's visibility icon is clicked.
	 * Generally this should work like the Scene Outliner does.
	 * If not overridden, we set the Editor Visibility of the object's AActor outer (unless it's an actor itself).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	void OnSetRowEditorVisibility(UObject* InObject, bool bNewIsVisible) const override;

	void OnSetRowEditorVisibility_Implementation(UObject* InObject, bool bNewIsVisible) const
	{
		return Super::OnSetRowEditorVisibility(InObject, bNewIsVisible);
	}
	
	/*
	 * Specify a list of property names corresponding to columns you want to show by default.
	 * For example, you can specify "Intensity" and "LightColor" to show only those property columns by default in the UI.
	 * Columns not specified will not be shown by default but can be enabled by the user in the UI.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	TArray<FName> GetColumnsToShowByDefault() const override;
	
	TArray<FName> GetColumnsToShowByDefault_Implementation() const
	{
		return Super::GetColumnsToShowByDefault();
	}
	
	/*
	 * Specify a list of property names corresponding to columns you don't want to ever show.
	 * For example, you can specify "Intensity" and "LightColor" to ensure that they can't be enabled or shown in the UI.
	 * Columns not specified can be enabled by the user in the UI.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	TArray<FName> GetColumnsFilter() const override;

	TArray<FName> GetColumnsFilter_Implementation() const
	{
		return Super::GetColumnsFilter();
	}

	/*
	 * Specify a list of property names found in parent classes you want to show that aren't in the specified classes.
	 * Note that properties specified here do not override the properties specified in GetColumnsFilter().
	 * For example, a ULightComponent displays "LightColor" in the editor's details panel,
	 * but ULightComponent itself doesn't have a property named "LightColor". Instead it's in its parent class, ULightComponentBase.
	 * In this scenario, ULightComponent is specified and PropertyInheritanceInclusionOptions is None, so "LightColor" won't appear by default.
	 * Specify "LightColor" in this function to ensure that "LightColor" will appear as a column as long as
	 * the property is accessible to one of the specified classes regardless of which parent class it comes from.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	TArray<FName> GetForceAddedColumns() const override;
	
	TArray<FName> GetForceAddedColumns_Implementation() const
	{
		return Super::GetForceAddedColumns();
	}
	
	/*
	 * Specify whether we should return only the properties of the specified classes or the properties of parent and child classes.
	 * Defaults to 'None' which only considers the properties of the specified classes.
	 * If you're not seeing all the properties you expected, try overloading this function.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	EObjectMixerPropertyInheritanceInclusionOptions GetObjectMixerPropertyInheritanceInclusionOptions() const override;

	EObjectMixerPropertyInheritanceInclusionOptions GetObjectMixerPropertyInheritanceInclusionOptions_Implementation() const
	{
		return Super::GetObjectMixerPropertyInheritanceInclusionOptions();
	}

	/*
	 * If true, properties that are not visible in the details panel and properties not supported by SSingleProperty will be selectable.
	 * Defaults to false.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Object Mixer")
	bool ShouldIncludeUnsupportedProperties() const override;

	bool ShouldIncludeUnsupportedProperties_Implementation() const
	{
		return Super::ShouldIncludeUnsupportedProperties();
	}

};
