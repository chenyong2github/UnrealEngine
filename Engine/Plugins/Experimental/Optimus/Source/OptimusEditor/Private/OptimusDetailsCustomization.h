// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "IPropertyTypeCustomization.h"


class FOptimusHLSLSyntaxHighlighter;
class SMultiLineEditableText;
class SScrollBar;


class FOptimusDataTypeRefCustomization : 
	public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization overrides
	void CustomizeHeader( 
		TSharedRef<IPropertyHandle> InPropertyHandle, 
		FDetailWidgetRow& InHeaderRow, 
		IPropertyTypeCustomizationUtils& InCustomizationUtils 
		) override;

	void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle, 
		IDetailChildrenBuilder& InChildBuilder, 
		IPropertyTypeCustomizationUtils& InCustomizationUtils
		) override;

private:	
	FOptimusDataTypeHandle GetCurrentDataType() const;
	void OnDataTypeChanged(FOptimusDataTypeHandle InDataType);

	FText GetDeclarationText() const;

	TSharedPtr<IPropertyHandle> TypeNameProperty;
	TAttribute<FOptimusDataTypeHandle> CurrentDataType;

};


class FOptimusDataDomainCustomization :
	public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	FOptimusDataDomainCustomization();

	// IPropertyTypeCustomization overrides
	void CustomizeHeader(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		FDetailWidgetRow& InHeaderRow,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;

	void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		IDetailChildrenBuilder& InChildBuilder,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override
	{ }

private:
	TArray<FName> ContextNames;
};


class FOptimusMultiLevelDataDomainCustomization :
	public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	FOptimusMultiLevelDataDomainCustomization();

	// IPropertyTypeCustomization overrides
	void CustomizeHeader(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		FDetailWidgetRow& InHeaderRow,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;

	void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		IDetailChildrenBuilder& InChildBuilder,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override
	{ }

private:
	TArray<TSharedRef<TArray<FName>>> NestedContextNames;
};


class FOptimusShaderTextCustomization : 
	public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	FOptimusShaderTextCustomization();

	// IPropertyTypeCustomization overrides
	void CustomizeHeader(
	    TSharedRef<IPropertyHandle> InPropertyHandle,
	    FDetailWidgetRow& InHeaderRow,
	    IPropertyTypeCustomizationUtils& InCustomizationUtils) override;

	void CustomizeChildren(
	    TSharedRef<IPropertyHandle> InPropertyHandle,
	    IDetailChildrenBuilder& InChildBuilder,
	    IPropertyTypeCustomizationUtils& InCustomizationUtils) override {}

private:
	TSharedRef<FOptimusHLSLSyntaxHighlighter> SyntaxHighlighter;
	TSharedRef<FOptimusHLSLSyntaxHighlighter> SyntaxHighlighterMain;

	TSharedPtr<IPropertyHandle> DeclarationsProperty;
	TSharedPtr<IPropertyHandle> ShaderTextProperty;
	TSharedPtr<IPropertyHandle> DiagnosticsProperty;

	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;

	TSharedPtr<SMultiLineEditableText> ShaderEditor;

	FText GetDeclarationsText() const;
	FText GetShaderText() const;

	void OnShaderTextChanged(const FText &InText);

	void UpdateDiagnostics();
	void OnPropertyChanged(UObject *InObject, FPropertyChangedEvent& InChangedEvent);

	FReply OnShaderTextKeyChar(const FGeometry& MyGeometry,const FCharacterEvent& InCharacterEvent);

	TArray<UObject *> InspectedObjects;
};
