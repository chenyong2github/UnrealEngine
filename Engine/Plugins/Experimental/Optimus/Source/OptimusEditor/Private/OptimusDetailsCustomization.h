// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "IPropertyTypeCustomization.h"
#include "IDetailCustomNodeBuilder.h"
#include "PropertyCustomizationHelpers.h"


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

	void SetAllowParameters(const bool bInAllowParameters);

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
	TSharedRef<FOptimusHLSLSyntaxHighlighter> SyntaxHighlighterMain;

	TSharedPtr<IPropertyHandle> DeclarationsProperty;
	TSharedPtr<IPropertyHandle> ShaderTextProperty;
	TSharedPtr<IPropertyHandle> DiagnosticsProperty;

	TSharedPtr<SScrollBar> HorizontalScrollbar;
	TSharedPtr<SScrollBar> VerticalScrollbar;

	TSharedPtr<SMultiLineEditableText> ShaderEditor;

	FText GetShaderText() const;

	void UpdateDiagnostics();
	void OnPropertyChanged(UObject *InObject, FPropertyChangedEvent& InChangedEvent);

	TArray<UObject *> InspectedObjects;
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