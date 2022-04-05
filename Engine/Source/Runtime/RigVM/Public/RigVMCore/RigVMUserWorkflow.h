// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "RigVMCore/RigVMTraits.h"
#include "RigVMUserWorkflow.generated.h"

class URigVMUserWorkflowOptions;

DECLARE_DELEGATE_ThreeParams(FRigVMReportDelegate, EMessageSeverity::Type, UObject*, const FString&);

// Types of actions within a workflow
UENUM(BlueprintType)
enum class ERigVMUserWorkflowActionType : uint8
{
	Invalid = 0 UMETA(Hidden),
	SetPinDefaultValue = 1
};

USTRUCT(BlueprintType)
struct RIGVM_API FRigVMUserWorkflowAction
{
	GENERATED_BODY()

public:

	FORCEINLINE FRigVMUserWorkflowAction()
    : Type(ERigVMUserWorkflowActionType::Invalid)
    , Subject()
    , Data()
    {}

	FORCEINLINE FRigVMUserWorkflowAction(
		ERigVMUserWorkflowActionType InType,
		UObject* InSubject,
		const FString& InData)
	: Type(InType)
	, Subject(InSubject)
	, Data(InData)
	{}

	FORCEINLINE bool IsValid() const { return Type != ERigVMUserWorkflowActionType::Invalid && Subject != nullptr; }
	FORCEINLINE ERigVMUserWorkflowActionType GetType() const { return Type; }
	FORCEINLINE UObject* GetSubject() const { return Subject; }
	FORCEINLINE const FString& GetData() const { return Data; }
	
	template<typename T>
	FORCEINLINE T* GetSubject() const { return Cast<T>(Subject.Get()); }

protected:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Action, meta = (AllowPrivateAccess = true))
	ERigVMUserWorkflowActionType Type;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Action, meta = (AllowPrivateAccess = true))
	TObjectPtr<UObject> Subject;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Action, meta = (AllowPrivateAccess = true))
	FString Data;
};

// Types of workflows offered by a rigvm struct node
UENUM(BlueprintType)
enum class ERigVMUserWorkflowType : uint8
{
	Invalid = 0 UMETA(Hidden),
	NodeContext = 0x001,
	PinContext = 0x002,
	OnPinDefaultChanged  = 0x004,
	All = NodeContext | PinContext | OnPinDefaultChanged
};

DECLARE_DELEGATE_RetVal_OneParam(TArray<FRigVMUserWorkflowAction>, FRigVMWorkflowGetActionsDelegate, const URigVMUserWorkflowOptions*);
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(TArray<FRigVMUserWorkflowAction>, FRigVMWorkflowGetActionsDynamicDelegate, const URigVMUserWorkflowOptions*, InOptions);

USTRUCT(BlueprintType)
struct RIGVM_API FRigVMUserWorkflow
{
	GENERATED_BODY()

public:

	FORCEINLINE FRigVMUserWorkflow()
	: Title()
	, Tooltip()
	, Type(ERigVMUserWorkflowType::Invalid)
	, OnGetActionsDelegate()
	, OnGetActionsDynamicDelegate()
	, OptionsClass(nullptr)
	{}

	FORCEINLINE FRigVMUserWorkflow(
		const FString& InTitle,
		const FString& InTooltip,
		ERigVMUserWorkflowType InType,
		FRigVMWorkflowGetActionsDelegate InGetActionsDelegate,
		UClass* InOptionsClass)
	: Title(InTitle)
	, Tooltip(InTooltip)
	, Type(InType)
	, OnGetActionsDelegate(InGetActionsDelegate)
	, OnGetActionsDynamicDelegate()
	, OptionsClass(InOptionsClass)
	{}
	
	FORCEINLINE virtual ~FRigVMUserWorkflow() {}

	FORCEINLINE bool IsValid() const
	{
		return Type != ERigVMUserWorkflowType::Invalid &&
			GetOptionsClass() != nullptr &&
			(OnGetActionsDelegate.IsBound() || OnGetActionsDynamicDelegate.IsBound());
	}

	FORCEINLINE const FString& GetTitle() const { return Title; }
	FORCEINLINE const FString& GetTooltip() const { return Tooltip; }
	FORCEINLINE ERigVMUserWorkflowType GetType() const { return Type; }
	FORCEINLINE UClass* GetOptionsClass() const { return OptionsClass; }

	TArray<FRigVMUserWorkflowAction> GetActions(const URigVMUserWorkflowOptions* InOptions) const;

protected:

	bool ValidateOptions(const URigVMUserWorkflowOptions* InOptions) const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Workflow, meta = (AllowPrivateAccess = true))
	FString Title;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Workflow, meta = (AllowPrivateAccess = true))
	FString Tooltip;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Workflow, meta = (AllowPrivateAccess = true))
	ERigVMUserWorkflowType Type;

	FRigVMWorkflowGetActionsDelegate OnGetActionsDelegate;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Workflow, meta = (ScriptName = "OnGetActions", AllowPrivateAccess = true))
	FRigVMWorkflowGetActionsDynamicDelegate OnGetActionsDynamicDelegate;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Workflow, meta = (AllowPrivateAccess = true))
	TObjectPtr<UClass> OptionsClass;
};

UCLASS(BlueprintType)
class RIGVM_API URigVMUserWorkflowOptions : public UObject
{
	GENERATED_BODY()

public:

	FORCEINLINE bool IsValid() const { return Subject != nullptr; }
	bool RequiresDialog() const;
	FORCEINLINE UObject* GetSubject() const { return Subject.Get(); }

	template<typename T>
	FORCEINLINE T* GetSubject() const { return Cast<T>(Subject.Get()); }

	FORCEINLINE UObject* GetSubjectChecked() const
	{
		UObject* Object = Subject.Get();
		check(Object);
		return Object;
	}

	template<typename T>
	FORCEINLINE T* GetSubjectChecked() const { return CastChecked<T>(Subject.Get()); }

	const FRigVMUserWorkflow& GetWorkflow() const { return Workflow; }

	void Report(EMessageSeverity::Type InSeverity, const FString& InMessage) const;

	template <typename FmtType, typename... Types>
	void Reportf(EMessageSeverity::Type InSeverity, const FmtType& Fmt, Types... Args) const
	{
		Report(InSeverity, FString::Printf(Fmt, Args...));
	}

protected:

	virtual bool RequiresDialog(const FProperty* InProperty) const;

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = Options, meta = (AllowPrivateAccess = true))
	TObjectPtr<UObject> Subject;

	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly, Category = Options, meta = (AllowPrivateAccess = true))
	FRigVMUserWorkflow Workflow;

	FRigVMReportDelegate ReportDelegate;

	friend class URigVMController;
};
