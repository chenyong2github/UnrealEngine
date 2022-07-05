// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IDetailCustomNodeBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "OptimusDataType.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"

class FOptimusHLSLSyntaxHighlighter;
class SExpandableArea;
class SMultiLineEditableText;
class SOptimusShaderTextDocumentTextBox;
class SScrollBar;
class UOptimusSource;


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
	TSharedPtr<IPropertyHandle> TypeObjectProperty;
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
	DECLARE_EVENT_OneParam(FOptimusMultiLevelDataDomainCustomization, FOnMultiLevelDataDomainChanged, const TArray<FName>& )
	
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

	void SetAllowParameters(const bool bInAllowParameters);

	FOnMultiLevelDataDomainChanged OnMultiLevelDataDomainChangedDelegate; 
	
private:
	void GenerateContextNames();

	TArray<TSharedRef<TArray<FName>>> NestedContextNames;
	bool bAllowParameters = false;
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

	TSharedPtr<IPropertyHandle> DeclarationsProperty;
	TSharedPtr<IPropertyHandle> ShaderTextProperty;

	TSharedPtr<SExpandableArea> ExpandableArea;
	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;

	TSharedPtr<SMultiLineEditableText> ShaderEditor;

	FText GetShaderText() const;
};


class FOptimusParameterBindingCustomization : public IPropertyTypeCustomization
{
public:
	struct FColumnSizeData
	{
		FColumnSizeData() : DataTypeColumnSize(0.5f) , DataDomainColumnSize(0.5f) {}
			
		float GetDataTypeColumnSize() const {return DataTypeColumnSize;}
		void OnDataTypeColumnResized(float InSize) {DataTypeColumnSize = InSize;}
		float GetDataDomainColumnSize() const {return DataDomainColumnSize;}
		void OnDataDomainColumnResized(float InSize) {DataDomainColumnSize = InSize;}
		
		float DataTypeColumnSize;
		float DataDomainColumnSize;	
	};
	
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	FOptimusParameterBindingCustomization();

	// IPropertyTypeCustomization overrides
	void CustomizeHeader(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		FDetailWidgetRow& InHeaderRow,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;

	void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		IDetailChildrenBuilder& InChildBuilder,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
};


class FOptimusParameterBindingArrayBuilder
	: public FDetailArrayBuilder
	, public TSharedFromThis<FOptimusParameterBindingArrayBuilder>
{
public:
	static TSharedRef<FOptimusParameterBindingArrayBuilder> MakeInstance(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InColumnSizeData,
		const bool bInAllowParameters);
	
	FOptimusParameterBindingArrayBuilder(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> InColumnSizeData,
		const bool bInAllowParameters);
	
	// FDetailArrayBuilder Interface
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;

	// Used by FOptimusParameterBindingArrayCustomization
	void GenerateWrapperStructHeaderRowContent(FDetailWidgetRow& NodeRow, TSharedRef<SWidget> NameContent);

private:
	void OnGenerateEntry(TSharedRef<IPropertyHandle> ElementProperty, int32 ElementIndex, IDetailChildrenBuilder& ChildrenBuilder) const;

	TSharedPtr<IPropertyHandleArray> ArrayProperty;

	TSharedPtr<FOptimusParameterBindingCustomization::FColumnSizeData> ColumnSizeData;

	bool bAllowParameters = false;
};


class FOptimusParameterBindingArrayCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FOptimusParameterBindingArrayCustomization>();
	}

	FOptimusParameterBindingArrayCustomization();
	
	// IPropertyTypeCustomization overrides
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		FDetailWidgetRow& InHeaderRow,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		IDetailChildrenBuilder& InChildBuilder,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
	
private:
	TSharedPtr<FOptimusParameterBindingArrayBuilder> ArrayBuilder;
	TSharedRef<FOptimusParameterBindingCustomization::FColumnSizeData> ColumnSizeData;	
};


class FOptimusValueContainerCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FOptimusValueContainerCustomization>();
	}

	FOptimusValueContainerCustomization();
	// IPropertyTypeCustomization overrides
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		FDetailWidgetRow& InHeaderRow,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle,
		IDetailChildrenBuilder& InChildBuilder,
		IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
private:
	TSharedPtr<IPropertyHandle> InnerPropertyHandle;
};


class FOptimusValidatedNameCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	FOptimusValidatedNameCustomization();

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
};


/** UI customization for UOptimusSource */
class FOptimusSourceDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

protected:
	FOptimusSourceDetailsCustomization();

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	UOptimusSource* OptimusSource = nullptr;

 	TSharedRef<FOptimusHLSLSyntaxHighlighter> SyntaxHighlighter;
 	TSharedPtr<SOptimusShaderTextDocumentTextBox> SourceTextBox;

	FText GetText() const;
	void OnTextChanged(const FText& InValue);
};
