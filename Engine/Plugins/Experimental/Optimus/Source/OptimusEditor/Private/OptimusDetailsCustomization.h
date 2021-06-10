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



class FOptimusType_ShaderTextCustomization : 
	public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	FOptimusType_ShaderTextCustomization();

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

	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;

	TSharedPtr<SMultiLineEditableText> ShaderEditor;

	FText GetDeclarationsText() const;
	FText GetShaderText() const;

	void OnShaderTextChanged(const FText &InText);

	FReply OnShaderTextKeyChar(const FGeometry& MyGeometry,const FCharacterEvent& InCharacterEvent);
};
