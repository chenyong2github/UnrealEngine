// Copyright Epic Games, Inc. All Rights Reserved.

#include "View/MVVMView.h"
#include "FieldNotificationDelegate.h"
#include "View/MVVMViewClass.h"
#include "Misc/MemStack.h"
#include "View/MVVMBindingSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Debugging/MVVMDebugging.h"
#include "Engine/Engine.h"
#include "MVVMMessageLog.h"
#include "Templates/ValueOrError.h"
#include "Types/MVVMFieldContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MVVMView)


#define LOCTEXT_NAMESPACE "MVVMView"

///////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////

void UMVVMView::ConstructView(const UMVVMViewClass* InClassExtension)
{
	ensure(ClassExtension == nullptr);
	ClassExtension = InClassExtension;

	Sources.Reserve(ClassExtension->GetViewModelCreators().Num());
	for (const FMVVMViewClass_SourceCreator& Item : ClassExtension->GetViewModelCreators())
	{
		FMVVMViewSource& NewCreatedSource = Sources.AddDefaulted_GetRef();
		NewCreatedSource.Source = nullptr;
		NewCreatedSource.SourceName = Item.GetSourceName();
		NewCreatedSource.bCreatedSource = true;
	}
}


void UMVVMView::Construct()
{
	check(ClassExtension);
	check(bConstructed == false);

	if (ClassExtension->DoesInitializeAtConstruction())
	{
		InitializeBindings();
	}

	bConstructed = true;
}

void UMVVMView::InitializeBindings()
{
	check(bInitialized == false);

	// Init ViewModel instances
	UUserWidget* UserWidget = GetUserWidget();
	const TArrayView<const FMVVMViewClass_SourceCreator> AllViewModelCreators = ClassExtension->GetViewModelCreators();
	for (int32 Index = 0; Index < AllViewModelCreators.Num(); ++Index)
	{
		const FMVVMViewClass_SourceCreator& Item = AllViewModelCreators[Index];
		FMVVMViewSource& ViewSource = Sources[Index];
		check(ViewSource.SourceName == Item.GetSourceName());
		check(ViewSource.bCreatedSource == true);

		if (!ViewSource.bSetManually)
		{
			UObject* NewSource = Item.CreateInstance(ClassExtension, this, UserWidget);
			if (NewSource)
			{
				if (Item.IsSourceAUserWidgetProperty())
				{
					FObjectPropertyBase* FoundObjectProperty = FindFProperty<FObjectPropertyBase>(UserWidget->GetClass(), Item.GetSourceName());
					if (ensureAlwaysMsgf(FoundObjectProperty, TEXT("The compiler should have added the property")))
					{
						if (ensure(NewSource->GetClass()->IsChildOf(FoundObjectProperty->PropertyClass)))
						{
							FoundObjectProperty->SetObjectPropertyValue_InContainer(UserWidget, NewSource);
							ViewSource.bAssignedToUserWidgetProperty = true;
						}
					}
				}
			}
			ViewSource.Source = NewSource;
		}
	}

#if UE_WITH_MVVM_DEBUGGING
	UE::MVVM::FDebugging::BroadcastViewConstructed(this);
#endif

	bHasEveryTickBinding = false;

	const TArrayView<const FMVVMViewClass_CompiledBinding> CompiledBindings = ClassExtension->GetCompiledBindings();
	for (int32 Index = 0; Index < CompiledBindings.Num(); ++Index)
	{
		const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
		if (Binding.IsEnabledByDefault())
		{
			EnableLibraryBinding(Binding, Index);
		}
	}

	bInitialized = true;

	if (bHasEveryTickBinding)
	{
		GEngine->GetEngineSubsystem<UMVVMBindingSubsystem>()->AddViewWithEveryTickBinding(this);
	}
}


void UMVVMView::Destruct()
{
	check(bConstructed == true);
	bConstructed = false;

#if UE_WITH_MVVM_DEBUGGING
	UE::MVVM::FDebugging::BroadcastViewBeginDestruction(this);
#endif

	if (bInitialized)
	{
		DeintializeBindings();
	}
}

void UMVVMView::DeintializeBindings()
{
	check(bInitialized == true);
	bInitialized = false;

	UUserWidget* UserWidget = GetUserWidget();
	check(UserWidget);

	for (FMVVMViewSource& Source : Sources)
	{
		if (Source.RegisteredCount > 0 && Source.Source)
		{
			TScriptInterface<INotifyFieldValueChanged> SourceAsInterface = Source.Source;
			checkf(SourceAsInterface.GetInterface(), TEXT("It was added as a INotifyFieldValueChanged. It should still be."));
			SourceAsInterface->RemoveAllFieldValueChangedDelegates(this);
		}

		// For GC release any object used by the view
		if (!Source.bSetManually)
		{
			Source.Source = nullptr;
			if (Source.bAssignedToUserWidgetProperty)
			{
				FObjectPropertyBase* FoundObjectProperty = FindFProperty<FObjectPropertyBase>(UserWidget->GetClass(), Source.SourceName);
				if (ensureAlwaysMsgf(FoundObjectProperty, TEXT("The compiler should have added the property")))
				{
					FoundObjectProperty->SetObjectPropertyValue_InContainer(UserWidget, nullptr);
				}
				Source.bAssignedToUserWidgetProperty = false;
			}
		}
	}

	RegisteredLibraryBindings.Reset();

	if (bHasEveryTickBinding)
	{
		GEngine->GetEngineSubsystem<UMVVMBindingSubsystem>()->RemoveViewWithEveryTickBinding(this);
	}
	bHasEveryTickBinding = false;
}


TScriptInterface<INotifyFieldValueChanged> UMVVMView::GetViewModel(FName ViewModelName) const
{
	const FMVVMViewSource* RegisteredSource = FindViewSource(ViewModelName);
	return RegisteredSource ? RegisteredSource->Source : nullptr;
}


bool UMVVMView::SetViewModel(FName ViewModelName, TScriptInterface<INotifyFieldValueChanged> NewValue)
{
	return SetSourceInternal(ViewModelName, NewValue, false);
}


bool UMVVMView::SetViewModelByClass(TScriptInterface<INotifyFieldValueChanged> NewValue)
{
	if (!NewValue.GetObject())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelInvalidObject", "The new viewmodel is null."));
		return false;
	}

	if (ClassExtension == nullptr)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelInvalidClass", "The view is not constructed."));
		return false;
	}

	FName ViewModelName;
	for (const FMVVMViewClass_SourceCreator& SourceCreator : ClassExtension->GetViewModelCreators())
	{
		if (NewValue.GetObject()->GetClass()->IsChildOf(SourceCreator.GetSourceClass()))
		{
			if (ViewModelName.IsNone())
			{
				ViewModelName = SourceCreator.GetSourceName();
			}
			else
			{
				UE::MVVM::FMessageLog Log(GetUserWidget());
				Log.Error(FText::Format(LOCTEXT("SetViewModelViewModelNotUnique", "More than one viewmodel match the type of the passed instance. Make sure there exists only one viewmodel of type {0} in the widget blueprint, or try SetViewModel by name."), FText::FromName(NewValue.GetObject()->GetClass()->GetFName())));
				return false;
			}
		}
	}

	if (ViewModelName.IsNone())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelViewModelNotFound", "A created viewmodel matching the class of the passed instance could not be found."));
		return false;
	}

	return SetViewModel(ViewModelName, NewValue);
}


bool UMVVMView::EvaluateSourceCreator(int32 SourceCreatorIndex)
{
	if (!ClassExtension->GetViewModelCreators().IsValidIndex(SourceCreatorIndex))
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(FText::Format(LOCTEXT("ReevaluateDynamicSourceViewModelNameNotFound", "Internal error. The dynamic source {0} cound not be found."), FText::AsNumber(SourceCreatorIndex)));
		return false;
	}

	UUserWidget* UserWidget = GetUserWidget();
	const TArrayView<const FMVVMViewClass_SourceCreator> ViewModelCreators = ClassExtension->GetViewModelCreators();
	const FMVVMViewClass_SourceCreator& SourceCreator = ViewModelCreators[SourceCreatorIndex];
	UObject* NewSource = SourceCreator.CreateInstance(ClassExtension, this, UserWidget);
	ensureMsgf((NewSource == nullptr || NewSource->GetClass()->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())), TEXT("The source has implement the interface. It should be check at compile time."));
	return SetSourceInternal(SourceCreator.GetSourceName(), NewSource, true);
}


bool UMVVMView::SetSourceInternal(FName ViewModelName, TScriptInterface<INotifyFieldValueChanged> NewValue, bool bForDynamicSource)
{
	if (ViewModelName.IsNone())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelInvalidName", "The viewmodel name is empty."));
		return false;
	}

	if (ClassExtension == nullptr)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelInvalidClass", "The view is not constructed."));
		return false;
	}

	const int32 AllCreatedSourcesIndex = ClassExtension->GetViewModelCreators().IndexOfByPredicate([ViewModelName](const FMVVMViewClass_SourceCreator& Item)
		{
			return Item.GetSourceName() == ViewModelName;
		});
		
	if (AllCreatedSourcesIndex == INDEX_NONE)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelViewModelNameNotFound", "The viewmodel name could not be found."));
		return false;
	}

	const TArrayView<const FMVVMViewClass_SourceCreator> ViewModelCreators = ClassExtension->GetViewModelCreators();
	const FMVVMViewClass_SourceCreator& SourceCreator = ViewModelCreators[AllCreatedSourcesIndex];

	if (bForDynamicSource && !SourceCreator.CanBeEvaluated())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(FText::Format(LOCTEXT("SetViewModelCannotBeEvalaute", "The new viewmodel {0} cannot be evaluated again."), FText::FromName(ViewModelName)));
		return false;
	}
	else if (!bForDynamicSource && !SourceCreator.CanBeSet())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(FText::Format(LOCTEXT("SetViewModelCannotBeSet", "The new viewmodel {0} cannot be set."), FText::FromName(ViewModelName)));
		return false;
	}
			
	if (NewValue.GetObject()
		&& !NewValue.GetObject()->GetClass()->IsChildOf(SourceCreator.GetSourceClass()))
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelInvalidValueType", "The new viewmodel is not of the expected type."));
		UE_LOG(LogMVVM, Error, TEXT("The viewmodel name '%s' is invalid for the view '%s'"), *ViewModelName.ToString(), *GetFullName());
		return false;
	}
			
	if (!Sources.IsValidIndex(AllCreatedSourcesIndex) || Sources[AllCreatedSourcesIndex].SourceName != ViewModelName)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(LOCTEXT("SetViewModelInvalidCreatedSource", "An internal error occurs. The new viewmodel is not found."));
		UE_LOG(LogMVVM, Error, TEXT("The viewmodel name '%s' is invalid for the view '%s'"), *ViewModelName.ToString(), *GetFullName());
		return false;
	}

	FMVVMViewSource& ViewSource = Sources[AllCreatedSourcesIndex];
	UObject* PreviousValue = Sources[AllCreatedSourcesIndex].Source;
	if (PreviousValue != NewValue.GetObject())
	{
		const TArrayView<const FMVVMViewClass_CompiledBinding> CompiledBindings = ClassExtension->GetCompiledBindings();

		// Unregister any bindings from that source
		for (int32 Index = 0; Index < CompiledBindings.Num(); ++Index)
		{
			const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
			if (IsLibraryBindingEnabled(Index) && Binding.GetSourceName() == ViewModelName)
			{
				DisableLibraryBinding(Binding, Index);
			}
		}

		ViewSource.Source = NewValue.GetObject();
		ViewSource.bSetManually = !bForDynamicSource;
		if (SourceCreator.IsSourceAUserWidgetProperty())
		{
			FObjectPropertyBase* FoundObjectProperty = FindFProperty<FObjectPropertyBase>(GetUserWidget()->GetClass(), ViewModelName);
			if (ensure(FoundObjectProperty))
			{
				FoundObjectProperty->SetObjectPropertyValue_InContainer(GetUserWidget(), NewValue.GetObject());
				ViewSource.bAssignedToUserWidgetProperty = true;
			}
		}

		bool bPreviousEveryTickBinding = bHasEveryTickBinding;
		bHasEveryTickBinding = false;
		// Register back any binding that was previously enabled
		if (NewValue.GetObject() && bInitialized)
		{
			// Enabled the default bindings
			for (int32 Index = 0; Index < CompiledBindings.Num(); ++Index)
			{
				const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
				if (Binding.IsEnabledByDefault())
				{
					// Binding on this viewmodel
					if (Binding.GetSourceName() == ViewModelName)
					{
						EnableLibraryBinding(Binding, Index);
						// Bindings that depends on this binding
						if (Binding.IsEvaluateSourceCreatorBinding())
						{
							int32 ParentSourceIndex = ClassExtension->GetViewModelCreators().IndexOfByPredicate([ViewModelName](const FMVVMViewClass_SourceCreator& Item)
								{
									return Item.GetParentSourceName() == ViewModelName;
								});
							if (ParentSourceIndex != INDEX_NONE)
							{
								check(ParentSourceIndex != AllCreatedSourcesIndex)
								EvaluateSourceCreator(ParentSourceIndex); // it will deactivate previous binding 
							}
						}
					}
				}
			}
		}

		if (bPreviousEveryTickBinding != bHasEveryTickBinding)
		{
			if (bHasEveryTickBinding)
			{
				GEngine->GetEngineSubsystem<UMVVMBindingSubsystem>()->AddViewWithEveryTickBinding(this);
			}
			else
			{
				GEngine->GetEngineSubsystem<UMVVMBindingSubsystem>()->RemoveViewWithEveryTickBinding(this);
			}
		}
	}
	return true;
}


namespace UE::MVVM::Private
{
	using FRecursiveDetctionElement = TTuple<const UObject*, int32>;
	TArray<FRecursiveDetctionElement> RecursiveDetector;
}

void UMVVMView::HandledLibraryBindingValueChanged(UObject* InViewModelOrWidget, UE::FieldNotification::FFieldId InFieldId, int32 InCompiledBindingIndex) const
{
	check(InViewModelOrWidget);
	check(InFieldId.IsValid());

	if (ensure(ClassExtension))
	{
		checkf(ClassExtension->GetCompiledBindings().IsValidIndex(InCompiledBindingIndex), TEXT("The binding at index '%d' does not exist. The binding was probably not cleared on destroyed."), InCompiledBindingIndex);
		const FMVVMViewClass_CompiledBinding& Binding = ClassExtension->GetCompiledBinding(InCompiledBindingIndex);

		EMVVMExecutionMode ExecutionMode = Binding.GetExecuteMode();
		if (ExecutionMode == EMVVMExecutionMode::Delayed)
		{
			GEngine->GetEngineSubsystem<UMVVMBindingSubsystem>()->AddDelayedBinding(this, InCompiledBindingIndex);
		}
		else if (ExecutionMode != EMVVMExecutionMode::Tick)
		{
			// Test for recursivity
			const UMVVMView* Self = this;
			if (UE::MVVM::Private::RecursiveDetector.FindByPredicate([Self, InCompiledBindingIndex](const UE::MVVM::Private::FRecursiveDetctionElement& Element)
				{
					return Element.Get<0>() == Self && Element.Get<1>() == InCompiledBindingIndex;
				}) != nullptr)
			{
				ensureAlwaysMsgf(false, TEXT("Recursive binding detected"));
				//Todo add more infos. Callstack maybe? Log the chain?
				UE::MVVM::FMessageLog Log(GetUserWidget());
				Log.Error(LOCTEXT("RecursionDetected", "A recursive binding was detected (ie. A->B->C->A->B->C) at runtime."));
				return;
			}

			{
				UE::MVVM::Private::RecursiveDetector.Emplace(this, InCompiledBindingIndex);

				ExecuteLibraryBinding(Binding, InCompiledBindingIndex);

				UE::MVVM::Private::RecursiveDetector.Pop();
			}
		}
		else
		{
			ensureMsgf(false, TEXT("We should not have registered the binding since it will always be executed."));
		}
	}
}


void UMVVMView::ExecuteDelayedBinding(const FMVVMViewDelayedBinding& DelayedBinding) const
{
	if (ensure(ClassExtension))
	{
		if (ensure(ClassExtension->GetCompiledBindings().IsValidIndex(DelayedBinding.GetCompiledBindingIndex())))
		{
			if (IsLibraryBindingEnabled(DelayedBinding.GetCompiledBindingIndex()))
			{
				const FMVVMViewClass_CompiledBinding& Binding = ClassExtension->GetCompiledBinding(DelayedBinding.GetCompiledBindingIndex());

				// Test for recursivity
				int32 CompiledBindingIndex = DelayedBinding.GetCompiledBindingIndex();
				const UMVVMView* Self = this;
				if (UE::MVVM::Private::RecursiveDetector.FindByPredicate([Self, CompiledBindingIndex](const UE::MVVM::Private::FRecursiveDetctionElement& Element)
					{
						return Element.Get<0>() == Self && Element.Get<1>() == CompiledBindingIndex;
					}) != nullptr)
				{
					ensureAlwaysMsgf(false, TEXT("Recursive binding detected"));
					//Todo add more infos. Callstack maybe? Log the chain?
					UE::MVVM::FMessageLog Log(GetUserWidget());
					Log.Error(LOCTEXT("RecursionDetected", "A recursive binding was detected (ie. A->B->C->A->B->C) at runtime."));
					return;
				}

				{
					UE::MVVM::Private::RecursiveDetector.Emplace(this, CompiledBindingIndex);

					ExecuteLibraryBinding(Binding, DelayedBinding.GetCompiledBindingIndex());

					UE::MVVM::Private::RecursiveDetector.Pop();
				}
			}
		}
	}
}


void UMVVMView::ExecuteEveryTickBindings() const
{
	ensure(bHasEveryTickBinding);

	if (ClassExtension)
	{
		const TArrayView<const FMVVMViewClass_CompiledBinding> CompiledBindings = ClassExtension->GetCompiledBindings();

		for (int32 Index = 0; Index < CompiledBindings.Num(); ++Index)
		{
			const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
			if (Binding.GetExecuteMode() == EMVVMExecutionMode::Tick && IsLibraryBindingEnabled(Index))
			{
				ExecuteLibraryBinding(Binding, Index);
			}
		}
	}
}


void UMVVMView::ExecuteLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex) const
{
	check(ClassExtension);
	check(GetUserWidget());

	bool bError = false;
	if (!Binding.IsEvaluateSourceCreatorBinding())
	{
		FMVVMCompiledBindingLibrary::EConversionFunctionType FunctionType = Binding.IsConversionFunctionComplex() ? FMVVMCompiledBindingLibrary::EConversionFunctionType::Complex : FMVVMCompiledBindingLibrary::EConversionFunctionType::Simple;
		TValueOrError<void, FMVVMCompiledBindingLibrary::EExecutionFailingReason> ExecutionResult = ClassExtension->GetBindingLibrary().Execute(GetUserWidget(), Binding.GetBinding(), FunctionType);

#if UE_WITH_MVVM_DEBUGGING
		if (ExecutionResult.HasError())
		{
			UE::MVVM::FDebugging::BroadcastLibraryBindingExecuted(this, Binding, ExecutionResult.GetError());
		}
#endif

		if (ExecutionResult.HasError())
		{
			UE::MVVM::FMessageLog Log(GetUserWidget());
			Log.Error(FText::Format(LOCTEXT("ExecuteBindingFailGeneric", "The binding '{0}' was not executed. {1}.")
#if UE_WITH_MVVM_DEBUGGING
				, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::Short()))
#else
				, FText::AsNumber(BindingIndex)
#endif
				, FMVVMCompiledBindingLibrary::LexToText(ExecutionResult.GetError())
			));
			bError = true;
		}
	}
	else
	{
		if (!const_cast<UMVVMView*>(this)->EvaluateSourceCreator(Binding.GetEvaluateSourceCreatorBindingIndex()))
		{
			UE::MVVM::FMessageLog Log(GetUserWidget());
			Log.Error(FText::Format(LOCTEXT("ExecuteBindingFailEvaluate", "The evaluate source creator binding '{0}' was not executed.")
#if UE_WITH_MVVM_DEBUGGING
				, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::Short()))
#else
				, FText::AsNumber(BindingIndex)
#endif
			));
			bError = true;
		}
	}

	if (bLogBinding && !bError)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("ExecuteBindingGeneric", "Execute binding '{0}'.")
#if UE_WITH_MVVM_DEBUGGING
			, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::All()))
#else
			, FText::AsNumber(BindingIndex)
#endif
		));

#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingExecuted(this, Binding);
#endif
	}
}


//void UMVVMView::SetLibraryBindingEnabled(FGuid ViewModelId, FMVVMBindingName BindingName, bool bEnable)
//{
//	if (ensure(ClassExtension))
//	{
//		int32 LibraryBindingIndex = ClassExtension->IndexOfCompiledBinding(ViewModelId, BindingName);
//		if (LibraryBindingIndex != INDEX_NONE)
//		{
//			const bool bIsEnabled = IsLibraryBindingEnabled(LibraryBindingIndex);
//			if (bIsEnabled != bEnable)
//			{
//				const FMVVMViewClass_CompiledBinding& Item = ClassExtension->GetCompiledBindings(LibraryBindingIndex);
//				if (bEnable)
//				{
//					EnableLibraryBinding(Item, LibraryBindingIndex);
//				}
//				else
//				{
//					DisableLibraryBinding(Item, LibraryBindingIndex);
//				}
//			}
//		}
//	}
//}


//bool UMVVMView::IsLibraryBindingEnabled(FGuid ViewModelId, FMVVMBindingName BindingName) const
//{
//	if (ensure(ClassExtension))
//	{
//		int32 LibraryBindingIndex = ClassExtension->IndexOfCompiledBinding(ViewModelId, BindingName);
//		return IsLibraryBindingEnabled(LibraryBindingIndex);
//	}
//	return false;
//}


bool UMVVMView::IsLibraryBindingEnabled(int32 InBindindIndex) const
{
	return RegisteredLibraryBindings.IsValidIndex(InBindindIndex) && RegisteredLibraryBindings[InBindindIndex];
}


void UMVVMView::EnableLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	RegisteredLibraryBindings.PadToNum(BindingIndex + 1, false);
	check(RegisteredLibraryBindings[BindingIndex] == false);

	EMVVMExecutionMode ExecutionMode = Binding.GetExecuteMode();
	bool bCanExecute = true;
	bool bRegistered = false;
	if (!Binding.IsOneTime() && ExecutionMode != EMVVMExecutionMode::Tick)
	{
		bRegistered = RegisterLibraryBinding(Binding, BindingIndex);
		bCanExecute = bRegistered;
	}

	if (bRegistered && bLogBinding)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("EnableLibraryBinding", "Enable binding '{0}'.")
#if UE_WITH_MVVM_DEBUGGING
			, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::All()))
#else
			, FText::AsNumber(BindingIndex)
#endif
		));
	}

	RegisteredLibraryBindings[BindingIndex] = bRegistered;

	if (bCanExecute && Binding.NeedsExecutionAtInitialization())
	{
		ExecuteLibraryBinding(Binding, BindingIndex);
	}

	bHasEveryTickBinding = bHasEveryTickBinding || ExecutionMode == EMVVMExecutionMode::Tick;
}


void UMVVMView::DisableLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	check(IsLibraryBindingEnabled(BindingIndex));


	EMVVMExecutionMode ExecutionMode = Binding.GetExecuteMode();
	RegisteredLibraryBindings[BindingIndex] = false;
	if (!Binding.IsOneTime() && ExecutionMode != EMVVMExecutionMode::Tick)
	{
		UnregisterLibraryBinding(Binding, BindingIndex);
	}

	if (bLogBinding)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("DisableLibraryBinding", "Disable binding '{0}'.")
#if UE_WITH_MVVM_DEBUGGING
			, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::All()))
#else
			, FText::AsNumber(BindingIndex)
#endif
			));
	}
}


bool UMVVMView::RegisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	check(ClassExtension);

	auto LogMessage = [this, &Binding, BindingIndex](const FText& Message)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(FText::Format(LOCTEXT("RegisterBindingFailed_Format", "Widget '{0}' can't register binding '{1}'. {2}")
			, FText::FromString(GetFullName())
#if UE_WITH_MVVM_DEBUGGING
			, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::Short()))
#else
			, FText::AsNumber(BindingIndex)
#endif
			, Message
			));
	};

	TValueOrError<UE::FieldNotification::FFieldId, void> FieldIdResult = ClassExtension->GetBindingLibrary().GetFieldId(Binding.GetSourceFieldId());
	if (FieldIdResult.HasError())
	{
		LogMessage(LOCTEXT("RegisterBindingFailed_InvalidFieldId", "The FieldId is invalid."));

#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_InvalidFieldId);
#endif
		return false;
	}

	UE::FieldNotification::FFieldId FieldId = FieldIdResult.StealValue();
	if (!FieldId.IsValid())
	{
		LogMessage(LOCTEXT("RegisterBindingFailed_FieldIdNotFound", "The FieldId was not found on the source."));

#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_FieldIdNotFound);
#endif
		return false;
	}

	// The source may not have been created because the property path was wrong.
	TScriptInterface<INotifyFieldValueChanged> Source = FindSource(Binding, BindingIndex, true);
	if (Source.GetInterface() == nullptr)
	{
		if (!Binding.IsRegistrationOptional())
		{
			LogMessage(LOCTEXT("RegisterBindingFailed_InvalidSourceObject", "The source object is invalid."));
		}

#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_InvalidSource);
#endif
		return false;
	}

	UE::FieldNotification::FFieldMulticastDelegate::FDelegate Delegate = UE::FieldNotification::FFieldMulticastDelegate::FDelegate::CreateUObject(this, &UMVVMView::HandledLibraryBindingValueChanged, BindingIndex);
	if (Source->AddFieldValueChangedDelegate(FieldId, MoveTemp(Delegate)).IsValid())
	{
		if (FMVVMViewSource* RegisteredSource = FindViewSource(Binding.GetSourceName()))
		{
			++RegisteredSource->RegisteredCount;
		}
		else
		{
			// if it's a widget, it may not be in the Sources
			FMVVMViewSource& NewSource = Sources.AddDefaulted_GetRef();
			NewSource.Source = Source.GetObject();
			NewSource.SourceName = Binding.GetSourceName();
			NewSource.RegisteredCount = 1;
		}

#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Success);
#endif
	}
	else
	{
#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Success);
#endif
	}

	return true;
}


void UMVVMView::UnregisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	TScriptInterface<INotifyFieldValueChanged> Source = FindSource(Binding, BindingIndex, true);
	if (Source.GetInterface() && ClassExtension)
	{
		TValueOrError<UE::FieldNotification::FFieldId, void> FieldIdResult = ClassExtension->GetBindingLibrary().GetFieldId(Binding.GetSourceFieldId());
		if (ensureMsgf(FieldIdResult.HasValue() && FieldIdResult.GetValue().IsValid(), TEXT("If the binding was enabled then the FieldId should exist.")))
		{
			Source->RemoveAllFieldValueChangedDelegates(FieldIdResult.GetValue(), this);
		}

		FMVVMViewSource* RegisteredSource = FindViewSource(Binding.GetSourceName());
		if (ensureMsgf(RegisteredSource, TEXT("If the binding was enabled, then the source should also be there.")))
		{
			ensureMsgf(RegisteredSource->RegisteredCount > 0, TEXT("The count should match the number of RegisterLibraryBinding and UnregisterLibraryBinding"));
			--RegisteredSource->RegisteredCount;
		}
	}
}


TScriptInterface<INotifyFieldValueChanged> UMVVMView::FindSource(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex, bool bAllowNull) const
{
	UUserWidget* UserWidget = GetUserWidget();
	check(UserWidget);
	check(ClassExtension);

	if (Binding.IsSourceUserWidget())
	{
		return TScriptInterface<INotifyFieldValueChanged>(UserWidget);
	}
	else
	{
		auto LogMessage = [this, &Binding, BindingIndex](const FText& Message)
		{
			UE::MVVM::FMessageLog Log(GetUserWidget());
			Log.Error(FText::Format(LOCTEXT("FindSourceFailed_Format", "Widget '{0}' can't evaluate the source for binding '{1}'. {2}.")
				, FText::FromString(GetFullName())
#if UE_WITH_MVVM_DEBUGGING
				, FText::FromString(Binding.ToString(ClassExtension->GetBindingLibrary(), FMVVMViewClass_CompiledBinding::FToStringArgs::Short()))
#else
				, FText::AsNumber(BindingIndex)
#endif
				, Message
			));
		};

		UObject* Source = nullptr;

		if (const FMVVMViewSource* RegisteredSource = FindViewSource(Binding.GetSourceName()))
		{
			Source = RegisteredSource->Source.Get();
		}
		else
		{
			const FObjectPropertyBase* SourceObjectProperty = CastField<FObjectPropertyBase>(UserWidget->GetClass()->FindPropertyByName(Binding.GetSourceName()));
			if (SourceObjectProperty == nullptr)
			{
				LogMessage(LOCTEXT("FindSourceFailed_InvalidPropertyName", "The property name is invalid."));
				return TScriptInterface<INotifyFieldValueChanged>();
			}

			Source = SourceObjectProperty->GetObjectPropertyValue_InContainer(UserWidget);
		}

		if (Source == nullptr)
		{
			if (!bAllowNull)
			{
				LogMessage(LOCTEXT("FindSourceFailed_InvalidSourceObject", "The source is an invalid object."));
			}
			return TScriptInterface<INotifyFieldValueChanged>();
		}

		if (!Source->Implements<UNotifyFieldValueChanged>())
		{
			LogMessage(LOCTEXT("FindSourceFailed_SourceObjectNotImplements", "The source does not implements INotifyFieldValueChanged."));
			return TScriptInterface<INotifyFieldValueChanged>();
		}

		return TScriptInterface<INotifyFieldValueChanged>(Source);
	}
}


FMVVMViewSource* UMVVMView::FindViewSource(const FName SourceName)
{
	return Sources.FindByPredicate([SourceName](const FMVVMViewSource& Other){ return Other.SourceName == SourceName; });
}


const FMVVMViewSource* UMVVMView::FindViewSource(const FName SourceName) const
{
	return Sources.FindByPredicate([SourceName](const FMVVMViewSource& Other) { return Other.SourceName == SourceName; });
}


#undef LOCTEXT_NAMESPACE

