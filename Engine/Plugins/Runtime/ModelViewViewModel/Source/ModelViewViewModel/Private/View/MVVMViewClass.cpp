// Copyright Epic Games, Inc. All Rights Reserved.

#include "View/MVVMViewClass.h"
#include "Types/MVVMFieldContext.h"
#include "View/MVVMView.h"
#include "View/MVVMViewModelContextResolver.h"

#include "Bindings/MVVMFieldPathHelper.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "MVVMGameSubsystem.h"
#include "MVVMMessageLog.h"
#include "MVVMSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MVVMViewClass)


#define LOCTEXT_NAMESPACE "MVVMViewClass"

///////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////

FMVVMViewClass_SourceCreator FMVVMViewClass_SourceCreator::MakeManual(FName InName, UClass* InNotifyFieldValueChangedClass)
{
	FMVVMViewClass_SourceCreator Result;
	if (ensure(InNotifyFieldValueChangedClass && InNotifyFieldValueChangedClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())))
	{
		Result.PropertyName = InName;
		Result.ExpectedSourceType = InNotifyFieldValueChangedClass;
		Result.bCreateInstance = false;
		Result.bOptional = true;
	}
	return Result;
}

FMVVMViewClass_SourceCreator FMVVMViewClass_SourceCreator::MakeInstance(FName InName, UClass* InNotifyFieldValueChangedClass)
{
	FMVVMViewClass_SourceCreator Result;
	if (ensure(InNotifyFieldValueChangedClass && InNotifyFieldValueChangedClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())))
	{
		Result.PropertyName = InName;
		Result.ExpectedSourceType = InNotifyFieldValueChangedClass;
		Result.bCreateInstance = true;
		Result.bOptional = false;
	}
	return Result;
}

FMVVMViewClass_SourceCreator FMVVMViewClass_SourceCreator::MakeFieldPath(FName InName, UClass* InNotifyFieldValueChangedClass, FMVVMVCompiledFieldPath InFieldPath, bool bOptional)
{
	FMVVMViewClass_SourceCreator Result;
	if (ensure(InNotifyFieldValueChangedClass&& InNotifyFieldValueChangedClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())))
	{
		if (ensure(InFieldPath.IsValid()))
		{
			Result.PropertyName = InName;
			Result.ExpectedSourceType = InNotifyFieldValueChangedClass;
			Result.FieldPath = InFieldPath;
			Result.bOptional = bOptional;
		}
	}
	return Result;
}

FMVVMViewClass_SourceCreator FMVVMViewClass_SourceCreator::MakeGlobalContext(FName InName, FMVVMViewModelContext InContext, bool bOptional)
{
	FMVVMViewClass_SourceCreator Result;
	if (ensure(InContext.ContextClass && InContext.ContextClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())))
	{
		Result.PropertyName = InName;
		Result.ExpectedSourceType = InContext.ContextClass;
		Result.GlobalViewModelInstance = MoveTemp(InContext);
		Result.bOptional = bOptional;
	}
	return Result;
}

FMVVMViewClass_SourceCreator FMVVMViewClass_SourceCreator::MakeResolver(FName InName, UClass* InNotifyFieldValueChangedClass, UMVVMViewModelContextResolver* InResolver, bool bOptional)
{
	FMVVMViewClass_SourceCreator Result;
	if (ensure(InResolver && InNotifyFieldValueChangedClass && InNotifyFieldValueChangedClass->ImplementsInterface(UNotifyFieldValueChanged::StaticClass())))
	{
		Result.PropertyName = InName;
		Result.ExpectedSourceType = InNotifyFieldValueChangedClass;
		Result.Resolver = InResolver;
		Result.bOptional = bOptional;
		ensure(InResolver->GetPackage() != GetTransientPackage() && !InResolver->HasAnyFlags(RF_Transient));
	}
	return Result;
}

UObject* FMVVMViewClass_SourceCreator::CreateInstance(const UMVVMViewClass* InViewClass, UMVVMView* InView, UUserWidget* InUserWidget) const
{
	check(InViewClass);
	check(InView);
	check(InUserWidget);

	UObject* Result = nullptr;

	if (bCreateInstance)
	{
		if (ExpectedSourceType.Get() != nullptr)
		{
			Result = NewObject<UObject>(InUserWidget, ExpectedSourceType.Get(), NAME_None, RF_Transient);
		}
		else if (!bOptional)
		{
			UE::MVVM::FMessageLog Log(InUserWidget);
			Log.Error(FText::Format(LOCTEXT("CreateInstanceCreateInstance", "The source '{0}' could not be created. The class is not loaded."), FText::FromName(PropertyName)));
		}
	}
	else if (Resolver)
	{
		Result = Resolver->CreateInstance(ExpectedSourceType.Get(), InUserWidget, InView);
		if (!Result && !bOptional)
		{
			UE::MVVM::FMessageLog Log(InUserWidget);
			Log.Error(FText::Format(LOCTEXT("CreateInstanceFailResolver", "The source '{0}' could not be created. Resolver returned an invalid value."), FText::FromName(PropertyName)));
		}
		if (Result && !Result->IsA(ExpectedSourceType.Get()))
		{
			Result = nullptr;

			UE::MVVM::FMessageLog Log(InUserWidget);
			Log.Error(FText::Format(LOCTEXT("CreateInstanceFailResolverExpected", "The source '{0}' could not be created. Resolver returned viewodel of an unexpected type."), FText::FromName(PropertyName)));
		}
	}
	else if (GlobalViewModelInstance.IsValid())
	{
		UMVVMViewModelCollectionObject* Collection = nullptr;
		UMVVMViewModelBase* FoundViewModelInstance = nullptr;
		if (const UWorld* World = InUserWidget->GetWorld())
		{
			if (const UGameInstance* GameInstance = World->GetGameInstance())
			{
				Collection = GameInstance->GetSubsystem<UMVVMGameSubsystem>()->GetViewModelCollection();
				if (Collection)
				{
					FoundViewModelInstance = Collection->FindViewModelInstance(GlobalViewModelInstance);
				}
			}
		}

		if (FoundViewModelInstance != nullptr)
		{
			ensureMsgf(FoundViewModelInstance->IsA(GlobalViewModelInstance.ContextClass), TEXT("The Global View Model Instance is not of the expected type."));
			Result = FoundViewModelInstance;
		}
		else if (!bOptional)
		{
			UE::MVVM::FMessageLog Log(InUserWidget);
			if (Collection)
			{
				Log.Error(FText::Format(LOCTEXT("CreateInstanceFailedGlobal", "The source '{0}' was not found in the global view model collection."), FText::FromName(GlobalViewModelInstance.ContextName)));
			}
			else
			{
				Log.Error(FText::Format(LOCTEXT("CreateInstanceFailedGlobalInstance", "The source '{0}' will be invalid because the global view model collection could not be found."), FText::FromName(GlobalViewModelInstance.ContextName)));
			}
		}
	}
	else if (FieldPath.IsValid())
	{
		TValueOrError<UE::MVVM::FFieldContext, void> FieldPathResult = InViewClass->GetBindingLibrary().EvaluateFieldPath(InUserWidget, FieldPath);
		if (FieldPathResult.HasValue())
		{
			TValueOrError<UObject*, void> ObjectResult = UE::MVVM::FieldPathHelper::EvaluateObjectProperty(FieldPathResult.GetValue());
			if (ObjectResult.HasValue() && ObjectResult.GetValue() != nullptr)
			{
				Result = ObjectResult.GetValue();
			}
		}

		if (Result == nullptr && !bOptional)
		{
			UE::MVVM::FMessageLog Log(InUserWidget);
			Log.Error(FText::Format(LOCTEXT("CreateInstanceInvalidBiding", "The source '{0}' was evaluated to be invalid at initialization."), FText::FromName(PropertyName)));
		}
	}

	return Result;
}


///////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////
namespace UE::MVVM::Private
{
	int32 GDefaultEvaluationMode = (int32)EMVVMExecutionMode::Immediate;
	static FAutoConsoleVariableRef CVarDefaultEvaluationMode(
		TEXT("MVVM.DefaultExecutionMode"),
		GDefaultEvaluationMode,
		TEXT("The default evaluation mode of a MVVM binding.")
	);
}

EMVVMExecutionMode FMVVMViewClass_CompiledBinding::GetExecuteMode() const
{
	EMVVMExecutionMode DefaultMode = (EMVVMExecutionMode)UE::MVVM::Private::GDefaultEvaluationMode;
	return (Flags & EBindingFlags::OverrideExecuteMode) == 0 ? DefaultMode : ExecutionMode;
}

FMVVMViewClass_CompiledBinding::FToStringArgs FMVVMViewClass_CompiledBinding::FToStringArgs::Short()
{
	FToStringArgs Result;
	Result.bUseDisplayName = false;
	Result.bAddFieldPath = false;
	Result.bAddFlags = false;
	return Result;
}

FMVVMViewClass_CompiledBinding::FToStringArgs FMVVMViewClass_CompiledBinding::FToStringArgs::All()
{
	return FToStringArgs();
}

FString FMVVMViewClass_CompiledBinding::ToString() const
{
#if WITH_EDITOR
	return EditorId.ToString();
#endif
}

FString FMVVMViewClass_CompiledBinding::ToString(const FMVVMCompiledBindingLibrary& BindingLibrary, FToStringArgs Args) const
{
	TStringBuilder<1024> StringBuilder;

#if WITH_EDITOR
	if (Args.bAddBindingId)
	{
		StringBuilder << TEXT("BindingId: ");
		EditorId.AppendString(StringBuilder);
		StringBuilder << TEXT("\n    ");
	}
#endif

	if (Args.bAddFieldPath)
	{
		StringBuilder << TEXT("Binding: ");
		TValueOrError<FString, FString> DestinationString = BindingLibrary.FieldPathToString(GetBinding().GetDestinationFieldPath(), Args.bUseDisplayName);
		TValueOrError<FString, FString> SourceString = BindingLibrary.FieldPathToString(GetBinding().GetSourceFieldPath(), Args.bUseDisplayName);
		TValueOrError<FString, FString> ConversionString = MakeValue(FString());
		if (GetBinding().GetConversionFunctionFieldPath().IsValid())
		{
			ConversionString = BindingLibrary.FieldPathToString(GetBinding().GetConversionFunctionFieldPath(), Args.bUseDisplayName);
		}

		if (DestinationString.HasError() || SourceString.HasError() || ConversionString.HasError())
		{
			StringBuilder << TEXT("Error: ");
		}

		if (Args.bUseDisplayName)
		{
			StringBuilder << TEXT('"');
		}
		StringBuilder << (DestinationString.HasValue() ? DestinationString.GetValue() : DestinationString.GetError());
		if (Args.bUseDisplayName)
		{
			StringBuilder << TEXT('"');
		}
		StringBuilder << TEXT(" = ");
		if (GetBinding().GetConversionFunctionFieldPath().IsValid())
		{
			if (Args.bUseDisplayName)
			{
				StringBuilder << TEXT('"');
			}
			StringBuilder << (ConversionString.HasValue() ? ConversionString.GetValue() : ConversionString.GetError());
			if (Args.bUseDisplayName)
			{
				StringBuilder << TEXT('"');
			}
			StringBuilder << TEXT(" ( ");
		}

		if (Args.bUseDisplayName)
		{
			StringBuilder << TEXT('"');
		}
		StringBuilder << (SourceString.HasValue() ? SourceString.GetValue() : SourceString.GetError());
		if (Args.bUseDisplayName)
		{
			StringBuilder << TEXT('"');
		}

		if (GetBinding().GetConversionFunctionFieldPath().IsValid())
		{
			StringBuilder << TEXT(" )");
		}

		StringBuilder << TEXT("\n    ");
	}

	StringBuilder << TEXT("PropertyName: '");
	StringBuilder << SourcePropertyName;

	if (Args.bAddFieldId)
	{
		if (GetSourceFieldId().IsValid())
		{
			TValueOrError<UE::FieldNotification::FFieldId, void> SourceFieldId = BindingLibrary.GetFieldId(GetSourceFieldId());
			StringBuilder << TEXT("', FieldId: '");
			StringBuilder << (SourceFieldId.HasValue() ? SourceFieldId.GetValue().GetName() : FName());
		}
		else
		{
			ensureMsgf(IsOneTime(), TEXT("The field Id should be valid."));
		}
	}

	if (Args.bAddFlags)
	{
		StringBuilder << TEXT("`, Mode: '");
		StringBuilder << StaticEnum<EMVVMExecutionMode>()->GetNameByValue((int64)ExecutionMode);
		StringBuilder << TEXT("`, Flags: '");

		auto AddPipe = [&StringBuilder]()
		{
			StringBuilder << TEXT('|');
		};

		if ((Flags & EBindingFlags::ForwardBinding) != 0)
		{
			StringBuilder << TEXT("Forward");
		}
		else
		{
			StringBuilder << TEXT("Backward");
		}

		if ((Flags & EBindingFlags::OneTime) != 0)
		{
			AddPipe();
			StringBuilder << TEXT("OneTime");
		}
		if ((Flags & EBindingFlags::EnabledByDefault) != 0)
		{
			AddPipe();
			StringBuilder << TEXT("EnabledByDefault");
		}
		if ((Flags & EBindingFlags::ViewModelOptional) != 0)
		{
			AddPipe();
			StringBuilder << TEXT("Optional");
		}
		if ((Flags & EBindingFlags::ConversionFunctionIsComplex) != 0)
		{
			AddPipe();
			StringBuilder << TEXT("Complex");
		}
		if ((Flags & EBindingFlags::OverrideExecuteMode) != 0)
		{
			AddPipe();
			StringBuilder << TEXT("OverrideExecutionMode");
		}
		if ((Flags & EBindingFlags::SourceObjectIsSelf) != 0)
		{
			AddPipe();
			StringBuilder << TEXT("Self");
		}
		StringBuilder << TEXT("'");
	}

	return StringBuilder.ToString();
}


///////////////////////////////////////////////////////////////////////
// 
///////////////////////////////////////////////////////////////////////

void UMVVMViewClass::Initialize(UUserWidget* UserWidget)
{
	ensure(UserWidget->GetExtension<UMVVMView>() == nullptr);
	UMVVMView* View = UserWidget->AddExtension<UMVVMView>();
	if (ensure(View))
	{
		if (!bLoaded)
		{
			BindingLibrary.Load();
			bLoaded = true;
		}

		View->ConstructView(this);
	}
}

#undef LOCTEXT_NAMESPACE

