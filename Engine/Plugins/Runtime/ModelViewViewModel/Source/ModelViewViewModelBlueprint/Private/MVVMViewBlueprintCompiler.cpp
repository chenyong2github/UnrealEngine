// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMViewBlueprintCompiler.h"
#include "Blueprint/WidgetTree.h"
#include "Bindings/MVVMBindingHelper.h"
#include "Bindings/MVVMConversionFunctionHelper.h"
#include "Bindings/MVVMFieldPathHelper.h"
#include "Components/Widget.h"
#include "HAL/IConsoleManager.h"
#include "MVVMBlueprintView.h"
#include "MVVMDeveloperProjectSettings.h"
#include "MVVMMessageLog.h"
#include "PropertyPermissionList.h"
#include "MVVMFunctionGraphHelper.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMBindingName.h"
#include "UObject/LinkerLoad.h"
#include "View/MVVMViewClass.h"
#include "View/MVVMViewModelContextResolver.h"

#define LOCTEXT_NAMESPACE "MVVMViewBlueprintCompiler"

namespace UE::MVVM::Private
{
FAutoConsoleVariable CVarLogViewCompliedResult(
	TEXT("MVVM.LogViewCompliedResult"),
	false,
	TEXT("After the view is compiled log the compiled bindings and sources.")
);

FString PropertyPathToString(const UMVVMBlueprintView* BlueprintView, const FMVVMBlueprintPropertyPath& PropertyPath)
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

FText PropertyPathToText(const UMVVMBlueprintView* BlueprintView, const FMVVMBlueprintPropertyPath& PropertyPath)
{
	return FText::FromString(PropertyPathToString(BlueprintView, PropertyPath));
}

FText GetViewModelIdText(const FMVVMBlueprintPropertyPath& PropertyPath)
{
	return FText::FromString(PropertyPath.GetViewModelId().ToString(EGuidFormats::DigitsWithHyphensInBraces));
}

TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> AddObjectFieldPath(FCompiledBindingLibraryCompiler& BindingLibraryCompiler, const UWidgetBlueprintGeneratedClass* Class, FStringView ObjectPath, UClass* ExpectedType)
{
	// Generate a path to read the value at runtime
	static const FText InvalidGetterFormat = LOCTEXT("ViewModelInvalidGetterWithReason", "Viewmodel has an invalid Getter. {0}");

	TValueOrError<TArray<FMVVMConstFieldVariant>, FText> GeneratedField = FieldPathHelper::GenerateFieldPathList(Class, ObjectPath, true);
	if (GeneratedField.HasError())
	{
		return MakeError(FText::Format(InvalidGetterFormat, GeneratedField.GetError()));
	}

	TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> ReadFieldPathResult = BindingLibraryCompiler.AddObjectFieldPath(GeneratedField.GetValue(), ExpectedType, true);
	if (ReadFieldPathResult.HasError())
	{
		return MakeError(FText::Format(InvalidGetterFormat, ReadFieldPathResult.GetError()));
	}

	return MakeValue(ReadFieldPathResult.StealValue());
}

void FMVVMViewBlueprintCompiler::AddMessageForBinding(FMVVMBlueprintViewBinding& Binding, UMVVMBlueprintView* BlueprintView, const FText& MessageText, EBindingMessageType MessageType, FName ArgumentName) const
{
	const FText BindingName = FText::FromString(Binding.GetDisplayNameString(WidgetBlueprintCompilerContext.WidgetBlueprint()));

	FText FormattedError;
	if (!ArgumentName.IsNone())
	{
		FormattedError = FText::Format(LOCTEXT("BindingFormatWithArgument", "Binding '{0}': Argument '{1}' - {2}"), BindingName, FText::FromName(ArgumentName), MessageText);
	}
	else
	{
		FormattedError = FText::Format(LOCTEXT("BindingFormat", "Binding '{0}': {1}"), BindingName, MessageText);
	}

	switch (MessageType)
	{
	case EBindingMessageType::Info:
		WidgetBlueprintCompilerContext.MessageLog.Note(*FormattedError.ToString());
		break;
	case EBindingMessageType::Warning:
		WidgetBlueprintCompilerContext.MessageLog.Warning(*FormattedError.ToString());
		break;
	case EBindingMessageType::Error:
		WidgetBlueprintCompilerContext.MessageLog.Error(*FormattedError.ToString());
		break;
	default:
		break;
	}
	FBindingMessage NewMessage = { FormattedError, MessageType };
	BlueprintView->AddMessageToBinding(Binding.BindingId, NewMessage);
}

void FMVVMViewBlueprintCompiler::AddErrorForViewModel(const FMVVMBlueprintViewModelContext& ViewModel, const FText& Message) const
{
	const FText FormattedError = FText::Format(LOCTEXT("ViewModelFormat", "Viewodel '{0}': {1}"), ViewModel.GetDisplayName(), Message);
	WidgetBlueprintCompilerContext.MessageLog.Error(*FormattedError.ToString());
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
			const ERenameFlags RenFlags = REN_DoNotDirty | REN_ForceNoResetLoaders | REN_DontCreateRedirectors;

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
}


void FMVVMViewBlueprintCompiler::CreateFunctions(UMVVMBlueprintView* BlueprintView)
{
	if (!bAreSourcesCreatorValid || !bIsBindingsValid)
	{
		return;
	}

	if (!GetDefault<UMVVMDeveloperProjectSettings>()->bAllowGeneratedViewModelSetter)
	{
		return;
	}

	for (const FCompilerSourceCreatorContext& SourceCreator : CompilerSourceCreatorContexts)
	{
		if (SourceCreator.SetterGraph)
		{
			if (!UE::MVVM::FunctionGraphHelper::GenerateViewModelSetter(WidgetBlueprintCompilerContext, SourceCreator.SetterGraph, SourceCreator.ViewModelContext.GetViewModelName()))
			{
				AddErrorForViewModel(SourceCreator.ViewModelContext, LOCTEXT("SetterFunctionCouldNotBeGenerated", "The setter function could not be generated."));
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

	auto CreateVariable = [&Context](const FCompilerUserWidgetPropertyContext& SourceContext) -> FProperty*
	{
		FEdGraphPinType NewPropertyPinType(UEdGraphSchema_K2::PC_Object, NAME_None, SourceContext.Class, EPinContainerType::None, false, FEdGraphTerminalType());
		FProperty* NewProperty = Context.CreateVariable(SourceContext.PropertyName, NewPropertyPinType);
		if (NewProperty != nullptr)
		{
			NewProperty->SetPropertyFlags(CPF_BlueprintVisible | CPF_RepSkip | CPF_Transient | CPF_DuplicateTransient);
			if (SourceContext.BlueprintSetter.IsEmpty())
			{
				NewProperty->SetPropertyFlags(CPF_BlueprintReadOnly);
			}
			NewProperty->SetPropertyFlags(SourceContext.bExposeOnSpawn ? CPF_ExposeOnSpawn : CPF_DisableEditOnInstance);

#if WITH_EDITOR
			if (!SourceContext.BlueprintSetter.IsEmpty())
			{
				NewProperty->SetMetaData(FBlueprintMetadata::MD_PropertySetFunction, *SourceContext.BlueprintSetter);
			}
			if (!SourceContext.DisplayName.IsEmpty())
			{
				NewProperty->SetMetaData(FBlueprintMetadata::MD_DisplayName, *SourceContext.DisplayName.ToString());
			}
			if (!SourceContext.CategoryName.IsEmpty())
			{
				NewProperty->SetMetaData(FBlueprintMetadata::MD_FunctionCategory, *SourceContext.CategoryName);
			}
			if (SourceContext.bExposeOnSpawn)
			{
				NewProperty->SetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
			}
			//if (!SourceContext.bPublicGetter)
			//{
			//	NewProperty->SetMetaData(FBlueprintMetadata::MD_Private, TEXT("true"));
			//}
#endif
		}
		return NewProperty;
	};

	for (FCompilerUserWidgetPropertyContext& SourceContext : CompilerUserWidgetPropertyContexts)
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
						WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("FieldIsNotProperty", "The field for source '{0}' exists but is not a property."), SourceContext.DisplayName).ToString());
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
				WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("FieldNotAccessibleAtRuntime", "The field for source '{0}' exists but is not accessible at runtime."), SourceContext.DisplayName).ToString());
				bAreSourcesCreatorValid = false;
				continue;
			}

			const FProperty* Property = SourceContext.Field.IsProperty() ? SourceContext.Field.GetProperty() : BindingHelper::GetReturnProperty(SourceContext.Field.GetFunction());
			const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property);
			const bool bIsCompatible = ObjectProperty && SourceContext.Class->IsChildOf(ObjectProperty->PropertyClass);
			if (!bIsCompatible)
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("PropertyExistsAndNotCompatible","There is already a property named '{0}' that is not compatible with the source of the same name."), SourceContext.DisplayName).ToString());
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
			WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("VariableCouldNotBeCreated", "The variable for '{0}' could not be created."), SourceContext.DisplayName).ToString());
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
	CompilerUserWidgetPropertyContexts.Reset();
	CompilerSourceCreatorContexts.Reset();

	TSet<FGuid> ViewModelGuids;
	TSet<FName> WidgetSources;
	for (const FMVVMBlueprintViewModelContext& ViewModelContext : BlueprintView->GetViewModels())
	{
		if (!ViewModelContext.GetViewModelId().IsValid())
		{
			AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelInvalidGuid", "GUID is invalid."));
			bAreSourcesCreatorValid = false;
			continue;
		}

		if (ViewModelGuids.Contains(ViewModelContext.GetViewModelId()))
		{
			AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelAlreadyAdded", "Identical viewmodel has already been added."));
			bAreSourcesCreatorValid = false;
			continue;
		}

		ViewModelGuids.Add(ViewModelContext.GetViewModelId());

		if (ViewModelContext.GetViewModelClass() == nullptr || !ViewModelContext.IsValid())
		{
			AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelInvalidClass", "Invalid class."));
			bAreSourcesCreatorValid = false;
			continue;
		}

		const bool bCreateSetterFunction = GetDefault<UMVVMDeveloperProjectSettings>()->bAllowGeneratedViewModelSetter
			&& (ViewModelContext.bCreateSetterFunction || ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual);

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
			FoundSourceCreatorContextIndex = CompilerSourceCreatorContexts.Emplace(MoveTemp(SourceContext));
		}
		else
		{
			FGuid ViewModelId = ViewModelContext.GetViewModelId();
			FoundSourceCreatorContextIndex = CompilerSourceCreatorContexts.IndexOfByPredicate([ViewModelId](const FCompilerSourceCreatorContext& Other)
				{
					return Other.ViewModelContext.GetViewModelId() == ViewModelId;
				});
		}
		checkf(FoundSourceCreatorContextIndex != INDEX_NONE, TEXT("The viewmodel was added after the skeleton was created?"));

		FCompilerUserWidgetPropertyContext SourceVariable;
		SourceVariable.Class = ViewModelContext.GetViewModelClass();
		SourceVariable.PropertyName = ViewModelContext.GetViewModelName();
		SourceVariable.DisplayName = ViewModelContext.GetDisplayName();
		SourceVariable.CategoryName = TEXT("Viewmodel");
		SourceVariable.bExposeOnSpawn = bCreateSetterFunction;
		//SourceVariable.bPublicGetter = ViewModelContext.bCreateGetterFunction;
		SourceVariable.BlueprintSetter = CompilerSourceCreatorContexts[FoundSourceCreatorContextIndex].SetterFunctionName;
		SourceVariable.ViewModelId = ViewModelContext.GetViewModelId();
		CompilerUserWidgetPropertyContexts.Emplace(MoveTemp(SourceVariable));
	}

	bAreSourceContextsValid = bAreSourcesCreatorValid;

	UWidgetBlueprintGeneratedClass* SkeletonClass = Context.GetSkeletonGeneratedClass();
	const FName DefaultWidgetCategory = Context.GetWidgetBlueprint()->GetFName();

	// Only find the source first property and destination first property.
	//The full path will be tested later. We want to build the list of property needed.
	for (int32 Index = 0; Index < BlueprintView->GetNumBindings(); ++Index)
	{
		FMVVMBlueprintViewBinding* BindingPtr = BlueprintView->GetBindingAt(Index);
		if (BindingPtr == nullptr)
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("BindingInvalidIndex", "Internal error: Tried to fetch binding for invalid index {0}."), Index).ToString());
			bAreSourceContextsValid = false;
			continue;
		}

		FMVVMBlueprintViewBinding& Binding = *BindingPtr;
		if (!Binding.bCompile)
		{
			continue;
		}

		FMVVMViewBlueprintCompiler* Self = this;
		auto GenerateCompilerSourceContext = [Self, BlueprintView, DefaultWidgetCategory, Class = SkeletonClass, &Binding, &ViewModelGuids, &WidgetSources](const FMVVMBlueprintPropertyPath& PropertyPath, bool bViewModelPath, FName ArgumentName = FName()) -> bool
		{
			if (PropertyPath.IsFromWidget())
			{
				if (bViewModelPath)
				{
					Self->AddMessageForBinding(Binding,
							BlueprintView,
							FText::Format(LOCTEXT("ExpectedViewModelPath", "Expected a viewmodel path, but received a path from widget: {0}"), 
								FText::FromName(PropertyPath.GetWidgetName())
							),
							EBindingMessageType::Error
						);
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
						Self->AddMessageForBinding(Binding,
							BlueprintView,
							FText::Format(LOCTEXT("InvalidWidgetFormat", "Could not find the targeted widget: {0}"), 
								FText::FromName(PropertyPath.GetWidgetName())
							),
							EBindingMessageType::Error,
							ArgumentName
						);
						return false;
					}

					UWidget* Widget = *WidgetPtr;
					FCompilerUserWidgetPropertyContext SourceVariable;
					SourceVariable.Class = Widget->GetClass();
					SourceVariable.PropertyName = PropertyPath.GetWidgetName();
					SourceVariable.DisplayName = FText::FromString(Widget->GetDisplayLabel());
					SourceVariable.CategoryName = TEXT("Widget");
					SourceVariable.ViewModelId = FGuid();
					SourceVariable.bPublicGetter = false;
					Self->CompilerUserWidgetPropertyContexts.Emplace(MoveTemp(SourceVariable));
				}
			}
			else if (PropertyPath.IsFromViewModel())
			{
				const FMVVMBlueprintViewModelContext* SourceViewModelContext = BlueprintView->FindViewModel(PropertyPath.GetViewModelId());
				if (SourceViewModelContext == nullptr)
				{
					Self->AddMessageForBinding(Binding,
						BlueprintView,
						FText::Format(LOCTEXT("BindingViewModelNotFound", "Could not find viewmodel with GUID {0}."), GetViewModelIdText(PropertyPath)),
						EBindingMessageType::Error,
						ArgumentName
					);
					return false;
				}

				if (!bViewModelPath)
				{
					Self->AddMessageForBinding(Binding,
							BlueprintView,
							FText::Format(LOCTEXT("ExpectedWidgetPath", "Expected a widget path, but received a path from viewmodel: {0}"), 
								SourceViewModelContext->GetDisplayName()
							),
							EBindingMessageType::Error,
							ArgumentName
						);
				}

				if (!ViewModelGuids.Contains(SourceViewModelContext->GetViewModelId()))
				{
					Self->AddMessageForBinding(Binding,
						BlueprintView,
						FText::Format(LOCTEXT("BindingViewModelInvalid", "Viewmodel {0} {1} was invalid."), 
							SourceViewModelContext->GetDisplayName(),
							GetViewModelIdText(PropertyPath)
						),
						EBindingMessageType::Error,
						ArgumentName
					);
					return false;
				}
			}
			else
			{
				Self->AddMessageForBinding(Binding,
					BlueprintView,
					bViewModelPath ? LOCTEXT("ViewModelPathNotSet", "A viewmodel path is required, but not set.") :
							LOCTEXT("WidgetPathNotSet", "A widget path is required, but not set."),
					EBindingMessageType::Error,
					ArgumentName
				);
				return false;
			}
			return true;
		};

		const bool bIsForwardBinding = IsForwardBinding(Binding.BindingType);
		const bool bIsBackwardBinding = IsBackwardBinding(Binding.BindingType);

		if (bIsForwardBinding || bIsBackwardBinding)
		{
			TMap<FName, FMVVMBlueprintPropertyPath> ConversionFunctionArguments = ConversionFunctionHelper::GetAllArgumentPropertyPaths(WidgetBlueprintCompilerContext.WidgetBlueprint(), Binding, bIsForwardBinding, true);
			if (ConversionFunctionArguments.Num() > 0)
			{
				if (Binding.BindingType == EMVVMBindingMode::TwoWay)
				{
					Self->AddMessageForBinding(Binding, BlueprintView, LOCTEXT("TwoWayBindingsWithConversion", "Two-way bindings are not allowed to use conversion functions."), EBindingMessageType::Error);
					bAreSourceContextsValid = false;
					continue;
				}

				// generate sources for conversion function arguments
				for (const TPair<FName, FMVVMBlueprintPropertyPath>& Arg : ConversionFunctionArguments)
				{
					if (bIsForwardBinding)
					{
						bAreSourceContextsValid &= GenerateCompilerSourceContext(Arg.Value, true, Arg.Key);
					}
					else
					{
						bAreSourceContextsValid &= GenerateCompilerSourceContext(Arg.Value, false, Arg.Key);
					}
				}

				// generate destination source
				if (bIsForwardBinding)
				{
					bAreSourceContextsValid &= GenerateCompilerSourceContext(Binding.DestinationPath, false);
				}
				else
				{
					bAreSourceContextsValid &= GenerateCompilerSourceContext(Binding.SourcePath, true);
				}
			}
			else
			{
				// if we aren't using a conversion function, just validate the widget and viewmodel paths
				bAreSourceContextsValid &= GenerateCompilerSourceContext(Binding.DestinationPath, false);
				bAreSourceContextsValid &= GenerateCompilerSourceContext(Binding.SourcePath, true);
			}
		}
	}
}


void FMVVMViewBlueprintCompiler::CreateFunctionsDeclaration(const FWidgetBlueprintCompilerContext::FCreateVariableContext& Context, UMVVMBlueprintView* BlueprintView)
{
	// Clean all previous intermediate function graph. It should stay alive. The graph lives on the Blueprint not on the class and it's used to generate the UFunction.
	{
		auto RenameObjectToTransientPackage = [](UObject* ObjectToRename)
		{
			const ERenameFlags RenFlags = REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty;
			ObjectToRename->Rename(nullptr, GetTransientPackage(), RenFlags);
			ObjectToRename->SetFlags(RF_Transient);
			ObjectToRename->ClearFlags(RF_Public | RF_Standalone | RF_ArchetypeObject);
			FLinkerLoad::InvalidateExport(ObjectToRename);
		};

		for (UEdGraph* OldGraph : BlueprintView->TemporaryGraph)
		{
			if (OldGraph)
			{
				RenameObjectToTransientPackage(OldGraph);
			}
		}
		BlueprintView->TemporaryGraph.Reset();
	}

	if (!GetDefault<UMVVMDeveloperProjectSettings>()->bAllowGeneratedViewModelSetter)
	{
		return;
	}

	for (FCompilerSourceCreatorContext& SourceCreator : CompilerSourceCreatorContexts)
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
			BlueprintView->TemporaryGraph.Add(SourceCreator.SetterGraph);

			if (SourceCreator.SetterGraph == nullptr || SourceCreator.SetterGraph->GetFName() != FName(*SourceCreator.SetterFunctionName))
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("SetterNameAlreadyExists", "The setter name {0} already exists and could not be autogenerated."), 
					FText::FromString(SourceCreator.SetterFunctionName)
					).ToString()
				);
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
	CompilerBindings.Reset(NumBindings*2);
	BindingSourceContexts.Reset(NumBindings*2);

	FPropertyEditorPermissionList::Get().AddPermissionList(Class, FNamePermissionList(), EPropertyPermissionListRules::AllowListAllProperties);

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

	TValueOrError<FCompiledBindingLibraryCompiler::FCompileResult, FText> CompileResult = BindingLibraryCompiler.Compile();
	if (CompileResult.HasError())
	{
		WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("BindingCompilationFailed", "The binding compilation failed. {1}"), CompileResult.GetError()).ToString());
		return false;
	}
	CompileSourceCreators(CompileResult.GetValue(), Class, BlueprintView, ViewExtension);
	CompileBindings(CompileResult.GetValue(), Class, BlueprintView, ViewExtension);

	bool bResult = bAreSourcesCreatorValid && bAreSourceContextsValid && bIsBindingsValid;
	if (bResult)
	{
		ViewExtension->BindingLibrary = MoveTemp(CompileResult.GetValue().Library);

#if UE_WITH_MVVM_DEBUGGING
		if (CVarLogViewCompliedResult->GetBool())
		{
			FMVVMViewClass_SourceCreator::FToStringArgs CreatorsToStringArgs = FMVVMViewClass_SourceCreator::FToStringArgs::All();
			CreatorsToStringArgs.bUseDisplayName = false;
			FMVVMViewClass_CompiledBinding::FToStringArgs BindingToStringArgs = FMVVMViewClass_CompiledBinding::FToStringArgs::All();
			BindingToStringArgs.bUseDisplayName = false;
			ViewExtension->Log(CreatorsToStringArgs, BindingToStringArgs);
		}
#endif
	}

	return bResult;
}


bool FMVVMViewBlueprintCompiler::PreCompileBindingSources(UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView)
{
	const int32 NumBindings = BlueprintView->GetNumBindings(); // NB Binding can be added when creating a dynamic vm
	for (int32 Index = 0; Index < NumBindings; ++Index)
	{
		FMVVMBlueprintViewBinding* BindingPtr = BlueprintView->GetBindingAt(Index);
		if (BindingPtr == nullptr)
		{
			WidgetBlueprintCompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("InvalidBindingIndex", "Internal error. Invalid binding index given."), Index).ToString());
			bIsBindingsValid = false;
			continue;
		}

		FMVVMBlueprintViewBinding& Binding = *BindingPtr;
		if (!Binding.bCompile)
		{
			continue;
		}

		bool bIsOneTimeBinding = IsOneTimeBinding(Binding.BindingType);

		auto CreateSourceContextForPropertyPath = [this, &Binding, BlueprintView, Class, Index, bIsOneTimeBinding](const FMVVMBlueprintPropertyPath& Path, bool bForwardBinding, FName ArgumentName) -> bool
		{
			const TValueOrError<FBindingSourceContext, FText> CreatedBindingSourceContext = CreateBindingSourceContext(BlueprintView, Class, Path, bIsOneTimeBinding);
			if (CreatedBindingSourceContext.HasError())
			{
				AddMessageForBinding(Binding, BlueprintView,
					FText::Format(LOCTEXT("PropertyPathInvalidWithReason", "The property path '{0}' is invalid. {1}"),
						PropertyPathToText(BlueprintView, Binding.SourcePath),
						CreatedBindingSourceContext.GetError()
					),
					EBindingMessageType::Error,
					ArgumentName
				);
				return false;
			}

			FBindingSourceContext BindingSourceContext = CreatedBindingSourceContext.GetValue();
			if (!IsPropertyPathValid(BindingSourceContext.PropertyPath))
			{
				AddMessageForBinding(Binding,
					BlueprintView,
					FText::Format(LOCTEXT("PropertyPathIsInvalid", "The property path '{0}' is invalid."), 
						PropertyPathToText(BlueprintView, Binding.SourcePath)
					),
					EBindingMessageType::Error,
					ArgumentName
				);
				return false;
			}

			if (BindingSourceContext.SourceClass == nullptr)
			{
				AddMessageForBinding(Binding, BlueprintView, LOCTEXT("BindingInvalidSourceClass", "Internal error. The binding could not find its source class."), EBindingMessageType::Error, ArgumentName);
				return false;
			}

			if (!BindingSourceContext.bIsRootWidget && BindingSourceContext.UserWidgetPropertyContextIndex == INDEX_NONE && BindingSourceContext.SourceCreatorContextIndex == INDEX_NONE)
			{
				AddMessageForBinding(Binding, BlueprintView, LOCTEXT("BindingInvalidSource", "Internal error. The binding could not find its source."), EBindingMessageType::Error, ArgumentName);
				return false;
			}

			BindingSourceContext.BindingIndex = Index;
			BindingSourceContext.bIsForwardBinding = bForwardBinding;
			BindingSourceContext.bIsComplexBinding = !ArgumentName.IsNone();

			this->BindingSourceContexts.Add(MoveTemp(BindingSourceContext));
			return true;
		};

		enum class ECreateSourcesForConversionFunctionResult : uint8 { Valid, Failed, Continue };
		auto CreateSourcesForConversionFunction = [this, &Binding, BlueprintView, &CreateSourceContextForPropertyPath](bool bForwardBinding)
		{
			ECreateSourcesForConversionFunctionResult Result = ECreateSourcesForConversionFunctionResult::Continue;
			TMap<FName, FMVVMBlueprintPropertyPath> ArgumentPaths = ConversionFunctionHelper::GetAllArgumentPropertyPaths(WidgetBlueprintCompilerContext.WidgetBlueprint(), Binding, bForwardBinding, true);
			for (const TPair<FName, FMVVMBlueprintPropertyPath>& Pair : ArgumentPaths)
			{
				if (Pair.Key.IsNone())
				{
					AddMessageForBinding(Binding, BlueprintView,
						FText::Format(LOCTEXT("InvalidArgumentPathName", "The conversion function {0} has an invalid argument"), FText::FromString(Binding.GetDisplayNameString(WidgetBlueprintCompilerContext.WidgetBlueprint()))),
						EBindingMessageType::Error
					);
					Result = ECreateSourcesForConversionFunctionResult::Failed;
				}
				else if (CreateSourceContextForPropertyPath(Pair.Value, bForwardBinding, Pair.Key))
				{
					Result = ECreateSourcesForConversionFunctionResult::Valid;
				}
				else
				{
					Result = ECreateSourcesForConversionFunctionResult::Failed;
					break;
				}
			}
			return Result;
		};

		auto AddWarningForPropertyWithMVVMAndLegacyBinding = [this, &Binding, &BlueprintView, Class](const FMVVMBlueprintPropertyPath& Path)
		{
			if (!Path.HasPaths())
			{
				return;
			}

			// There can't be a legacy binding in the local scope, so we can skip this if the MVVM binding refers to a property in local scope.
			if (Path.HasFieldInLocalScope())
			{
				return;
			}

			TArrayView<FMVVMBlueprintFieldPath const> MVVMBindingPath = Path.GetFieldPaths();
			TArray< FDelegateRuntimeBinding > LegacyBindings = Class->Bindings;
			FName MVVMFieldName = Path.GetPaths().Last();
			FName MVVMObjectName = Path.GetWidgetName();

			if (Path.GetFieldPaths().Last().GetBindingKind() == EBindingKind::Function)
			{
				return;
			}

			// If the first field is a UserWidget, we know this property resides in a nested UserWidget.
			if (MVVMBindingPath[0].GetParentClass() && MVVMBindingPath[0].GetParentClass()->IsChildOf(UUserWidget::StaticClass()) && MVVMBindingPath.Num() > 1)
			{
				if (UWidgetBlueprintGeneratedClass* NestedBPGClass = Cast<UWidgetBlueprintGeneratedClass>(MVVMBindingPath[MVVMBindingPath.Num() - 2].GetParentClass()))
				{
					LegacyBindings = NestedBPGClass->Bindings;

					// We can't use Path.GetWidgetName() when we are dealing with nested UserWidgets, because it refers to the topmost UserWidget.
					MVVMObjectName = MVVMBindingPath[MVVMBindingPath.Num() - 2].GetFieldName();
				}
				else
				{
					return;
				}
			}

			for (const FDelegateRuntimeBinding& LegacyBinding : LegacyBindings)
			{
				if (LegacyBinding.ObjectName == MVVMObjectName) 
				{
					if (LegacyBinding.PropertyName == MVVMFieldName)
					{
						AddMessageForBinding(Binding, BlueprintView, LOCTEXT("BindingConflictWithLegacy", "The binding is set on a property with legacy binding."), EBindingMessageType::Warning);
						break;
					}
				}
			}
		};

		// Add the forward binding. If the binding has a conversion function, use it instead of the regular binding.
		if (IsForwardBinding(Binding.BindingType))
		{
			ECreateSourcesForConversionFunctionResult ConversionFunctionResult = CreateSourcesForConversionFunction(true);
			if (ConversionFunctionResult == ECreateSourcesForConversionFunctionResult::Continue)
			{
				if (!Binding.SourcePath.IsEmpty())
				{
					if (!Binding.DestinationPath.IsEmpty())
					{
						AddWarningForPropertyWithMVVMAndLegacyBinding(Binding.DestinationPath);
					}
					if (!CreateSourceContextForPropertyPath(Binding.SourcePath, true, FName()))
					{
						bIsBindingsValid = false;
						continue;
					}
				}
				else
				{
					AddMessageForBinding(Binding, BlueprintView, LOCTEXT("BindingEmptySourcePath", "The binding doesn't have a Source or a Conversion function."), EBindingMessageType::Error);
					bIsBindingsValid = false;
					continue;
				}
			}
			else if (ConversionFunctionResult == ECreateSourcesForConversionFunctionResult::Failed)
			{
				bIsBindingsValid = false;
				continue;
			}
		}

		// Add the backward binding. If the binding has a conversion function, use it instead of the regular binding.
		if (IsBackwardBinding(Binding.BindingType))
		{
			ECreateSourcesForConversionFunctionResult ConversionFunctionResult = CreateSourcesForConversionFunction(false);
			if (ConversionFunctionResult == ECreateSourcesForConversionFunctionResult::Continue)
			{
				if (!Binding.DestinationPath.IsEmpty())
				{
					if (!Binding.SourcePath.IsEmpty())
					{
						AddWarningForPropertyWithMVVMAndLegacyBinding(Binding.SourcePath);
					}
					if (!CreateSourceContextForPropertyPath(Binding.DestinationPath, false, FName()))
					{
						bIsBindingsValid = false;
						continue;
					}
				}
				else
				{
					AddMessageForBinding(Binding, BlueprintView, LOCTEXT("BindingEmptyDestinationPath", "The binding doesn't have a Destination or a Conversion function."), EBindingMessageType::Error);
					bIsBindingsValid = false;
					continue;
				}
			}
			else if (ConversionFunctionResult == ECreateSourcesForConversionFunctionResult::Failed)
			{
				bIsBindingsValid = false;
				continue;
			}
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

	for (FCompilerSourceCreatorContext& SourceCreatorContext : CompilerSourceCreatorContexts)
	{
		FMVVMViewClass_SourceCreator CompiledSourceCreator;

		if (SourceCreatorContext.Type == ECompilerSourceCreatorType::ViewModel)
		{
			const FMVVMBlueprintViewModelContext& ViewModelContext = SourceCreatorContext.ViewModelContext;
			checkf(ViewModelContext.GetViewModelClass(), TEXT("The viewmodel class is invalid. It was checked in CreateSourceList"));

			if (ViewModelContext.GetViewModelClass()->HasAllClassFlags(CLASS_Deprecated))
			{
				AddErrorForViewModel(ViewModelContext, FText::Format(LOCTEXT("ViewModelTypeDeprecated", "Viewmodel class '{0}' is deprecated and should not be used. Please update it in the View Models panel."),
					ViewModelContext.GetViewModelClass()->GetDisplayNameText()
				));
			}

			if (!GetAllowedContextCreationType(ViewModelContext.GetViewModelClass()).Contains(ViewModelContext.CreationType))
			{
				AddErrorForViewModel(ViewModelContext, FText::Format(LOCTEXT("ViewModelContextCreationTypeInvalid", "Viewmodel '{0}' has an invalidate creation type. You can change it in the View Models panel."),
					ViewModelContext.GetViewModelClass()->GetDisplayNameText()
				));
				bAreSourcesCreatorValid = false;
				continue;
			}

			if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual)
			{
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::CreateInstance)
			{
				if (ViewModelContext.GetViewModelClass()->HasAllClassFlags(CLASS_Abstract))
				{
					AddErrorForViewModel(ViewModelContext, FText::Format(LOCTEXT("ViewModelTypeAbstract", "Viewmodel class '{0}' is abstract and can't be created. You can change it in the View Models panel."),
						ViewModelContext.GetViewModelClass()->GetDisplayNameText()
					));
					bAreSourcesCreatorValid = false;
					continue;
				}
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath)
			{
				if (ViewModelContext.ViewModelPropertyPath.IsEmpty())
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewModelInvalidGetter", "Viewmodel has an invalid Getter. You can select a new one in the View Models panel."));
					bAreSourcesCreatorValid = true;
					continue;
				}

				TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> ReadFieldPathResult = AddObjectFieldPath(BindingLibraryCompiler, Class, ViewModelContext.ViewModelPropertyPath, ViewModelContext.GetViewModelClass());
				if (ReadFieldPathResult.HasError())
				{
					AddErrorForViewModel(ViewModelContext, ReadFieldPathResult.GetError());
					bAreSourcesCreatorValid = false;
					continue;
				}

				SourceCreatorContext.ReadPropertyPath = ReadFieldPathResult.StealValue();
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection)
			{
				if (ViewModelContext.GlobalViewModelIdentifier.IsNone())
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelInvalidGlobalIdentifier", "Viewmodel doesn't have a valid Global identifier. You can specify a new one in the Viewmodels panel."));
					bAreSourcesCreatorValid = false;
					continue;
				}
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Resolver)
			{
				if (!ViewModelContext.Resolver)
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelInvalidResolver", "Viewmodel doesn't have a valid Resolver. You can specify a new one in the Viewmodels panel."));
					bAreSourcesCreatorValid = false;
					continue;
				}
			}
			else
			{
				AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelInvalidCreationType", "Viewmodel doesn't have a valid creation type. You can select one in the Viewmodels panel."));
				bAreSourcesCreatorValid = false;
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

	for (const FCompilerSourceCreatorContext& SourceCreatorContext : CompilerSourceCreatorContexts)
	{
		const FMVVMBlueprintViewModelContext& ViewModelContext = SourceCreatorContext.ViewModelContext;
		FMVVMViewClass_SourceCreator CompiledSourceCreator;

		ensure(ViewModelContext.GetViewModelClass() && ViewModelContext.GetViewModelClass()->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()));
		CompiledSourceCreator.ExpectedSourceType = ViewModelContext.GetViewModelClass();
		CompiledSourceCreator.PropertyName = ViewModelContext.GetViewModelName();

		bool bCanBeSet = false;
		bool CanBeEvaluated = false;
		bool bIsOptional = false;
		bool bCreateInstance = false;
		bool IsUserWidgetProperty = false;

		if (SourceCreatorContext.Type == ECompilerSourceCreatorType::ViewModel)
		{
			IsUserWidgetProperty = true;
			bCanBeSet = ViewModelContext.bCreateSetterFunction;

			if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Manual)
			{
				bCanBeSet = true;
				bIsOptional = true;
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::CreateInstance)
			{
				bCreateInstance = true;
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath)
			{
				const FMVVMVCompiledFieldPath* CompiledFieldPath = CompileResult.FieldPaths.Find(SourceCreatorContext.ReadPropertyPath);
				if (CompiledFieldPath == nullptr)
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewModelInvalidInitializationBindingNotGenerated", "The viewmodel initialization binding was not generated."));
					bAreSourcesCreatorValid = false;
					continue;
				}

				CompiledSourceCreator.FieldPath = *CompiledFieldPath;
				bIsOptional = ViewModelContext.bOptional;
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection)
			{
				if (ViewModelContext.GlobalViewModelIdentifier.IsNone())
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewModelInvalidGlobalIdentifier", "The viewmodel doesn't have a valid Global identifier. You can specify a new one in the Viewmodels panel."));
					bAreSourcesCreatorValid = false;
					continue;
				}

				FMVVMViewModelContext GlobalViewModelInstance;
				GlobalViewModelInstance.ContextClass = ViewModelContext.GetViewModelClass();
				GlobalViewModelInstance.ContextName = ViewModelContext.GlobalViewModelIdentifier;
				if (!GlobalViewModelInstance.IsValid())
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewModelCouldNotBeCreated", "The context for viewmodel could not be created. You can change the viewmodel in the Viewmodels panel."));
					bAreSourcesCreatorValid = false;
					continue;
				}

				CompiledSourceCreator.GlobalViewModelInstance = MoveTemp(GlobalViewModelInstance);
				bIsOptional = ViewModelContext.bOptional;
			}
			else if (ViewModelContext.CreationType == EMVVMBlueprintViewModelContextCreationType::Resolver)
			{
				UMVVMViewModelContextResolver* Resolver = DuplicateObject(ViewModelContext.Resolver.Get(), ViewExtension);
				if (!Resolver)
				{
					AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewmodelFailedResolverDuplicate", "Internal error. The resolver could not be dupliated."));
					bAreSourcesCreatorValid = false;
					continue;
				}

				CompiledSourceCreator.Resolver = Resolver;
				bIsOptional = ViewModelContext.bOptional;
			}
			else
			{
				AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewModelWithoutValidCreationType", "The viewmodel doesn't have a valid creation type."));
				bAreSourcesCreatorValid = false;
				continue;
			}
		}
		else if (SourceCreatorContext.Type == ECompilerSourceCreatorType::ViewModelDynamic)
		{
			const FMVVMVCompiledFieldPath* CompiledFieldPath = CompileResult.FieldPaths.Find(SourceCreatorContext.ReadPropertyPath);
			if (CompiledFieldPath == nullptr)
			{
				AddErrorForViewModel(ViewModelContext, LOCTEXT("ViewModelInvalidInitializationBindingNotGenerated", "The viewmodel initialization binding was not generated."));
				bAreSourcesCreatorValid = false;
				continue;
			}

			CanBeEvaluated = true;
			CompiledSourceCreator.FieldPath = *CompiledFieldPath;
		}

		CompiledSourceCreator.Flags = 0;
		CompiledSourceCreator.Flags |= bCreateInstance ? (uint8)FMVVMViewClass_SourceCreator::ESourceFlags::TypeCreateInstance : 0;
		CompiledSourceCreator.Flags |= IsUserWidgetProperty ? (uint8)FMVVMViewClass_SourceCreator::ESourceFlags::IsUserWidgetProperty : 0;
		CompiledSourceCreator.Flags |= bIsOptional ? (uint8)FMVVMViewClass_SourceCreator::ESourceFlags::IsOptional : 0;
		CompiledSourceCreator.Flags |= bCanBeSet ? (uint8)FMVVMViewClass_SourceCreator::ESourceFlags::CanBeSet : 0;
		CompiledSourceCreator.Flags |= CanBeEvaluated ? (uint8)FMVVMViewClass_SourceCreator::ESourceFlags::CanBeEvaluated : 0;

		ViewExtension->SourceCreators.Add(MoveTemp(CompiledSourceCreator));
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
		auto AddFieldId = [Self](const UClass* SourceContextClass, bool bNotifyFieldValueChangedRequired, EMVVMBindingMode BindingMode, FName FieldToListenTo) -> TValueOrError<FCompiledBindingLibraryCompiler::FFieldIdHandle, FText>
		{
			if (!IsOneTimeBinding(BindingMode) && bNotifyFieldValueChangedRequired)
			{
				return Self->BindingLibraryCompiler.AddFieldId(SourceContextClass, FieldToListenTo);
			}
			return MakeValue(FCompiledBindingLibraryCompiler::FFieldIdHandle());
		};

		if (Binding.bOverrideExecutionMode)
		{
			if (!GetDefault<UMVVMDeveloperProjectSettings>()->IsExecutionModeAllowed(Binding.OverrideExecutionMode))
			{
				AddMessageForBinding(Binding, BlueprintView, LOCTEXT("NotAllowedExecutionMode", "The binding has a restricted execution mode."), EBindingMessageType::Error);
			}
		}

		TValueOrError<FCompiledBindingLibraryCompiler::FFieldIdHandle, FText> AddFieldResult = AddFieldId(BindingSourceContext.SourceClass, true, Binding.BindingType, BindingSourceContext.FieldId.GetFieldName());
		if (AddFieldResult.HasError())
		{
			AddMessageForBinding(Binding, BlueprintView, FText::Format(LOCTEXT("CouldNotCreateSource", "Could not create source. {0}"),
				AddFieldResult.GetError()), EBindingMessageType::Error);
			bIsBindingsValid = false;
			continue;
		}

		TArray<UE::MVVM::FMVVMConstFieldVariant> SetterPath;
		{
			const FMVVMBlueprintPropertyPath& DestinationPath = BindingSourceContext.bIsForwardBinding ? Binding.DestinationPath : Binding.SourcePath;
			SetterPath = CreateBindingDestinationPath(BlueprintView, Class, DestinationPath);
			if (!IsPropertyPathValid(SetterPath))
			{
				AddMessageForBinding(Binding, BlueprintView, FText::Format(LOCTEXT("PropertyPathIsInvalid", "The property path '{0}' is invalid."),
					PropertyPathToText(BlueprintView, DestinationPath)),
					EBindingMessageType::Error
				);
				bIsBindingsValid = false;
				continue;
			}
		}

		FMemberReference ConversionFunctionReference = BindingSourceContext.bIsForwardBinding ? Binding.Conversion.SourceToDestinationFunction : Binding.Conversion.DestinationToSourceFunction;
		FName ConversionFunctionWrapper = BindingSourceContext.bIsForwardBinding ? Binding.Conversion.SourceToDestinationWrapper : Binding.Conversion.DestinationToSourceWrapper;
		if (!ConversionFunctionWrapper.IsNone())
		{
			ConversionFunctionReference.SetSelfMember(ConversionFunctionWrapper);
		}

		const UFunction* ConversionFunction = ConversionFunctionReference.ResolveMember<UFunction>(Class);
		if (!ConversionFunctionWrapper.IsNone() && ConversionFunction == nullptr)
		{
			AddMessageForBinding(Binding, BlueprintView, FText::Format(LOCTEXT("ConversionFunctionNotFound", "The conversion function '{0}' could not be found."), 
				FText::FromName(ConversionFunctionReference.GetMemberName())),
				EBindingMessageType::Error
			);
			bIsBindingsValid = false;
			continue;
		}

		TValueOrError<FCompiledBinding, FText> AddBindingResult = CreateCompiledBinding(Class, BindingSourceContext.PropertyPath, SetterPath, ConversionFunction, BindingSourceContext.bIsComplexBinding);
		if (AddBindingResult.HasError())
		{
			AddMessageForBinding(Binding, BlueprintView,
				FText::Format(LOCTEXT("CouldNotCreateBinding", "Could not create binding. {0}"), AddBindingResult.GetError()),
				EBindingMessageType::Error
			);
			bIsBindingsValid = false;
			continue;
		}

		FCompilerBinding NewBinding;
		NewBinding.BindingIndex = BindingSourceContext.BindingIndex;
		NewBinding.UserWidgetPropertyContextIndex = BindingSourceContext.UserWidgetPropertyContextIndex;
		NewBinding.SourceCreatorContextIndex = BindingSourceContext.SourceCreatorContextIndex;
		NewBinding.bSourceIsUserWidget = BindingSourceContext.bIsRootWidget;
		NewBinding.bFieldIdNeeded = !IsOneTimeBinding(Binding.BindingType);
		NewBinding.bIsForwardBinding = BindingSourceContext.bIsForwardBinding;

		NewBinding.CompiledBinding = AddBindingResult.StealValue();
		NewBinding.FieldIdHandle = AddFieldResult.StealValue();

		CompilerBindings.Emplace(NewBinding);
	}

	return bIsBindingsValid;
}


bool FMVVMViewBlueprintCompiler::CompileBindings(const FCompiledBindingLibraryCompiler::FCompileResult& CompileResult, UWidgetBlueprintGeneratedClass* Class, UMVVMBlueprintView* BlueprintView, UMVVMViewClass* ViewExtension)
{
	if (!bIsBindingsValid)
	{
		return false;
	}

	IConsoleVariable* CVarDefaultExecutionMode = IConsoleManager::Get().FindConsoleVariable(TEXT("MVVM.DefaultExecutionMode"));
	if (!CVarDefaultExecutionMode)
	{
		WidgetBlueprintCompilerContext.MessageLog.Error(*LOCTEXT("CantFindDefaultExecutioMode", "The default execution mode cannot be found.").ToString());
		return false;
	}

	for (const FCompilerBinding& CompileBinding : CompilerBindings)
	{
		// PropertyBinding needs a valid CompileBinding.BindingIndex.
		check(CompileBinding.Type != ECompilerBindingType::PropertyBinding || (CompileBinding.BindingIndex >= 0 && CompileBinding.BindingIndex < BlueprintView->GetNumBindings()));
		FMVVMBlueprintViewBinding* ViewBinding = CompileBinding.Type == ECompilerBindingType::PropertyBinding
			? BlueprintView->GetBindingAt(CompileBinding.BindingIndex)
			: nullptr;


		FMVVMViewClass_CompiledBinding NewBinding;
		const bool bIsSourceSelf = CompileBinding.bSourceIsUserWidget;
		if (!bIsSourceSelf)
		{
			if (CompilerUserWidgetPropertyContexts.IsValidIndex(CompileBinding.UserWidgetPropertyContextIndex))
			{
				NewBinding.SourcePropertyName = CompilerUserWidgetPropertyContexts[CompileBinding.UserWidgetPropertyContextIndex].PropertyName;
			}
			else if (CompilerSourceCreatorContexts.IsValidIndex(CompileBinding.SourceCreatorContextIndex))
			{
				NewBinding.SourcePropertyName = CompilerSourceCreatorContexts[CompileBinding.SourceCreatorContextIndex].ViewModelContext.GetViewModelName();
			}
			else
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*LOCTEXT("InvalidSourceInternal", "Internal error. The binding doesn't have a valid source.").ToString());
				return false;
			}
		}

		auto AddErroMessage = [this, ViewBinding, BlueprintView](const FText& ErrorMessage)
		{
				if (ViewBinding)
				{
					AddMessageForBinding(*ViewBinding, BlueprintView, ErrorMessage, EBindingMessageType::Error);
				}
				else
				{
					WidgetBlueprintCompilerContext.MessageLog.Error(*ErrorMessage.ToString());
				}
		};

		const FMVVMVCompiledFieldId* CompiledFieldId = CompileResult.FieldIds.Find(CompileBinding.FieldIdHandle);
		if (CompiledFieldId == nullptr && CompileBinding.bFieldIdNeeded)
		{
			AddErroMessage(FText::Format(LOCTEXT("FieldIdNotGenerated", "Could not generate field ID for property '{0}'."), FText::FromName(NewBinding.SourcePropertyName)));
			bIsBindingsValid = false;
			continue;
		}

		const FMVVMVCompiledBinding* CompiledBinding = CompileResult.Bindings.Find(CompileBinding.CompiledBinding.BindingHandle);
		if (CompiledBinding == nullptr && CompileBinding.Type != ECompilerBindingType::ViewModelDynamic)
		{
			AddErroMessage(LOCTEXT("CompiledBindingNotGenerated", "Could not generate compiled binding."));
			bIsBindingsValid = false;
			continue;
		}

		bool bIsOptional = false;
		if (!bIsSourceSelf)
		{
			const FMVVMBlueprintViewModelContext* ViewModelContext = nullptr;
			if (CompilerUserWidgetPropertyContexts.IsValidIndex(CompileBinding.UserWidgetPropertyContextIndex))
			{
				if (CompilerUserWidgetPropertyContexts[CompileBinding.UserWidgetPropertyContextIndex].ViewModelId.IsValid())
				{
					ViewModelContext = BlueprintView->FindViewModel(CompilerUserWidgetPropertyContexts[CompileBinding.UserWidgetPropertyContextIndex].ViewModelId);
					if (ViewModelContext == nullptr)
					{
						AddErroMessage(LOCTEXT("CompiledBindingWithInvalidIVewModelId", "Internal error: the viewmodel became invalid."));
						bIsBindingsValid = false;
						continue;
					}
				}
			}
			else if (CompilerSourceCreatorContexts.IsValidIndex(CompileBinding.SourceCreatorContextIndex))
			{
				ViewModelContext = &CompilerSourceCreatorContexts[CompileBinding.SourceCreatorContextIndex].ViewModelContext;
			}

			if (ViewModelContext)
			{
				bIsOptional = ViewModelContext->bOptional || ViewModelContext->CreationType == EMVVMBlueprintViewModelContextCreationType::Manual;
			}
		}

		if (CompileBinding.Type == ECompilerBindingType::ViewModelDynamic)
		{
			int32 FoundSourceCreatorIndex = ViewExtension->SourceCreators.IndexOfByPredicate([LookFor = CompileBinding.DynamicViewModelName](const FMVVMViewClass_SourceCreator& Other)
				{
					return Other.GetSourceName() == LookFor;
				});

			check(FoundSourceCreatorIndex < std::numeric_limits<int8>::max()); // the index is saved as a int8
			NewBinding.EvaluateSourceCreatorIndex = FoundSourceCreatorIndex;
			if (NewBinding.EvaluateSourceCreatorIndex == INDEX_NONE)
			{
				FText ErrorMessage = FText::Format(LOCTEXT("CompiledViewModelDynamicBindingNotGenerated", "Was not able to find the source for {0}."), FText::FromName(CompileBinding.DynamicViewModelName));
				WidgetBlueprintCompilerContext.MessageLog.Error(*ErrorMessage.ToString());
				bIsBindingsValid = false;
				continue;
			}
		}

		FText WrongBindingTypeInternalErrorMessage = LOCTEXT("WrongBindingTypeInternalError", "Internal Error. Wrong binding type.");
		// FieldId can be null if it's a one time binding
		NewBinding.FieldId = CompiledFieldId ? *CompiledFieldId : FMVVMVCompiledFieldId();
		NewBinding.Binding = CompiledBinding ? *CompiledBinding : FMVVMVCompiledBinding();
		NewBinding.Flags = 0;
		if (ViewBinding)
		{
			if (CompileBinding.Type != ECompilerBindingType::PropertyBinding)
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*WrongBindingTypeInternalErrorMessage.ToString());
				bIsBindingsValid = false;
				continue;
			}

			NewBinding.ExecutionMode = ViewBinding->bOverrideExecutionMode ? ViewBinding->OverrideExecutionMode : (EMVVMExecutionMode)CVarDefaultExecutionMode->GetInt();;
			NewBinding.EditorId = ViewBinding->BindingId;

			NewBinding.Flags |= (ViewBinding->bEnabled) ? FMVVMViewClass_CompiledBinding::EBindingFlags::EnabledByDefault : 0;
			NewBinding.Flags |= (CompileBinding.bIsForwardBinding) ? FMVVMViewClass_CompiledBinding::EBindingFlags::ExecuteAtInitialization : 0;
			NewBinding.Flags |= (IsOneTimeBinding(ViewBinding->BindingType)) ? FMVVMViewClass_CompiledBinding::EBindingFlags::OneTime : 0;
			NewBinding.Flags |= (bIsOptional) ? FMVVMViewClass_CompiledBinding::EBindingFlags::ViewModelOptional : 0;
			NewBinding.Flags |= (ViewBinding->bOverrideExecutionMode) ? FMVVMViewClass_CompiledBinding::EBindingFlags::OverrideExecuteMode : 0;
			NewBinding.Flags |= (bIsSourceSelf) ? FMVVMViewClass_CompiledBinding::EBindingFlags::SourceObjectIsSelf : 0;
		}
		else
		{
			if (CompileBinding.Type != ECompilerBindingType::ViewModelDynamic)
			{
				WidgetBlueprintCompilerContext.MessageLog.Error(*WrongBindingTypeInternalErrorMessage.ToString());
				bIsBindingsValid = false;
				continue;
			}
			NewBinding.ExecutionMode = EMVVMExecutionMode::Immediate;
			NewBinding.EditorId = FGuid();

			NewBinding.Flags |= FMVVMViewClass_CompiledBinding::EBindingFlags::EnabledByDefault;
			NewBinding.Flags |= (bIsOptional) ? FMVVMViewClass_CompiledBinding::EBindingFlags::ViewModelOptional : 0;
			NewBinding.Flags |= FMVVMViewClass_CompiledBinding::EBindingFlags::OverrideExecuteMode; // The mode needs to be Immediate.
			NewBinding.Flags |= (bIsSourceSelf) ? FMVVMViewClass_CompiledBinding::EBindingFlags::SourceObjectIsSelf : 0;
		}

		ViewExtension->CompiledBindings.Emplace(MoveTemp(NewBinding));
	}

	return bIsBindingsValid;
}


TValueOrError<FMVVMViewBlueprintCompiler::FCompiledBinding, FText> FMVVMViewBlueprintCompiler::CreateCompiledBinding(const UWidgetBlueprintGeneratedClass* Class, TArrayView<const UE::MVVM::FMVVMConstFieldVariant> GetterFields, TArrayView<const UE::MVVM::FMVVMConstFieldVariant> SetterFields, const UFunction* ConversionFunction, bool bIsComplexBinding)
{
	FCompiledBinding Result;

	if (ConversionFunction != nullptr)
	{
		TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> FieldPathResult = BindingLibraryCompiler.AddConversionFunctionFieldPath(Class, ConversionFunction);
		if (FieldPathResult.HasError())
		{
			return MakeError(FText::Format(LOCTEXT("CouldNotCreateConversionFunctionFieldPath", "Couldn't create the conversion function field path '{0}'. {1}")
				, FText::FromString(ConversionFunction->GetPathName())
				, FieldPathResult.GetError()));
		}
		Result.ConversionFunction = FieldPathResult.StealValue();
		Result.bIsConversionFunctionComplex = bIsComplexBinding;

		// Sanity check
		if (bIsComplexBinding && !BindingHelper::IsValidForComplexRuntimeConversion(ConversionFunction))
		{
			return MakeError(LOCTEXT("ConversionFunctionIsNotComplex", "Internal Error. The complex conversion function does not respect the prerequist."));
		}
	}

	if (!Result.bIsConversionFunctionComplex)
	{
		static const FText CouldNotCreateSourceFieldPathFormat = LOCTEXT("CouldNotCreateSourceFieldPath", "Couldn't create the source field path '{0}'. {1}");

		// Generate a path to read the value at runtime
		TValueOrError<TArray<FMVVMConstFieldVariant>, FText> GeneratedField = FieldPathHelper::GenerateFieldPathList(GetterFields, true);
		if (GeneratedField.HasError())
		{
			return MakeError(FText::Format(CouldNotCreateSourceFieldPathFormat
				, ::UE::MVVM::FieldPathHelper::ToText(GetterFields)
				, GeneratedField.GetError()));
		}

		TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> FieldPathResult = BindingLibraryCompiler.AddFieldPath(GeneratedField.GetValue(), true);
		if (FieldPathResult.HasError())
		{
			return MakeError(FText::Format(CouldNotCreateSourceFieldPathFormat
				, ::UE::MVVM::FieldPathHelper::ToText(GetterFields)
				, FieldPathResult.GetError()));
		}
		Result.SourceRead = FieldPathResult.StealValue();
	}

	{
		static const FText CouldNotCreateDestinationFieldPathFormat = LOCTEXT("CouldNotCreateDestinationFieldPath", "Couldn't create the destination field path '{0}'. {1}");

		TValueOrError<TArray<FMVVMConstFieldVariant>, FText> GeneratedField = FieldPathHelper::GenerateFieldPathList(SetterFields, false);
		if (GeneratedField.HasError())
		{
			return MakeError(FText::Format(CouldNotCreateDestinationFieldPathFormat
				, ::UE::MVVM::FieldPathHelper::ToText(SetterFields)
				, GeneratedField.GetError()));
		}

		TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> FieldPathResult = BindingLibraryCompiler.AddFieldPath(GeneratedField.GetValue(), false);
		if (FieldPathResult.HasError())
		{
			return MakeError(FText::Format(CouldNotCreateDestinationFieldPathFormat
				, ::UE::MVVM::FieldPathHelper::ToText(SetterFields)
				, FieldPathResult.GetError()));
		}
		Result.DestinationWrite = FieldPathResult.StealValue();
	}

	// Generate the binding
	TValueOrError<FCompiledBindingLibraryCompiler::FBindingHandle, FText> BindingResult = Result.bIsConversionFunctionComplex
		? BindingLibraryCompiler.AddComplexBinding(Result.DestinationWrite, Result.ConversionFunction)
		: BindingLibraryCompiler.AddBinding(Result.SourceRead, Result.DestinationWrite, Result.ConversionFunction);
	if (BindingResult.HasError())
	{
		return MakeError(BindingResult.StealError());
	}
	Result.BindingHandle = BindingResult.StealValue();

	return MakeValue(Result);
}


const FMVVMViewBlueprintCompiler::FCompilerSourceCreatorContext* FMVVMViewBlueprintCompiler::FindViewModelSource(FGuid Id) const
{
	return CompilerSourceCreatorContexts.FindByPredicate([Id](const FCompilerSourceCreatorContext& Other)
		{
			return Other.Type == ECompilerSourceCreatorType::ViewModel ? Other.ViewModelContext.GetViewModelId() == Id : false;
		});
}


TValueOrError<FMVVMViewBlueprintCompiler::FBindingSourceContext, FText> FMVVMViewBlueprintCompiler::CreateBindingSourceContext(const UMVVMBlueprintView* BlueprintView, const UWidgetBlueprintGeneratedClass* Class, const FMVVMBlueprintPropertyPath& PropertyPath, bool bIsOneTimeBinding)
{
	if (PropertyPath.IsEmpty())
	{
		ensureAlways(false);
		return MakeError(LOCTEXT("EmptyPropertyPath", "Empty property path found. This is ilegal."));
	}

	FBindingSourceContext Result;
	if (PropertyPath.IsFromViewModel())
	{
		Result.bIsRootWidget = false;

		const FMVVMBlueprintViewModelContext* SourceViewModelContext = BlueprintView->FindViewModel(PropertyPath.GetViewModelId());
		check(SourceViewModelContext);
		const FName SourceName = SourceViewModelContext->GetViewModelName();
		Result.UserWidgetPropertyContextIndex = CompilerUserWidgetPropertyContexts.IndexOfByPredicate([SourceName](const FCompilerUserWidgetPropertyContext& Other) { return Other.PropertyName == SourceName; });
		check(Result.UserWidgetPropertyContextIndex != INDEX_NONE);

		Result.SourceClass = CompilerUserWidgetPropertyContexts[Result.UserWidgetPropertyContextIndex].Class;
		Result.PropertyPath = CreatePropertyPath(Class, CompilerUserWidgetPropertyContexts[Result.UserWidgetPropertyContextIndex].PropertyName, PropertyPath.GetFields());
	}
	else if (PropertyPath.IsFromWidget())
	{
		const FName SourceName = PropertyPath.GetWidgetName();
		Result.bIsRootWidget = SourceName == Class->ClassGeneratedBy->GetFName();
		if (Result.bIsRootWidget)
		{
			Result.UserWidgetPropertyContextIndex = INDEX_NONE;
			Result.SourceClass = const_cast<UWidgetBlueprintGeneratedClass*>(Class);
			Result.PropertyPath = CreatePropertyPath(Class, FName(), PropertyPath.GetFields());
		}
		else
		{
			Result.UserWidgetPropertyContextIndex = CompilerUserWidgetPropertyContexts.IndexOfByPredicate([SourceName](const FCompilerUserWidgetPropertyContext& Other) { return Other.PropertyName == SourceName; });
			check(Result.UserWidgetPropertyContextIndex != INDEX_NONE);
			Result.SourceClass = CompilerUserWidgetPropertyContexts[Result.UserWidgetPropertyContextIndex].Class;
			Result.PropertyPath = CreatePropertyPath(Class, CompilerUserWidgetPropertyContexts[Result.UserWidgetPropertyContextIndex].PropertyName, PropertyPath.GetFields());
		}
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Not supported yet."));
	}

	// The path may contains another INotifyFieldValueChanged
	TValueOrError<FieldPathHelper::FParsedNotifyBindingInfo, FText> BindingInfoResult = FieldPathHelper::GetNotifyBindingInfoFromFieldPath(Class, Result.PropertyPath);
	if (BindingInfoResult.HasError())
	{
		return MakeError(BindingInfoResult.StealError());
	}
	const FieldPathHelper::FParsedNotifyBindingInfo& BindingInfo = BindingInfoResult.GetValue();
	Result.FieldId = BindingInfo.NotifyFieldId;

	if (BindingInfo.ViewModelIndex < 1 && BindingInfo.NotifyFieldClass && Result.SourceClass)
	{
		if (!Result.SourceClass->IsChildOf(BindingInfo.NotifyFieldClass))
		{
			return MakeError(LOCTEXT("InvalidNotifyFieldClassInternal", "Internal error. The viewmodel class doesn't matches."));
		}
	}

	// The INotifyFieldValueChanged/viewmodel is not the first and only INotifyFieldValueChanged/viewmodel property path.
	//Create a new source in PropertyPath creator mode. Create a special binding to update the viewmodel when it changes.
	//This binding (calling this function) will use the new source.
	if (BindingInfo.ViewModelIndex >= 1 && !bIsOneTimeBinding)
	{
		if (!GetDefault<UMVVMDeveloperProjectSettings>()->bAllowLongSourcePath)
		{
			return MakeError(LOCTEXT("DynamicSourceEntryNotSupport", "Long source entry is not supported. Add the viewmodel manually."));
		}

		int32 SourceCreatorContextIndex = INDEX_NONE;
		for (int32 DynamicIndex = 1; DynamicIndex <= BindingInfo.ViewModelIndex; ++DynamicIndex)
		{
			if (!Result.PropertyPath.IsValidIndex(DynamicIndex))
			{
				return MakeError(LOCTEXT("DynamicSourceEntryInternalIndex", "Internal error. The source index is not valid."));
			}

			FName NewSourceName;
			FString NewSourcePropertyPath;
			{
				TStringBuilder<512> PropertyPathBuilder;
				TStringBuilder<512> DynamicNameBuilder;
				for (int32 Index = 0; Index <= DynamicIndex; ++Index)
				{
					if (Index > 0)
					{
						PropertyPathBuilder << TEXT('.');
						DynamicNameBuilder << TEXT('_');
					}
					PropertyPathBuilder << Result.PropertyPath[Index].GetName();
					DynamicNameBuilder << Result.PropertyPath[Index].GetName();
				}

				NewSourceName = FName(DynamicNameBuilder.ToString());
				NewSourcePropertyPath = PropertyPathBuilder.ToString();
			}

			// Did we already create the new source?
			int32 PreviousSourceCreatorContextIndex = SourceCreatorContextIndex;
			SourceCreatorContextIndex = CompilerSourceCreatorContexts.IndexOfByPredicate([NewSourceName](const FCompilerSourceCreatorContext& Other)
				{
					return Other.Type == ECompilerSourceCreatorType::ViewModelDynamic
						&& Other.ViewModelContext.GetViewModelName() == NewSourceName;
				});
			if (SourceCreatorContextIndex == INDEX_NONE)
			{
				// Create the new source
				{
					const UClass* ViewModelClass = Cast<const UClass>(Result.PropertyPath[DynamicIndex+1].GetOwner());
					if (!ViewModelClass)
					{
						return MakeError(FText::GetEmpty());
					}

					FCompilerSourceCreatorContext SourceCreatorContext;
					SourceCreatorContext.Type = ECompilerSourceCreatorType::ViewModelDynamic;
					SourceCreatorContext.ViewModelContext = FMVVMBlueprintViewModelContext(ViewModelClass, NewSourceName);
					SourceCreatorContext.ViewModelContext.bCreateSetterFunction = false;
					SourceCreatorContext.ViewModelContext.bOptional = false;
					SourceCreatorContext.ViewModelContext.CreationType = EMVVMBlueprintViewModelContextCreationType::PropertyPath;
					SourceCreatorContext.ViewModelContext.ViewModelPropertyPath = NewSourcePropertyPath;

					TValueOrError<FCompiledBindingLibraryCompiler::FFieldPathHandle, FText> ReadFieldPathResult = AddObjectFieldPath(BindingLibraryCompiler, Class, SourceCreatorContext.ViewModelContext.ViewModelPropertyPath, SourceCreatorContext.ViewModelContext.GetViewModelClass());
					if (ReadFieldPathResult.HasError())
					{
						return MakeError(FText::Format(LOCTEXT("DynamicSourceEntryInvalidPath", "Internal error. {0}."), ReadFieldPathResult.StealError()));
					}
					SourceCreatorContext.ReadPropertyPath = ReadFieldPathResult.StealValue();

					SourceCreatorContextIndex = CompilerSourceCreatorContexts.Add(MoveTemp(SourceCreatorContext));
				}

				// Create the binding to update the source when it changes.
				{
					FCompilerBinding NewCompilerBinding;
					NewCompilerBinding.Type = ECompilerBindingType::ViewModelDynamic;
					NewCompilerBinding.bSourceIsUserWidget = false;
					NewCompilerBinding.bIsForwardBinding = true;
					NewCompilerBinding.bFieldIdNeeded = true;
					NewCompilerBinding.DynamicViewModelName = NewSourceName;

					if (PreviousSourceCreatorContextIndex == INDEX_NONE)
					{
						NewCompilerBinding.UserWidgetPropertyContextIndex = Result.UserWidgetPropertyContextIndex;
					}
					else
					{
						NewCompilerBinding.SourceCreatorContextIndex = PreviousSourceCreatorContextIndex;
					}

					{
						const UClass* OwnerClass = Cast<const UClass>(Result.PropertyPath[DynamicIndex].GetOwner());
						if (!OwnerClass)
						{
							return MakeError(FText::GetEmpty());
						}

						TValueOrError<FCompiledBindingLibraryCompiler::FFieldIdHandle, FText> AddFieldResult = BindingLibraryCompiler.AddFieldId(OwnerClass, Result.PropertyPath[DynamicIndex].GetName());
						if (AddFieldResult.HasError())
						{
							return MakeError(AddFieldResult.StealError());
						}
						NewCompilerBinding.FieldIdHandle = AddFieldResult.StealValue();
					}

					CompilerBindings.Add(MoveTemp(NewCompilerBinding));
				}
			}
		}

		Result.SourceClass = BindingInfo.NotifyFieldClass;
		Result.FieldId = BindingInfo.NotifyFieldId;
		Result.UserWidgetPropertyContextIndex = INDEX_NONE;
		Result.SourceCreatorContextIndex = SourceCreatorContextIndex;
		Result.bIsRootWidget = false;
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
		const int32 DestinationVariableContextIndex = CompilerUserWidgetPropertyContexts.IndexOfByPredicate([DestinationName](const FCompilerUserWidgetPropertyContext& Other) { return Other.PropertyName == DestinationName; });
		check(DestinationVariableContextIndex != INDEX_NONE);

		return CreatePropertyPath(Class, CompilerUserWidgetPropertyContexts[DestinationVariableContextIndex].PropertyName, PropertyPath.GetFields());
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
			const int32 DestinationVariableContextIndex = CompilerUserWidgetPropertyContexts.IndexOfByPredicate([DestinationName](const FCompilerUserWidgetPropertyContext& Other) { return Other.PropertyName == DestinationName; });
			if (ensureAlwaysMsgf(DestinationVariableContextIndex != INDEX_NONE, TEXT("Could not find source context for destination '%s'"), *DestinationName.ToString()))
			{
				return CreatePropertyPath(Class, CompilerUserWidgetPropertyContexts[DestinationVariableContextIndex].PropertyName, PropertyPath.GetFields());
			}
		}
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Not supported yet."));
		return CreatePropertyPath(Class, FName(), PropertyPath.GetFields());
	}

	return TArray<FMVVMConstFieldVariant>();
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

#undef LOCTEXT_NAMESPACE
