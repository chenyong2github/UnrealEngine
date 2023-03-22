// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Bindings/MVVMCompiledBindingLibraryCompiler.h"
#include "MVVMBlueprintViewModelContext.h"
#include "Types/MVVMFieldVariant.h"
#include "WidgetBlueprintCompiler.h"

struct FMVVMBlueprintPropertyPath;

struct FMVVMViewModelPropertyPath;
struct FMVVMBlueprintViewBinding;
class FWidgetBlueprintCompilerContext;
class UEdGraph;
class UMVVMBlueprintView;
class UMVVMViewClass;
class UWidgetBlueprintGeneratedClass;
namespace  UE::MVVM
{
	enum class EBindingMessageType : uint8;
}

namespace UE::MVVM::Private
{

struct FMVVMViewBlueprintCompiler
{
private:
	struct FCompilerUserWidgetPropertyContext;
	struct FCompilerSourceCreatorContext;
	struct FCompiledBinding;
	struct FCompilerBinding;
	struct FBindingSourceContext;

public:
	FMVVMViewBlueprintCompiler(FWidgetBlueprintCompilerContext& InCreationContext)
		: WidgetBlueprintCompilerContext(InCreationContext)
	{}


	FWidgetBlueprintCompilerContext& GetCompilerContext()
	{
		return WidgetBlueprintCompilerContext;
	}

	void AddExtension(UWidgetBlueprintGeneratedClass* Class, UMVVMViewClass* ViewExtension);
	void CleanOldData(UWidgetBlueprintGeneratedClass* ClassToClean, UObject* OldCDO);
	void CleanTemporaries(UWidgetBlueprintGeneratedClass* ClassToClean);

	/** Generate function that are hidden from the user (not on the Skeleton class). */
	void CreateFunctions(UMVVMBlueprintView* BlueprintView);
	/** Generate variable in the Skeleton class */
	void CreateVariables(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView);

	/** Add all the field path and the bindings to the library compiler. */
	bool PreCompile(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView);
	/** Compile the library and fill the view and viewclass */
	bool Compile(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension);

private:
	void CreateWidgetMap(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView);
	void CreateSourceLists(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView);
	void CreateFunctionsDeclaration(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView);

	bool PreCompileBindingSources(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView);
	bool CompileBindingSources(const FCompiledBindingLibraryCompiler::FCompileResult& CompileResult, UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension);

	bool PreCompileSourceCreators(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView);
	bool CompileSourceCreators(const FCompiledBindingLibraryCompiler::FCompileResult& CompileResult, UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension);

	bool PreCompileBindings(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView);
	bool CompileBindings(const FCompiledBindingLibraryCompiler::FCompileResult& CompileResult, UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension);

	const FCompilerSourceCreatorContext* FindViewModelSource(FGuid Id) const;

	void AddMessageForBinding(FMVVMBlueprintViewBinding& Binding, UMVVMBlueprintView* BlueprintView, const FText& MessageText, EBindingMessageType MessageType, FName ArgumentName = FName()) const;
	void AddErrorForViewModel(const FMVVMBlueprintViewModelContext& ViewModel, const FText& Message) const;

	TValueOrError<FBindingSourceContext, FText> CreateBindingSourceContext(const UMVVMBlueprintView* BlueprintView, const UWidgetBlueprintGeneratedClass* Class, const FMVVMBlueprintPropertyPath& PropertyPath, bool bIsOneTimeBinding);
	TArray<FMVVMConstFieldVariant> CreateBindingDestinationPath(const UMVVMBlueprintView* BlueprintView, const UWidgetBlueprintGeneratedClass* Class, const FMVVMBlueprintPropertyPath& PropertyPath) const;
	TValueOrError<FCompiledBinding, FText> CreateCompiledBinding(const UWidgetBlueprintGeneratedClass* Class, TArrayView<const UE::MVVM::FMVVMConstFieldVariant> GetterFields, TArrayView<const UE::MVVM::FMVVMConstFieldVariant> SetterFields, const UFunction* ConversionFunction, bool bIsComplexBinding);


	TArray<FMVVMConstFieldVariant> CreatePropertyPath(const UClass* Class, FName PropertyName, TArray<FMVVMConstFieldVariant> Properties) const;
	bool IsPropertyPathValid(TArrayView<const FMVVMConstFieldVariant> PropertyPath) const;

private:
	/**
	 * Describe the Property that need to be added (if it doesn't already exist)
	 * They can be a Widget, a viewmodel, or any object owned by the UserWidget.
	 */
	struct FCompilerUserWidgetPropertyContext
	{
		UClass* Class = nullptr;
		FName PropertyName;
		FText DisplayName;
		FString CategoryName;
		FString BlueprintSetter;
		FMVVMConstFieldVariant Field;
		// If the class is a viewmodel on the userwidget, what is the id of that viewmodel
		FGuid ViewModelId;

		bool bExposeOnSpawn = false;
		bool bPublicGetter = false;
	};
	TArray<FCompilerUserWidgetPropertyContext> CompilerUserWidgetPropertyContexts;

	/** 
	 * Describe the data initialize the view's properties/viewmodels.
	 */
	enum class ECompilerSourceCreatorType
	{
		ViewModel, // added manually
		ViewModelDynamic, // added by a long binding chain
	};
	struct FCompilerSourceCreatorContext
	{
		FMVVMBlueprintViewModelContext ViewModelContext;
		FCompiledBindingLibraryCompiler::FFieldPathHandle ReadPropertyPath;
		ECompilerSourceCreatorType Type = ECompilerSourceCreatorType::ViewModel;
		FString SetterFunctionName;
		UEdGraph* SetterGraph = nullptr;
	};
	TArray<FCompilerSourceCreatorContext> CompilerSourceCreatorContexts;

	/** The compiled binding from the binding compiler. */
	struct FCompiledBinding
	{
		FCompiledBindingLibraryCompiler::FBindingHandle BindingHandle;

		// At runtime, OnFieldValueChanged
		//if the ConversionFunction exist
		// then we read the Source
		// then we call ConversionFunction with the source as an input
		// then we copy the result of the ConversionFunction to DestinationWrite
		//else if the ConversionFunction is complex
		// then we call ConversionFunction without input (SourceRead can be invalid)
		// then we copy the result of the ConversionFunction to DestinationWrite
		//else
		// then we copy SourceRead to DestiantionWrite.
		FCompiledBindingLibraryCompiler::FFieldPathHandle SourceRead;
		FCompiledBindingLibraryCompiler::FFieldPathHandle DestinationWrite;
		FCompiledBindingLibraryCompiler::FFieldPathHandle ConversionFunction;

		bool bIsConversionFunctionComplex = false;
	};

	/** 
	 * Describe a binding for the compiler.
	 * 2 FCompilerBinding will be generated if the binding is TwoWays.
	 */
	enum class ECompilerBindingType
	{
		PropertyBinding, // normal binding
		ViewModelDynamic, // added by a long binding chain
	};
	struct FCompilerBinding
	{
		ECompilerBindingType Type = ECompilerBindingType::PropertyBinding;
		int32 BindingIndex = INDEX_NONE;
		int32 UserWidgetPropertyContextIndex = INDEX_NONE;
		int32 SourceCreatorContextIndex = INDEX_NONE;
		bool bSourceIsUserWidget = false;
		bool bFieldIdNeeded = false;
		bool bIsForwardBinding = false;

		FCompiledBindingLibraryCompiler::FFieldIdHandle FieldIdHandle;
		FCompiledBinding CompiledBinding;
		FName DynamicViewModelName;
	};
	TArray<FCompilerBinding> CompilerBindings;

	/**
	 * Source path of a binding that contains a FieldId that we can register/bind to
	 */
	struct FBindingSourceContext
	{
		int32 BindingIndex = INDEX_NONE;
		bool bIsForwardBinding = false;
		// Complex binding do not need to add the PropertyPath to the binding info.
		bool bIsComplexBinding = false;

		const UClass* SourceClass = nullptr;
		// The property that are registering to.
		FFieldNotificationId FieldId;
		// The path always start with the UserWidget as self.
		// Viewmodel.Field.SubProperty.SubProperty
		// or Widget.Field.SubProperty.SubProperty
		// or Field.SubProperty.SubProperty
		TArray<UE::MVVM::FMVVMConstFieldVariant> PropertyPath;

		// The source if it's a property
		int32 UserWidgetPropertyContextIndex = INDEX_NONE;
		// The source if it's a property
		int32 SourceCreatorContextIndex = INDEX_NONE;
		// The source is the UserWidget itself
		bool bIsRootWidget = false;
	};
	TArray<FBindingSourceContext> BindingSourceContexts;

	TMap<FName, UWidget*> WidgetNameToWidgetPointerMap;
	FWidgetBlueprintCompilerContext& WidgetBlueprintCompilerContext;
	FCompiledBindingLibraryCompiler BindingLibraryCompiler;
	bool bAreSourcesCreatorValid = true;
	bool bAreSourceContextsValid = true;
	bool bIsBindingsValid = true;
};

} //namespace
