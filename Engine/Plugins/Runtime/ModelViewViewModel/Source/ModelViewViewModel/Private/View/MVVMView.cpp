// Copyright Epic Games, Inc. All Rights Reserved.

#include "View/MVVMView.h"
#include "FieldNotification/FieldMulticastDelegate.h"
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
}


void UMVVMView::Construct()
{
	check(ClassExtension);
	check(bConstructed == false);
	check(Sources.Num() == 0);

	// Init ViewModel instances
	for (const FMVVMViewClass_SourceCreator& Item : ClassExtension->GetViewModelCreators())
	{
		UUserWidget* UserWidget = GetUserWidget();
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
					}
				}
			}
		}

		check(Item.GetSourceName().IsValid());
		check(!FindViewSource(Item.GetSourceName()));

		FMVVMViewSource& NewCreatedSource = Sources.AddDefaulted_GetRef();
		NewCreatedSource.Source = NewSource;
		NewCreatedSource.SourceName = Item.GetSourceName();
		NewCreatedSource.bCreatedSource = true;
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

	bConstructed = true;

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

	for (const FMVVMViewSource& Source : Sources)
	{
		if (Source.RegisteredCout > 0 && Source.Source)
		{
			TScriptInterface<INotifyFieldValueChanged> SourceAsInterface = Source.Source;
			checkf(SourceAsInterface.GetInterface(), TEXT("It was added as a INotifyFieldValueChanged. It should still be."));
			SourceAsInterface->RemoveAllFieldValueChangedDelegates(this);
		}
	}
	// Todo, do we need to reset the FProperty also?
	Sources.Reset(); // For GC release any object used by the view
	EnabledLibraryBindings.Reset();

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
	if (!ViewModelName.IsNone() && ClassExtension != nullptr)
	{
		const int32 AllCreatedSourcesIndex = ClassExtension->GetViewModelCreators().IndexOfByPredicate([ViewModelName](const FMVVMViewClass_SourceCreator& Item)
			{
				return Item.GetSourceName() == ViewModelName;
			});
		{
			if (AllCreatedSourcesIndex == INDEX_NONE)
			{
				UE::MVVM::FMessageLog Log(GetUserWidget());
				Log.Error(LOCTEXT("SetViewModelViewModelNameNotFound", "The viewmodel name could not be found."));
				return false;
			}
			else if (NewValue.GetObject()
				&& !NewValue.GetObject()->GetClass()->IsChildOf(ClassExtension->GetViewModelCreators()[AllCreatedSourcesIndex].GetSourceClass()))
			{
				UE::MVVM::FMessageLog Log(GetUserWidget());
				Log.Error(LOCTEXT("SetViewModelInvalidValueType", "The new viewmodel is not of the expected type."));
				UE_LOG(LogMVVM, Error, TEXT("The viewmodel name '%s' is invalid for the view '%s'"), *ViewModelName.ToString(), *GetFullName());
				return false;
			}
			else if (!Sources.IsValidIndex(AllCreatedSourcesIndex) || Sources[AllCreatedSourcesIndex].SourceName != ViewModelName)
			{
				UE::MVVM::FMessageLog Log(GetUserWidget());
				Log.Error(LOCTEXT("SetViewModelInvalidCreatedSource", "An internal error occurs. The new viewmodel is not found."));
				UE_LOG(LogMVVM, Error, TEXT("The viewmodel name '%s' is invalid for the view '%s'"), *ViewModelName.ToString(), *GetFullName());
				return false;
			}
		}

		FMVVMViewSource& ViewSource = Sources[AllCreatedSourcesIndex];
		UObject* PreviousValue = Sources[AllCreatedSourcesIndex].Source;
		if (PreviousValue != NewValue.GetObject())
		{
			const TArrayView<const FMVVMViewClass_CompiledBinding> CompiledBindings = ClassExtension->GetCompiledBindings();

			FMemMark Mark(FMemStack::Get());
			TArray<int32, TMemStackAllocator<>> BindingToReenable;
			BindingToReenable.Reserve(ViewSource.RegisteredCout);

			// Unregister any bindings from that source
			for (int32 Index = 0; Index < CompiledBindings.Num(); ++Index)
			{
				const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
				if (Binding.GetSourceName() == ViewModelName && IsLibraryBindingEnabled(Index))
				{
					DisableLibraryBinding(Binding, Index);
					BindingToReenable.Add(Index);
				}
			}

			ViewSource.Source = NewValue.GetObject();
			if (ClassExtension->GetViewModelCreators()[AllCreatedSourcesIndex].IsSourceAUserWidgetProperty())
			{
				FObjectPropertyBase* FoundObjectProperty = FindFProperty<FObjectPropertyBase>(GetUserWidget()->GetClass(), ViewModelName);
				if (ensure(FoundObjectProperty))
				{
					FoundObjectProperty->SetObjectPropertyValue_InContainer(GetUserWidget(), NewValue.GetObject());
				}
			}

			bool bPreviousEveryTickBinding = bHasEveryTickBinding;
			bHasEveryTickBinding = false;
			if (NewValue.GetObject())
			{
				// Register back any binding that was previously enabled
				if (BindingToReenable.Num() > 0)
				{
					for (int32 Index : BindingToReenable)
					{
						const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
						EnableLibraryBinding(Binding, Index);
					}
				}
				else if (bConstructed)
				{
					// Enabled the default bindings
					for (int32 Index = 0; Index < CompiledBindings.Num(); ++Index)
					{
						const FMVVMViewClass_CompiledBinding& Binding = CompiledBindings[Index];
						if (Binding.IsEnabledByDefault() && Binding.GetSourceName() == ViewModelName)
						{
							EnableLibraryBinding(Binding, Index);
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
	return false;
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

				ExecuteLibraryBinding(Binding, InViewModelOrWidget);

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

					ExecuteLibraryBinding(Binding);

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
				ExecuteLibraryBinding(Binding);
			}
		}
	}
}


void UMVVMView::ExecuteLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding) const
{
	check(ClassExtension);
	check(GetUserWidget());

	FMVVMCompiledBindingLibrary::EConversionFunctionType FunctionType = Binding.IsConversionFunctionComplex() ? FMVVMCompiledBindingLibrary::EConversionFunctionType::Complex : FMVVMCompiledBindingLibrary::EConversionFunctionType::Simple;
	TValueOrError<void, FMVVMCompiledBindingLibrary::EExecutionFailingReason> ExecutionResult = ClassExtension->GetBindingLibrary().Execute(GetUserWidget(), Binding.GetBinding(), FunctionType);

#if UE_WITH_MVVM_DEBUGGING
	if (ExecutionResult.HasError())
	{
		UE::MVVM::FDebugging::BroadcastLibraryBindingExecuted(this, Binding, ExecutionResult.GetError());
	}
	else
	{
		UE::MVVM::FDebugging::BroadcastLibraryBindingExecuted(this, Binding);
	}
#endif

	if (ExecutionResult.HasError())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(FText::Format(LOCTEXT("ExecuteBindingGenericExecute", "The binding '{0}' was not executed. {1}."), FText::FromString(Binding.ToString()), LOCTEXT("todo", "Reason is todo.")));
	}
	else if (bLogBinding)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("ExecuteLibraryBinding", "Execute binding '{0}'."), FText::FromString(Binding.ToString())));
	}
}


void UMVVMView::ExecuteLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, UObject* Source) const
{
	check(ClassExtension);
	check(GetUserWidget());
	check(Source);

	TValueOrError<void, FMVVMCompiledBindingLibrary::EExecutionFailingReason> ExecutionResult = Binding.IsConversionFunctionComplex()
		? ClassExtension->GetBindingLibrary().Execute(GetUserWidget(), Binding.GetBinding(), FMVVMCompiledBindingLibrary::EConversionFunctionType::Complex)
		: ClassExtension->GetBindingLibrary().ExecuteWithSource(GetUserWidget(), Binding.GetBinding(), Source);

#if UE_WITH_MVVM_DEBUGGING
	if (ExecutionResult.HasError())
	{
		UE::MVVM::FDebugging::BroadcastLibraryBindingExecuted(this, Binding, ExecutionResult.GetError());
	}
	else
	{
		UE::MVVM::FDebugging::BroadcastLibraryBindingExecuted(this, Binding);
	}
#endif
	if (ExecutionResult.HasError())
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Error(FText::Format(LOCTEXT("ExecuteBindingGenericExecute", "The binding '{0}' was not executed. {1}."), FText::FromString(Binding.ToString()), LOCTEXT("todo", "Reason is todo.")));
	}
	else if (bLogBinding)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("ExecuteLibraryBinding", "Execute binding '{0}'."), FText::FromString(Binding.ToString())));
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
	return EnabledLibraryBindings.IsValidIndex(InBindindIndex) && EnabledLibraryBindings[InBindindIndex];
}


void UMVVMView::EnableLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	EnabledLibraryBindings.PadToNum(BindingIndex + 1, false);
	check(EnabledLibraryBindings[BindingIndex] == false);

	EMVVMExecutionMode ExecutionMode = Binding.GetExecuteMode();
	bool bRegistered = true;
	if (!Binding.IsOneTime() && ExecutionMode != EMVVMExecutionMode::Tick)
	{
		bRegistered = RegisterLibraryBinding(Binding, BindingIndex);
	}

	if (bRegistered && bLogBinding)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("EnableLibraryBinding", "Enable binding '{0}'."), FText::FromString(Binding.ToString())));
	}

	EnabledLibraryBindings[BindingIndex] = bRegistered;

	if (bRegistered && Binding.NeedsExecutionAtInitialization())
	{
		ExecuteLibraryBinding(Binding);
	}

	bHasEveryTickBinding = bHasEveryTickBinding || ExecutionMode == EMVVMExecutionMode::Tick;
}


void UMVVMView::DisableLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	check(IsLibraryBindingEnabled(BindingIndex));


	EMVVMExecutionMode ExecutionMode = Binding.GetExecuteMode();
	EnabledLibraryBindings[BindingIndex] = false;
	if (!Binding.IsOneTime() && ExecutionMode != EMVVMExecutionMode::Tick)
	{
		UnregisterLibraryBinding(Binding);
	}

	if (bLogBinding)
	{
		UE::MVVM::FMessageLog Log(GetUserWidget());
		Log.Info(FText::Format(LOCTEXT("DisableLibraryBinding", "Disable binding '{0}'."), FText::FromString(Binding.ToString())));
	}
}


bool UMVVMView::RegisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding, int32 BindingIndex)
{
	check(ClassExtension);

	TValueOrError<UE::FieldNotification::FFieldId, void> FieldIdResult = ClassExtension->GetBindingLibrary().GetFieldId(Binding.GetSourceFieldId());
	if (FieldIdResult.HasError())
	{
#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_InvalidFieldId);
#endif
		UE_LOG(LogMVVM, Error, TEXT("'%s' can't register binding '%s'. The FieldId is invalid."), *GetFullName(), *Binding.ToString());
		return false;
	}

	UE::FieldNotification::FFieldId FieldId = FieldIdResult.StealValue();
	if (!FieldId.IsValid())
	{
#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_FieldIdNotFound);
#endif
		UE_LOG(LogMVVM, Error, TEXT("'%s' can't register binding '%s'. The FieldId was not found on the source."), *GetFullName(), *Binding.ToString());
		return false;
	}

	// The source may not have been created because the property path was wrong.
	TScriptInterface<INotifyFieldValueChanged> Source = FindSource(Binding, true);
	if (Source.GetInterface() == nullptr)
	{
#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_InvalidSource);
#endif
		if (!Binding.IsRegistrationOptional())
		{
			UE_LOG(LogMVVM, Error, TEXT("'%s' can't register binding '%s'. The source is invalid."), *GetFullName(), *Binding.ToString());
		}
		return false;
	}

	// Only bind if the source and the destination are valid.
	if (ClassExtension->GetBindingLibrary().EvaluateFieldPath(GetUserWidget(), Binding.GetBinding().GetSourceFieldPath()).HasError())
	{
#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_InvalidSourceField);
#endif
		UE_LOG(LogMVVM, Warning, TEXT("'%s' can't register binding '%s'. The destination was not evaluated."), *GetFullName(), *Binding.ToString());
		return false;
	}
	if (ClassExtension->GetBindingLibrary().EvaluateFieldPath(GetUserWidget(), Binding.GetBinding().GetDestinationFieldPath()).HasError())
	{
#if UE_WITH_MVVM_DEBUGGING
		UE::MVVM::FDebugging::BroadcastLibraryBindingRegistered(this, Binding, UE::MVVM::FDebugging::ERegisterLibraryBindingResult::Failed_InvalidDestinationField);
#endif
		UE_LOG(LogMVVM, Warning, TEXT("'%s' can't register binding '%s'. The destination was not evaluated."), *GetFullName(), *Binding.ToString());
		return false;
	}


	UE::FieldNotification::FFieldMulticastDelegate::FDelegate Delegate = UE::FieldNotification::FFieldMulticastDelegate::FDelegate::CreateUObject(this, &UMVVMView::HandledLibraryBindingValueChanged, BindingIndex);
	if (Source->AddFieldValueChangedDelegate(FieldId, MoveTemp(Delegate)).IsValid())
	{
		if (FMVVMViewSource* RegisteredSource = FindViewSource(Binding.GetSourceName()))
		{
			++RegisteredSource->RegisteredCout;
		}
		else
		{
			FMVVMViewSource& NewSource = Sources.AddDefaulted_GetRef();
			NewSource.Source = Source.GetObject();
			NewSource.SourceName = Binding.GetSourceName();
			NewSource.RegisteredCout = 1;
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


void UMVVMView::UnregisterLibraryBinding(const FMVVMViewClass_CompiledBinding& Binding)
{
	TScriptInterface<INotifyFieldValueChanged> Source = FindSource(Binding, true);
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
			ensureMsgf(RegisteredSource->RegisteredCout > 0, TEXT("The count should match the number of RegisterLibraryBinding and UnregisterLibraryBinding"));
			--RegisteredSource->RegisteredCout;
		}
	}
}


TScriptInterface<INotifyFieldValueChanged> UMVVMView::FindSource(const FMVVMViewClass_CompiledBinding& Binding, bool bAllowNull) const
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
				UE_LOG(LogMVVM, Error, TEXT("'%s' could not evaluate the source for binding '%s'. The property name is invalid."), *GetFullName(), *Binding.ToString());
				return TScriptInterface<INotifyFieldValueChanged>();
			}

			Source = SourceObjectProperty->GetObjectPropertyValue_InContainer(UserWidget);
		}

		if (Source == nullptr)
		{
			if (!bAllowNull)
			{
				UE_LOG(LogMVVM, Error, TEXT("'%s' could not evaluate the source for binding '%s'. The source is an invalid object."), *GetFullName(), *Binding.ToString());
			}
			return TScriptInterface<INotifyFieldValueChanged>();
		}

		if (!Source->Implements<UNotifyFieldValueChanged>())
		{
			UE_LOG(LogMVVM, Error, TEXT("'%s' could not evaluate the source for binding '%s'. The object '%s' doesn't implements INotifyFieldValueChanged."), *GetFullName(), *Binding.ToString(), *Source->GetFName().ToString());
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

