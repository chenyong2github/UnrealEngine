// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMViewBlueprintCompiler.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "Blueprint/WidgetTree.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Bindings/MVVMCompiledBindingLibraryCompiler.h"
#include "Bindings/MVVMFieldPathHelper.h"
#include "EdGraphSchema_K2.h"
#include "MVVMBlueprintView.h"
#include "MVVMFunctionGraphHelper.h"
#include "MVVMSubsystem.h"
#include "MVVMViewModelBase.h"
#include "WidgetBlueprintCompiler.h"
#include "View/MVVMViewClass.h"

namespace UE::MVVM::Private
{

void FMVVMViewBlueprintCompiler::AddErrorForBinding(FMVVMBlueprintViewBinding& Binding, const UMVVMBlueprintView* View, const FString& Message) const
{
	const FString BindingName = Binding.GetDisplayNameString(View);
	WidgetBlueprintCompilerContext.MessageLog.Error(*(BindingName + TEXT(": ") + Message));
	Binding.Errors.Add(FText::FromString(BindingName + TEXT(": ") + Message));
}


void FMVVMViewBlueprintCompiler::AddExtension(UWidgetBlueprintGeneratedClass* Class, UMVVMViewClass* ViewExtension)
{
	WidgetBlueprintCompilerContext.AddExtension(Class, ViewExtension);
}


void FMVVMViewBlueprintCompiler::CleanOldData(UWidgetBlueprintGeneratedClass* ClassToClean, UObject* OldCDO)
{
	// Clean old View
	if (!WidgetBlueprintCompilerContext.Blueprint->bIsRegeneratingOnLoad && WidgetBlueprintCompilerContext.bIsFullCompile)
	{
		auto RenameObjectToTransientPackage = [](UObject* ObjectToRename)
		{
			const ERenameFlags RenFlags = REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty;

			ObjectToRename->Rename(nullptr, GetTransientPackage(), RenFlags);
			ObjectToRename->SetFlags(RF_Transient);
			ObjectToRename->ClearFlags(RF_Public | RF_Standalone | RF_ArchetypeObject);

			FLinkerLoad::InvalidateExport(ObjectToRename);
		};

		TArray<UObject*> Children;
		const bool bIncludeNestedObjects = false;
		ForEachObjectWithOuter(ClassToClean, [&Children](UObject* Child)
			{
				if (Cast<UMVVMViewClass>(Child))
				{
					Children.Add(Child);
				}
			}, bIncludeNestedObjects);		

		for (UObject* Child : Children)
		{
			RenameObjectToTransientPackage(Child);
		}
	}
}


void FMVVMViewBlueprintCompiler::CleanTemporaries(UWidgetBlueprintGeneratedClass* ClassToClean)
{
	for (FCompilerSourceCreatorContext& SourceCreatorContext : SourceCreatorContexts)
	{
		if (SourceCreatorContext.SetterGraph)
		{
			ensureMsgf(SourceCreatorContext.SetterGraph->HasAnyFlags(RF_Transient), TEXT("The graph should be temporary and should be generated automaticly."));
			// GC may not have clean the graph (GC doesn't run when bRegenerateSkeletonOnly is on)
			SourceCreatorContext.SetterGraph->Rename(nullptr, SourceCreatorContext.SetterGraph->GetOuter(), REN_DoNotDirty | REN_ForceNoResetLoaders);
			SourceCreatorContext.SetterGraph = nullptr;
		}
	}
}


void FMVVMViewBlueprintCompiler::CreateFunctions(UMVVMBlueprintView* BlueprintView)
{
	if (!bAreSourcesCreatorValid || !bIsBindingsValid)
	{
		return;
	}

	for (const FCompilerSourceCreatorContext& SourceCreator : SourceCreatorContexts)
	{
		if (SourceCreator.SetterGraph)
		{
			if (!UE::MVVM::FunctionGraphHelper::GenerateViewModelSetter(WidgetBlueprintCompilerContext, SourceCreator.SetterGraph, SourceCreator.ViewModelContext.GetViewModelName()))
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(TEXT("The setter function for '%s' could not be generated."), *SourceCreator.ViewModelContext.GetDisplayName().ToString());
				continue;
			}
		}
	}
}


void FMVVMViewBlueprintCompiler::CreateVariables(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView)
{
	if (!BlueprintView)
	{
		return;
	}

	if (!bAreSourcesCreatorValid || !bAreSourceContextsValid || !bIsBindingsValid)
	{
		return;
	}

	if (Context.GetCompileType() == EKismetCompileType::SkeletonOnly)
	{
		CreateWidgetMap(Context, BlueprintView);
		CreateSourceLists(Context, BlueprintView);
		CreateFunctionsDeclaration(Context, BlueprintView);
	}

	auto CreateVariable = [&Context](const FCompilerSourceContext& SourceContext) -> FProperty*
	{
		FEdGraphPinType ViewModelPinType(UEdGraphSchema_K2::PC_Object, NAME_None, SourceContext.Class, EPinContainerType::None, false, FEdGraphTerminalType());
		FProperty* ViewModelProperty = Context.CreateVariable(SourceContext.PropertyName, ViewModelPinType);
		if (ViewModelProperty != nullptr)
		{
			ViewModelProperty->SetPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_RepSkip
				| CPF_Transient | CPF_DuplicateTransient);
			ViewModelProperty->SetPropertyFlags(SourceContext.bExposeOnSpawn ? CPF_ExposeOnSpawn : CPF_DisableEditOnInstance);

#if WITH_EDITOR
			if (!SourceContext.BlueprintSetter.IsEmpty())
			{
				ViewModelProperty->SetMetaData(FBlueprintMetadata::MD_PropertySetFunction, *SourceContext.BlueprintSetter);
			}
			if (!SourceContext.DisplayName.IsEmpty())
			{
				ViewModelProperty->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *SourceContext.DisplayName);
			}
			if (!SourceContext.CategoryName.IsEmpty())
			{
				ViewModelProperty->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *SourceContext.CategoryName);
			}
			if (SourceContext.bExposeOnSpawn)
			{
				ViewModelProperty->SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
			}
#endif
		}
		return ViewModelProperty;
	};

	for (FCompilerSourceContext& SourceContext : SourceContexts)
	{
		SourceContext.Field = BindingHelper::FindFieldByName(Context.GetSkeletonGeneratedClass(), FMVVMBindingName(SourceContext.PropertyName));

		// The class is not linked yet. It may not be available yet.
		if (SourceContext.Field.IsEmpty())
		{
			for (FField* Field = Context.GetSkeletonGeneratedClass()->ChildProperties; Field != nullptr; Field = Field->Next)
			{
				if (Field->GetFName() == SourceContext.PropertyName)
				{
					if (FProperty* Property = CastField<FProperty>(Field))
					{
						SourceContext.Field = FMVVMFieldVariant(Property);
						break;
					}
					else
					{
						WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The field for source '%s' exists but is not a property.")
							, *SourceContext.DisplayName));
						bAreSourcesCreatorValid = false;
						continue;
					}
				}
			}
		}

		// Reuse the property if found
		if (!SourceContext.Field.IsEmpty())
		{
			if (!BindingHelper::IsValidForSourceBinding(SourceContext.Field))
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The field for source '%s' exist but will is not accessible at runtime.")
					, *SourceContext.DisplayName));
				bAreSourcesCreatorValid = false;
				continue;
			}

			const FProperty* Property = SourceContext.Field.IsProperty() ? SourceContext.Field.GetProperty() : BindingHelper::GetReturnProperty(SourceContext.Field.GetFunction());
			const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property);
			const bool bIsCompatible = ObjectProperty && SourceContext.Class->IsChildOf(ObjectProperty->PropertyClass);
			if (!bIsCompatible)
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(TEXT("There is already a property named '%s' that is not compatible with source of the same name."), *SourceContext.DisplayName);
				bAreSourceContextsValid = false;
				continue;
			}
		}

		if (SourceContext.Field.IsEmpty())
		{
			SourceContext.Field = FMVVMConstFieldVariant(CreateVariable(SourceContext));
		}

		if (SourceContext.Field.IsEmpty())
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(TEXT("The variable for '%s' could not be created."), *SourceContext.DisplayName);
			bAreSourceContextsValid = false;
			continue;
		}
	}
}


void FMVVMViewBlueprintCompiler::CreateWidgetMap(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView)
{
	// The widget tree is not created yet for SKEL class.
	//Context.GetGeneratedClass()->GetWidgetTreeArchetype()
	WidgetNameToWidgetPointerMap.Reset();

	TArray<UWidget*> Widgets;
	UWidgetBlueprint* WidgetBPToScan = Context.GetWidgetBlueprint();
	while (WidgetBPToScan != nullptr)
	{
		Widgets = WidgetBPToScan->GetAllSourceWidgets();
		if (Widgets.Num() != 0)
		{
			break;
		}
		WidgetBPToScan = WidgetBPToScan->ParentClass && WidgetBPToScan->ParentClass->ClassGeneratedBy ? Cast<UWidgetBlueprint>(WidgetBPToScan->ParentClass->ClassGeneratedBy) : nullptr;
	}

	for (UWidget* Widget : Widgets)
	{
		WidgetNameToWidgetPointerMap.Add(Widget->GetFName(), Widget);
	}
}


void FMVVMViewBlueprintCompiler::CreateSourceLists(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView)
{
	SourceContexts.Reset();
	SourceCreatorContexts.Reset();

	TSet<FGuid> ViewModelsGuid;
	TSet<FName> WidgetSources;
	for (const FMVVMBlueprintViewModelContext& ViewModelContext : BlueprintView->GetViewModels())
	{
		if (!ViewModelContext.GetViewModelId().IsValid())
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(TEXT("The viewmodel context has an invalid Guid"));
			bAreSourcesCreatorValid = false;
			continue;
		}

		if (ViewModelsGuid.Contains(ViewModelContext.GetViewModelId()))
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' is present twice.")
				, *ViewModelContext.GetViewModelId().ToString()));
			bAreSourcesCreatorValid = false;
			continue;
		}

		ViewModelsGuid.Add(ViewModelContext.GetViewModelId());

		if (ViewModelContext.GetViewModelClass() == nullptr || !ViewModelContext.IsValid())
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' has an invalid class.")
				, *ViewModelContext.GetViewModelId().ToString()));
			bAreSourcesCreatorValid = false;
			continue;
		}

		const bool bCreateSetterFunction = ViewModelContext.bCreateSetterFunction || ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual;

		int32 FoundSourceCreatorContextIndex = INDEX_NONE;
		if (Context.GetCompileType() == EKismetCompileType::SkeletonOnly)
		{
			FCompilerSourceCreatorContext SourceContext;
			SourceContext.ViewModelContext = ViewModelContext;
			SourceContext.Type = ECompilerSourceCreatorType::ViewModel;
			if (bCreateSetterFunction)
			{
				SourceContext.SetterFunctionName = TEXT("Set") + ViewModelContext.GetViewModelName().ToString();
			}
			FoundSourceCreatorContextIndex = SourceCreatorContexts.Emplace(MoveTemp(SourceContext));
		}
		else
		{
			FGuid ViewModelId = ViewModelContext.GetViewModelId();
			FoundSourceCreatorContextIndex = SourceCreatorContexts.IndexOfByPredicate([ViewModelId](const FCompilerSourceCreatorContext& Other)
				{
					return Other.ViewModelContext.GetViewModelId() == ViewModelId;
				});
		}
		checkf(FoundSourceCreatorContextIndex != INDEX_NONE, TEXT("The viewmodel was added after the skeleton was created?"));

		FCompilerSourceContext SourceVariable;
		SourceVariable.Class = ViewModelContext.GetViewModelClass();
		SourceVariable.PropertyName = ViewModelContext.GetViewModelName();
		SourceVariable.DisplayName = ViewModelContext.GetDisplayName().ToString();
		SourceVariable.CategoryName = TEXT("Viewmodel");
		SourceVariable.bExposeOnSpawn = bCreateSetterFunction;
		SourceVariable.BlueprintSetter = SourceCreatorContexts[FoundSourceCreatorContextIndex].SetterFunctionName;
		SourceContexts.Emplace(MoveTemp(SourceVariable));
	}

	bAreSourceContextsValid = bAreSourcesCreatorValid;

	// Only find the source first property and destination first property.
	//The full path will be tested later. We want to build the list of property needed.
	for (int32 Index = 0; Index < BlueprintView->GetNumBindings(); ++Index)
	{
		const FMVVMBlueprintViewBinding* BindingPtr = BlueprintView->GetBindingAt(Index);
		if (BindingPtr == nullptr)
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding number %d is invalid."), Index));
			bAreSourceContextsValid = false;
			continue;
		}
		const FMVVMBlueprintViewBinding& Binding = *BindingPtr;
		if (!Binding.bCompile)
		{
			continue;
		}

		FMVVMViewBlueprintCompiler* Self = this;
		auto GenerateCompilerSourceContext = [Self, Index, &ViewModelsGuid, &WidgetSources](const UWidgetBlueprintGeneratedClass* Class, const UMVVMBlueprintView* BlueprintView, const FMVVMBlueprintPropertyPath& PropertyPath) -> bool
		{
			if (PropertyPath.IsFromWidget())
			{
				if (PropertyPath.GetWidgetName().IsNone())
				{
					Self->WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding number %d has an invalid widget name."), Index));
					return false;
				}

				if (PropertyPath.GetWidgetName() == Class->ClassGeneratedBy->GetFName())
				{
					// it's the userwidget
					return true;
				}

				// If the widget doesn't have a property, add one automatically.
				if (!WidgetSources.Contains(PropertyPath.GetWidgetName()))
				{
					WidgetSources.Add(PropertyPath.GetWidgetName());

					UWidget** WidgetPtr = Self->WidgetNameToWidgetPointerMap.Find(PropertyPath.GetWidgetName());
					if (WidgetPtr == nullptr || *WidgetPtr == nullptr)
					{
						Self->WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding number %d has an invalid path. The widget '%s' could not be found."), Index, *PropertyPath.GetWidgetName().ToString()));
						return false;
					}

					UWidget* Widget = *WidgetPtr;
					FCompilerSourceContext SourceVariable;
					SourceVariable.Class = Widget->GetClass();
					SourceVariable.PropertyName = PropertyPath.GetWidgetName();
					SourceVariable.DisplayName = Widget->GetDisplayLabel();
					SourceVariable.CategoryName = TEXT("Widget");
					Self->SourceContexts.Emplace(MoveTemp(SourceVariable));
				}
			}
			else if (PropertyPath.IsFromViewModel())
			{
				const FMVVMBlueprintViewModelContext* SourceViewModelContext = BlueprintView->FindViewModel(PropertyPath.GetViewModelId());
				if (SourceViewModelContext == nullptr)
				{
					Self->WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding number %d has an invalid viewmodel."), Index));
					return false;
				}

				bool bFound = ViewModelsGuid.Contains(SourceViewModelContext->GetViewModelId());
				if (!bFound)
				{
					Self->WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding number %d has a viewmodel that was not added."), Index));
					return false;
				}
			}
			else
			{
				Self->WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding number %d has a unknown path type."), Index));
				return false;
			}
			return true;
		};

		if (IsForwardBinding(Binding.BindingType) || IsBackwardBinding(Binding.BindingType))
		{
			bAreSourceContextsValid = GenerateCompilerSourceContext(Context.GetSkeletonGeneratedClass(), BlueprintView, Binding.ViewModelPath)
				&& GenerateCompilerSourceContext(Context.GetSkeletonGeneratedClass(), BlueprintView, Binding.WidgetPath)
				&& bAreSourceContextsValid;
		}
	}
}


void FMVVMViewBlueprintCompiler::CreateFunctionsDeclaration(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView)
{
	for (FCompilerSourceCreatorContext& SourceCreator : SourceCreatorContexts)
	{
		if (!SourceCreator.SetterFunctionName.IsEmpty() && SourceCreator.Type == ECompilerSourceCreatorType::ViewModel)
		{
			ensure(SourceCreator.SetterGraph == nullptr);
			SourceCreator.SetterGraph = UE::MVVM::FunctionGraphHelper::CreateIntermediateFunctionGraph(
				WidgetBlueprintCompilerContext
				, SourceCreator.SetterFunctionName
				, (FUNC_BlueprintCallable | FUNC_Public)
				, TEXT("Viewmodel")
				, false);

			if (SourceCreator.SetterGraph == nullptr || SourceCreator.SetterGraph->GetFName() != FName(*SourceCreator.SetterFunctionName))
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The setter name %s already exist and could not be autogenerated."), *SourceCreator.SetterFunctionName));
			}

			UE::MVVM::FunctionGraphHelper::AddFunctionArgument(SourceCreator.SetterGraph, SourceCreator.ViewModelContext.GetViewModelClass(), "Viewmodel");
		}
	}
}


bool FMVVMViewBlueprintCompiler::PreCompile(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView)
{
	if (!bAreSourcesCreatorValid || !bAreSourceContextsValid || !bIsBindingsValid)
	{
		return false;
	}

	const int32 NumBindings = BlueprintView->GetNumBindings();
	Bindings.Reset(NumBindings*2);
	BindingSourceContexts.Reset(NumBindings*2);

	PreCompileBindingSources(Class, BlueprintView);
	PreCompileSourceCreators(Class, BlueprintView);
	PreCompileBindings(Class, BlueprintView);

	return bAreSourcesCreatorValid && bAreSourceContextsValid && bIsBindingsValid;
}


bool FMVVMViewBlueprintCompiler::Compile(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension)
{
	if (!bAreSourcesCreatorValid || !bAreSourceContextsValid || !bIsBindingsValid)
	{
		return false;
	}

	TValueOrError<FCompiledBindingLibraryCompiler::FCompileResult, FString> CompileResult = BindingLibraryCompiler.Compile();
	if (CompileResult.HasError())
	{
		WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding compilation failed. %s"), *CompileResult.GetError()));
		return false;
	}
	CompileSourceCreators(CompileResult.GetValue(), Class, BlueprintView, ViewExtension);
	CompileBindings(CompileResult.GetValue(), Class, BlueprintView, ViewExtension);

	bool bResult = bAreSourcesCreatorValid && bAreSourceContextsValid && bIsBindingsValid;
	if (bResult)
	{
		ViewExtension->BindingLibrary = MoveTemp(CompileResult.GetValue().Library);
	}

	return bResult;
}


bool FMVVMViewBlueprintCompiler::PreCompileBindingSources(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView)
{
	const int32 NumBindings = BlueprintView->GetNumBindings();
	for (int32 Index = 0; Index < NumBindings; ++Index)
	{
		FMVVMBlueprintViewBinding* BindingPtr = BlueprintView->GetBindingAt(Index);
		if (BindingPtr == nullptr)
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding at index '%d' is invalid."), Index));
			bIsBindingsValid = false;
			continue;
		}

		FMVVMBlueprintViewBinding& Binding = *BindingPtr;
		if (!Binding.bCompile)
		{
			continue;
		}

		if (IsForwardBinding(Binding.BindingType))
		{
			const TValueOrError<FBindingSourceContext, FString> CreatedBindingSourceContext = CreateBindingSourceContext(BlueprintView, Class, Binding.ViewModelPath);
			if (CreatedBindingSourceContext.HasError())
			{
				AddErrorForBinding(Binding, BlueprintView, FString::Printf(TEXT("The property path '%s' is invalid. %s")
					, *PropertyPathToString(BlueprintView, Binding.ViewModelPath)
					, *CreatedBindingSourceContext.GetError()));
				bIsBindingsValid = false;
				continue;
			}

			FBindingSourceContext BindingSourceContext = CreatedBindingSourceContext.GetValue();
			if (!IsPropertyPathValid(BindingSourceContext.PropertyPath))
			{
				AddErrorForBinding(Binding, BlueprintView, FString::Printf(TEXT("The property path '%s' is invalid."), *PropertyPathToString(BlueprintView, Binding.ViewModelPath)));
				bIsBindingsValid = false;
				continue;
			}
			if (BindingSourceContext.SourceClass == nullptr)
			{
				AddErrorForBinding(Binding, BlueprintView, TEXT("Internal error. The binding could not find its source."));
				bIsBindingsValid = false;
				continue;
			}
			if (!BindingSourceContext.bIsRootWidget && BindingSourceContext.CompilerSourceContextIndex == INDEX_NONE)
			{
				AddErrorForBinding(Binding, BlueprintView, TEXT("Internal error. The binding could not find its source."));
				bIsBindingsValid = false;
				continue;
			}

			BindingSourceContext.BindingIndex = Index;
			BindingSourceContext.bIsForwardBinding = true;
			BindingSourceContexts.Add(BindingSourceContext);
		}

		if (IsBackwardBinding(Binding.BindingType))
		{
			const TValueOrError<FBindingSourceContext, FString> CreatedBindingSourceContext = CreateBindingSourceContext(BlueprintView, Class, Binding.WidgetPath);
			if (CreatedBindingSourceContext.HasError())
			{
				AddErrorForBinding(Binding, BlueprintView, FString::Printf(TEXT("The property path '%s' is invalid. %s")
					, *PropertyPathToString(BlueprintView, Binding.ViewModelPath)
					, *CreatedBindingSourceContext.GetError()));
				bIsBindingsValid = false;
				continue;
			}

			FBindingSourceContext BindingSourceContext = CreatedBindingSourceContext.GetValue();
			if (!IsPropertyPathValid(BindingSourceContext.PropertyPath))
			{
				AddErrorForBinding(Binding, BlueprintView, FString::Printf(TEXT("The property path '%s' is invalid."), *PropertyPathToString(BlueprintView, Binding.WidgetPath)));
				bIsBindingsValid = false;
				continue;
			}
			if (BindingSourceContext.SourceClass == nullptr)
			{
				AddErrorForBinding(Binding, BlueprintView, TEXT("Internal error. The binding could not find its source."));
				bIsBindingsValid = false;
				continue;
			}
			if (!BindingSourceContext.bIsRootWidget && BindingSourceContext.CompilerSourceContextIndex == INDEX_NONE)
			{
				AddErrorForBinding(Binding, BlueprintView, TEXT("Internal error. The binding could not find its source."));
				bIsBindingsValid = false;
				continue;
			}

			BindingSourceContext.BindingIndex = Index;
			BindingSourceContext.bIsForwardBinding = false;
			BindingSourceContexts.Add(BindingSourceContext);
		}
	}

	return bIsBindingsValid;
}


bool FMVVMViewBlueprintCompiler::PreCompileSourceCreators(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView)
{
	if (!bAreSourcesCreatorValid)
	{
		return false;
	}

	for (FCompilerSourceCreatorContext& SourceCreatorContext : SourceCreatorContexts)
	{
		FMVVMViewClass_SourceCreator CompiledSourceCreator;

		if (SourceCreatorContext.Type == ECompilerSourceCreatorType::ViewModel)
		{
			const FMVVMBlueprintViewModelContext& ViewModelContext = SourceCreatorContext.ViewModelContext;
			checkf(ViewModelContext.GetViewModelClass(), TEXT("The viewmodel class is invalid. It was checked in CreateSourceList"));

			if (ViewModelContext.GetViewModelClass()->HasAllClassFlags(CLASS_Deprecated))
			{
				WidgetBlueprintCompilerContext.MessageLog.Warning(*FString::Printf(TEXT("The viewmodel type '%s' is deprecated and should not be used for '%s'. Please update it in the View Binding panel under Manage ViewModels.")
					, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()
					, *ViewModelContext.GetDisplayName().ToString()));
			}

			if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual)
			{
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::CreateInstance)
			{
				if (ViewModelContext.GetViewModelClass()->HasAllClassFlags(CLASS_Abstract))
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel type '%s' is abstract and can't be created for '%s'. You can change it in the View Binding panel under Manage ViewModels.")
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()
						, *ViewModelContext.GetDisplayName().ToString()));
					bAreSourcesCreatorValid = false;
					continue;
				}
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath)
			{
				if (ViewModelContext.ViewModelPropertyPath.IsEmpty())
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' has an invalid Getter. You can select a new one in the View Binding panel under Manage ViewModels.")
						, *ViewModelContext.GetDisplayName().ToString()));
					bAreSourcesCreatorValid = true;
					continue;
				}

				// Generate a path to read the value at runtime
				TValueOrError<TArray<FMVVMConstFieldVariant>, FString> GeneratedField = FieldPathHelper::GenerateFieldPathList(Class, ViewModelContext.ViewModelPropertyPath, true);
				if (GeneratedField.HasError())
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' (%s) has an invalid Getter. %s")
						, *ViewModelContext.GetDisplayName().ToString()
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()
						, *GeneratedField.GetError()));
					bAreSourcesCreatorValid = false;
					continue;
				}

				TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> ReadFieldPathResult = BindingLibraryCompiler.AddObjectFieldPath(GeneratedField.GetValue(), ViewModelContext.GetViewModelClass(), true);
				if (ReadFieldPathResult.HasError())
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' (%s) has an invalid Getter. %s")
						, *ViewModelContext.GetDisplayName().ToString()
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()
						, *ReadFieldPathResult.GetError()));
					bAreSourcesCreatorValid = false;
					continue;
				}

				SourceCreatorContext.ReadPropertyPath = ReadFieldPathResult.StealValue();
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection)
			{
				if (ViewModelContext.GlobalViewModelIdentifier.IsNone())
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' (%s) doesn't have a valid Global identifier. You can specify a new one in the View Binding panel under Manage ViewModels.")
						, *ViewModelContext.GetDisplayName().ToString()
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()));
					bAreSourcesCreatorValid = false;
					continue;
				}
			}
			else
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' (%s) doesn't have a valid creation type. You can select one in the View Binding panel under Manage ViewModels.")
					, *ViewModelContext.GetDisplayName().ToString()
					, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()));
				bAreSourcesCreatorValid = true;
				continue;
			}
		}
	}

	return bAreSourcesCreatorValid;
}


bool FMVVMViewBlueprintCompiler::CompileSourceCreators(const FCompiledBindingLibraryCompiler::FCompileResult& CompileResult, UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension)
{
	if (!bAreSourcesCreatorValid)
	{
		return false;
	}

	for (const FCompilerSourceCreatorContext& SourceCreatorContext : SourceCreatorContexts)
	{
		if (SourceCreatorContext.Type == ECompilerSourceCreatorType::ViewModel)
		{
			const FMVVMBlueprintViewModelContext& ViewModelContext = SourceCreatorContext.ViewModelContext;

			FMVVMViewClass_SourceCreator CompiledSourceCreator;

			if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual)
			{
				CompiledSourceCreator = FMVVMViewClass_SourceCreator::MakeManual(ViewModelContext.GetViewModelName(), ViewModelContext.GetViewModelClass());
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::CreateInstance)
			{
				CompiledSourceCreator = FMVVMViewClass_SourceCreator::MakeInstance(ViewModelContext.GetViewModelName(), ViewModelContext.GetViewModelClass());
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath)
			{
				const FMVVMVCompiledFieldPath* CompiledFieldPath = CompileResult.FieldPaths.Find(SourceCreatorContext.ReadPropertyPath);
				if (CompiledFieldPath == nullptr)
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' initialization binding was not generated.")
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()));
					bAreSourcesCreatorValid = false;
					continue;
				}
				CompiledSourceCreator = FMVVMViewClass_SourceCreator::MakeFieldPath(ViewModelContext.GetViewModelName(), ViewModelContext.GetViewModelClass(), *CompiledFieldPath, ViewModelContext.bOptional);
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection)
			{
				if (ViewModelContext.GlobalViewModelIdentifier.IsNone())
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' doesn't have a valid Global identifier.")
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()));
					bAreSourcesCreatorValid = false;
					continue;
				}

				FMVVMViewModelContext GlobalViewModelInstance;
				GlobalViewModelInstance.ContextClass = ViewModelContext.GetViewModelClass();
				GlobalViewModelInstance.ContextName = ViewModelContext.GlobalViewModelIdentifier;
				if (!GlobalViewModelInstance.IsValid())
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The context for viewmodel '%s' could not be created.")
						, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()));
					bAreSourcesCreatorValid = false;
					continue;
				}

				CompiledSourceCreator = FMVVMViewClass_SourceCreator::MakeGlobalContext(ViewModelContext.GetViewModelName(), MoveTemp(GlobalViewModelInstance), ViewModelContext.bOptional);
			}
			else
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The viewmodel '%s' doesn't have a valid creation type.")
					, *ViewModelContext.GetViewModelClass()->GetDisplayNameText().ToString()));
				bAreSourcesCreatorValid = false;
				continue;
			}

			ViewExtension->SourceCreators.Add(MoveTemp(CompiledSourceCreator));
		}
	}

	return bAreSourcesCreatorValid;
}


bool FMVVMViewBlueprintCompiler::PreCompileBindings(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView)
{
	if (!bAreSourceContextsValid || !bIsBindingsValid)
	{
		return false;
	}

	for (const FBindingSourceContext& BindingSourceContext : BindingSourceContexts)
	{
		FMVVMBlueprintViewBinding* BindingPtr = BlueprintView->GetBindingAt(BindingSourceContext.BindingIndex);
		check(BindingPtr);
		FMVVMBlueprintViewBinding& Binding = *BindingPtr;

		FMVVMViewBlueprintCompiler* Self = this;
		auto AddBinding = [Self](UWidgetBlueprintGeneratedClass* Class, const TArrayView<TArray<UE::MVVM::FMVVMConstFieldVariant>> GetterFields, TArrayView<UE::MVVM::FMVVMConstFieldVariant> SetterFields, const UFunction* ConversionFunction) -> TValueOrError<FCompilerBinding, FString>
		{
			FCompilerBinding Result;

			for (const TArray<UE::MVVM::FMVVMConstFieldVariant>& GetterField : GetterFields)
			{
				// Generate a path to read the value at runtime
				TValueOrError<TArray<FMVVMConstFieldVariant>, FString> GeneratedField = FieldPathHelper::GenerateFieldPathList(GetterField, true);
				if (GeneratedField.HasError())
				{
					return MakeError(FString::Printf(TEXT("Couldn't create the source field path '%s'. %s")
						, *::UE::MVVM::FieldPathHelper::ToString(GetterField)
						, *GeneratedField.GetError()));
				}

				TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FieldPathResult = Self->BindingLibraryCompiler.AddFieldPath(GeneratedField.GetValue(), true);
				if (FieldPathResult.HasError())
				{
					return MakeError(FString::Printf(TEXT("Couldn't create the source field path '%s'. %s")
						, *::UE::MVVM::FieldPathHelper::ToString(GetterField)
						, *FieldPathResult.GetError()));
				}
				Result.SourceRead.Add(FieldPathResult.GetValue());
			}

			{
				TValueOrError<TArray<FMVVMConstFieldVariant>, FString> GeneratedField = FieldPathHelper::GenerateFieldPathList(SetterFields, false);
				if (GeneratedField.HasError())
				{
					return MakeError(FString::Printf(TEXT("Couldn't create the destination field path '%s'. %s")
						, *::UE::MVVM::FieldPathHelper::ToString(SetterFields)
						, *GeneratedField.GetError()));
				}

				TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FieldPathResult = Self->BindingLibraryCompiler.AddFieldPath(GeneratedField.GetValue(), false);
				if (FieldPathResult.HasError())
				{
					return MakeError(FString::Printf(TEXT("Couldn't create the destination field path '%s'. %s")
						, *::UE::MVVM::FieldPathHelper::ToString(SetterFields)
						, *FieldPathResult.GetError()));
				}
				Result.DestinationWrite = FieldPathResult.GetValue();
			}

			if (ConversionFunction != nullptr)
			{
				TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FString> FieldPathResult = Self->BindingLibraryCompiler.AddConversionFunctionFieldPath(Class, ConversionFunction);
				if (FieldPathResult.HasError())
				{
					return MakeError(FString::Printf(TEXT("Couldn't create the conversion function field path '%s'. %s")
						, *ConversionFunction->GetPathName()
						, *FieldPathResult.GetError()));
				}
				Result.ConversionFunction = FieldPathResult.GetValue();
			}

			// Generate the binding
			TValueOrError<FCompiledBindingLibraryCompiler::FBindingHandle, FString> BindingResult = Self->BindingLibraryCompiler.AddBinding(Result.SourceRead, Result.DestinationWrite, Result.ConversionFunction);
			if (BindingResult.HasError())
			{
				return MakeError(BindingResult.StealError());
			}
			Result.BindingHandle = BindingResult.StealValue();

			return MakeValue(Result);
		};

		auto AddFieldId = [Self](UClass* SourceContextClass, bool bNotifyFieldValueChangedRequired, EMVVMBindingMode BindingMode, FName FieldToListenTo) -> TValueOrError<FCompiledBindingLibraryCompiler::FFieldIdHandle, FString>
		{
			if (!IsOneTimeBinding(BindingMode) && bNotifyFieldValueChangedRequired)
			{
				return Self->BindingLibraryCompiler.AddFieldId(SourceContextClass, FieldToListenTo);
			}
			return MakeValue(FCompiledBindingLibraryCompiler::FFieldIdHandle());
		};

		TValueOrError<FCompiledBindingLibraryCompiler::FFieldIdHandle, FString> AddFieldResult = AddFieldId(BindingSourceContext.SourceClass, true, Binding.BindingType, BindingSourceContext.FieldId.GetFieldName());
		if (AddFieldResult.HasError())
		{
			AddErrorForBinding(Binding, BlueprintView, *FString::Printf(TEXT("The binding could not create its source. %s"), *AddFieldResult.GetError()));
			bIsBindingsValid = false;
			continue;
		}

		TArray<TArray<UE::MVVM::FMVVMConstFieldVariant>> GetterFields;
		//if (Binding.Conversion.HasConversionFunction && has read values defined (that are not the default one))
		//{
		//  Add the read values for the conversion function
		//}
		//else
		{
			GetterFields.Add(BindingSourceContext.PropertyPath);
		}

		TArray<UE::MVVM::FMVVMConstFieldVariant> SetterPath;
		{
			const FMVVMBlueprintPropertyPath& DestinationPath = BindingSourceContext.bIsForwardBinding ? Binding.WidgetPath : Binding.ViewModelPath;
			SetterPath = CreateBindingDestinationPath(BlueprintView, Class, DestinationPath);
			if (!IsPropertyPathValid(SetterPath))
			{
				AddErrorForBinding(Binding, BlueprintView, FString::Printf(TEXT("The property path '%s' is invalid."), *PropertyPathToString(BlueprintView, DestinationPath)));
				bIsBindingsValid = false;
				continue;
			}
		}

		FMemberReference ConversionFunctionRef = Binding.Conversion.SourceToDestinationFunction;
		if (!Binding.Conversion.SourceToDestinationWrapper.IsNone())
		{
			ConversionFunctionRef.SetSelfMember(Binding.Conversion.SourceToDestinationWrapper);
		}

		const UFunction* ConversionFunction = ConversionFunctionRef.ResolveMember<UFunction>(Class);

		TValueOrError<FCompilerBinding, FString> AddBindingResult = AddBinding(Class, GetterFields, SetterPath, ConversionFunction);
		if (AddBindingResult.HasError())
		{
			AddErrorForBinding(Binding, BlueprintView, FString::Printf(TEXT("The binding could not be created. %s"), *AddBindingResult.GetError()));
			bIsBindingsValid = false;
			continue;
		}

		FCompilerBinding NewBinding = AddBindingResult.StealValue();
		NewBinding.BindingIndex = BindingSourceContext.BindingIndex;
		NewBinding.SourceContextIndex = BindingSourceContext.CompilerSourceContextIndex;
		NewBinding.FieldIdHandle = AddFieldResult.StealValue();
		NewBinding.bSourceIsUserWidget = BindingSourceContext.bIsRootWidget;
		NewBinding.bFieldIdNeeded = !IsOneTimeBinding(Binding.BindingType);
		Bindings.Emplace(NewBinding);
	}

	return bIsBindingsValid;
}


bool FMVVMViewBlueprintCompiler::CompileBindings(const FCompiledBindingLibraryCompiler::FCompileResult& CompileResult, UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension)
{
	if (!bIsBindingsValid)
	{
		return false;
	}

	for (const FCompilerBinding& CompileBinding : Bindings)
	{
		const FMVVMBlueprintViewBinding& ViewBinding = *BlueprintView->GetBindingAt(CompileBinding.BindingIndex);

		FMVVMViewClass_CompiledBinding NewBinding;

		check(CompileBinding.SourceContextIndex != INDEX_NONE);
		NewBinding.SourcePropertyName = SourceContexts[CompileBinding.SourceContextIndex].PropertyName;

		const FMVVMVCompiledFieldId* CompiledFieldId = CompileResult.FieldIds.Find(CompileBinding.FieldIdHandle);
		if (CompiledFieldId == nullptr && CompileBinding.bFieldIdNeeded)
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The field id for binding '%d' was not generated.")
				, CompileBinding.BindingIndex));
			bIsBindingsValid = false;
			continue;
		}

		const FMVVMVCompiledBinding* CompiledBinding = CompileResult.Bindings.Find(CompileBinding.BindingHandle);
		if (CompiledBinding == nullptr)
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FString::Printf(TEXT("The binding '%d' was not generated.")
				, CompileBinding.BindingIndex));
			bIsBindingsValid = false;
			continue;
		}

		NewBinding.FieldId = CompiledFieldId  ? *CompiledFieldId : FMVVMVCompiledFieldId();
		NewBinding.Binding = *CompiledBinding;
		NewBinding.UpdateMode = ViewBinding.UpdateMode;

		NewBinding.Flags = 0;
		NewBinding.Flags |= (ViewBinding.bEnabled) ? FMVVMViewClass_CompiledBinding::EBindingFlags::EnabledByDefault : 0;
		NewBinding.Flags |= (IsForwardBinding(ViewBinding.BindingType)) ? FMVVMViewClass_CompiledBinding::EBindingFlags::ForwardBinding : 0;
		NewBinding.Flags |= (ViewBinding.BindingType == EMVVMBindingMode::TwoWay) ? FMVVMViewClass_CompiledBinding::EBindingFlags::TwoWayBinding : 0;
		NewBinding.Flags |= (IsOneTimeBinding(ViewBinding.BindingType)) ? FMVVMViewClass_CompiledBinding::EBindingFlags::OneTime : 0;

		ViewExtension->CompiledBindings.Emplace(MoveTemp(NewBinding));
	}

	return bIsBindingsValid;
}


const FMVVMViewBlueprintCompiler::FCompilerSourceCreatorContext* FMVVMViewBlueprintCompiler::FindViewModelSource(FGuid Id) const
{
	return SourceCreatorContexts.FindByPredicate([Id](const FCompilerSourceCreatorContext& Other)
		{
			return Other.Type == ECompilerSourceCreatorType::ViewModel ? Other.ViewModelContext.GetViewModelId() == Id : false;
		});
}


TValueOrError<FMVVMViewBlueprintCompiler::FBindingSourceContext, FString> FMVVMViewBlueprintCompiler::CreateBindingSourceContext(const UMVVMBlueprintView* BlueprintView, const UWidgetBlueprintGeneratedClass* Class, const FMVVMBlueprintPropertyPath& PropertyPath)
{
	if (PropertyPath.IsEmpty())
	{
		ensureAlways(false);
		return MakeError(TEXT("Empty property path found. This is legal."));
	}

	FBindingSourceContext Result;
	if (PropertyPath.IsFromViewModel())
	{
		Result.bIsRootWidget = false;

		const FMVVMBlueprintViewModelContext* SourceViewModelContext = BlueprintView->FindViewModel(PropertyPath.GetViewModelId());
		check(SourceViewModelContext);
		const FName SourceName = SourceViewModelContext->GetViewModelName();
		Result.CompilerSourceContextIndex = SourceContexts.IndexOfByPredicate([SourceName](const FCompilerSourceContext& Other) { return Other.PropertyName == SourceName; });
		check(Result.CompilerSourceContextIndex != INDEX_NONE);

		Result.SourceClass = SourceContexts[Result.CompilerSourceContextIndex].Class;
		Result.PropertyPath = CreatePropertyPath(Class, SourceContexts[Result.CompilerSourceContextIndex].PropertyName, PropertyPath.GetFields());
	}
	else if (PropertyPath.IsFromWidget())
	{
		const FName SourceName = PropertyPath.GetWidgetName();
		Result.bIsRootWidget = SourceName == Class->ClassGeneratedBy->GetFName();
		if (Result.bIsRootWidget)
		{
			Result.CompilerSourceContextIndex = INDEX_NONE;
			Result.SourceClass = Class->ClassGeneratedBy->GetClass();
			Result.PropertyPath = CreatePropertyPath(Class, FName(), PropertyPath.GetFields());
		}
		else
		{
			Result.CompilerSourceContextIndex = SourceContexts.IndexOfByPredicate([SourceName](const FCompilerSourceContext& Other) { return Other.PropertyName == SourceName; });
			check(Result.CompilerSourceContextIndex != INDEX_NONE);
			Result.SourceClass = SourceContexts[Result.CompilerSourceContextIndex].Class;
			Result.PropertyPath = CreatePropertyPath(Class, SourceContexts[Result.CompilerSourceContextIndex].PropertyName, PropertyPath.GetFields());
		}
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Not supported yet."));
	}

	// The path may contains another INotifyFieldValueChanged
	TValueOrError<FieldPathHelper::FParsedBindingInfo, FString> BindingInfoResult = FieldPathHelper::GetBindingInfoFromFieldPath(Class, Result.PropertyPath);
	if (BindingInfoResult.HasError())
	{
		return MakeError(BindingInfoResult.StealError());
	}

	const FieldPathHelper::FParsedBindingInfo& BindingInfo = BindingInfoResult.GetValue();
	Result.FieldId = BindingInfo.NotifyFieldId;

	const bool bCreateADynamicSourceEntry = BindingInfo.ViewModelIndex > 1
		&& BindingInfo.NotifyFieldClass != nullptr;

	// The INotifyFieldValueChanged/viewmodel is not the fist property. Creates the ViaBinding source entries.
	if (bCreateADynamicSourceEntry)
	{
		Result.SourceClass = BindingInfo.NotifyFieldClass;
		Result.CompilerSourceContextIndex = INDEX_NONE;
		Result.bIsRootWidget = false;

		FString InterfacePath = FieldPathHelper::ToString(BindingInfo.NotifyFieldInterfacePath);
		int32 SourceCreatorContextIndex = SourceCreatorContexts.IndexOfByPredicate([&InterfacePath](const FCompilerSourceCreatorContext& Other)
			{
				return (Other.Type == ECompilerSourceCreatorType::ViewModel)
					? (Other.ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath
						&& Other.ViewModelContext.ViewModelPropertyPath == InterfacePath)
					: false;
			});


		// Add if the path doesn't already exists
		if (!SourceCreatorContexts.IsValidIndex(SourceCreatorContextIndex))
		{
			return MakeError(TEXT("The field in the path is from a viewmodel that is not the root of the path. This is not yet supported."));
		}

		Result.PropertyPath.RemoveAt(0, BindingInfo.ViewModelIndex);
		if (SourceCreatorContexts[SourceCreatorContextIndex].Type == ECompilerSourceCreatorType::ViewModel)
		{
			Result.CompilerSourceContextIndex = SourceCreatorContextIndex;
		}
	}
	else
	{
		if (Result.SourceClass != BindingInfo.NotifyFieldClass)
		{
			return MakeError(TEXT("Internal Error. The class doesn't match"));
		}
	}

	return MakeValue(Result);
}


TArray<FMVVMConstFieldVariant> FMVVMViewBlueprintCompiler::CreateBindingDestinationPath(const UMVVMBlueprintView* BlueprintView, const UWidgetBlueprintGeneratedClass* Class, const FMVVMBlueprintPropertyPath& PropertyPath) const
{
	if (PropertyPath.IsEmpty())
	{
		ensureAlwaysMsgf(false, TEXT("Empty property path found. This is legal."));
		return TArray<FMVVMConstFieldVariant>();
	}

	if (PropertyPath.IsFromViewModel())
	{
		const FMVVMBlueprintViewModelContext* SourceViewModelContext = BlueprintView->FindViewModel(PropertyPath.GetViewModelId());
		check(SourceViewModelContext);
		FName DestinationName = SourceViewModelContext->GetViewModelName();
		const int32 DestinationVariableContextIndex = SourceContexts.IndexOfByPredicate([DestinationName](const FCompilerSourceContext& Other) { return Other.PropertyName == DestinationName; });
		check(DestinationVariableContextIndex != INDEX_NONE);

		return CreatePropertyPath(Class, SourceContexts[DestinationVariableContextIndex].PropertyName, PropertyPath.GetFields());
	}
	else if (PropertyPath.IsFromWidget())
	{
		FName DestinationName = PropertyPath.GetWidgetName();
		checkf(!DestinationName.IsNone(), TEXT("The destination should have been checked and set bAreSourceContextsValid."));
		const bool bSourceIsUserWidget = DestinationName == Class->ClassGeneratedBy->GetFName();
		if (bSourceIsUserWidget)
		{
			return CreatePropertyPath(Class, FName(), PropertyPath.GetFields());
		}
		else
		{
			const int32 DestinationVariableContextIndex = SourceContexts.IndexOfByPredicate([DestinationName](const FCompilerSourceContext& Other) { return Other.PropertyName == DestinationName; });
			check(DestinationVariableContextIndex != INDEX_NONE);

			return CreatePropertyPath(Class, SourceContexts[DestinationVariableContextIndex].PropertyName, PropertyPath.GetFields());
		}
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Not supported yet."));
		return CreatePropertyPath(Class, FName(), PropertyPath.GetFields());
	}
}


FString FMVVMViewBlueprintCompiler::PropertyPathToString(const UMVVMBlueprintView* BlueprintView, const FMVVMBlueprintPropertyPath& PropertyPath) const
{
	if (PropertyPath.IsEmpty())
	{
		return FString();
	}

	TStringBuilder<512> Result;
	if (PropertyPath.IsFromViewModel())
	{
		if (const FMVVMBlueprintViewModelContext* SourceViewModelContext = BlueprintView->FindViewModel(PropertyPath.GetViewModelId()))
		{
			Result << SourceViewModelContext->GetViewModelName();
		}
		else
		{
			Result << TEXT("<none>");
		}
	}
	else if (PropertyPath.IsFromWidget())
	{
		Result << PropertyPath.GetWidgetName();
	}
	else
	{
		Result << TEXT("<none>");
	}

	FString BasePropertyPath = PropertyPath.GetBasePropertyPath();
	if (BasePropertyPath.Len())
	{
		Result << TEXT('.');
		Result << MoveTemp(BasePropertyPath);
	}
	return Result.ToString();
}

TArray<FMVVMConstFieldVariant> FMVVMViewBlueprintCompiler::CreatePropertyPath(const UClass* Class, FName PropertyName, TArray<FMVVMConstFieldVariant> Properties) const
{
	if (PropertyName.IsNone())
	{
		return Properties;
	}

	check(Class);
	FMVVMConstFieldVariant NewProperty = BindingHelper::FindFieldByName(Class, FMVVMBindingName(PropertyName));
	Properties.Insert(NewProperty, 0);
	return Properties;
}


bool FMVVMViewBlueprintCompiler::IsPropertyPathValid(TArrayView<const FMVVMConstFieldVariant> PropertyPath) const
{
	for (const FMVVMConstFieldVariant& Field : PropertyPath)
	{
		if (Field.IsEmpty())
		{
			return false;
		}
		if (Field.IsProperty() && Field.GetProperty() == nullptr)
		{
			return false;
		}
		if (Field.IsFunction() && Field.GetFunction() == nullptr)
		{
			return false;
		}
	}
	return true;
}

} //namespace
