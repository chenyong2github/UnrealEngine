// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigModel.h"
#include "UObject/PropertyPortFlags.h"
#include "ControlRigController.generated.h"

/**
 * The ControlRigController is the sole authority to perform changes
 * in a ControlRigModel.
 */
UCLASS()
class CONTROLRIGDEVELOPER_API UControlRigController : public UObject
{
	GENERATED_BODY()

public:

	UControlRigController();
	virtual ~UControlRigController();

	UPROPERTY()
	UControlRigModel* Model;

	void SetModel(UControlRigModel* InModel);
	virtual bool Clear();
	static bool ConstructPreviewParameter(const FName& InDataType, EControlRigModelParameterType InParameterType, FControlRigModelNode& OutNode);
	static bool ConstructPreviewNode(const FName& InFunctionName, FControlRigModelNode& OutNode);
	virtual bool AddParameter(const FName& InName, const FName& InDataType, EControlRigModelParameterType InParameterType = EControlRigModelParameterType::Input, const FVector2D& InPosition = FVector2D::ZeroVector, bool bUndo = true);
	virtual bool AddComment(const FName& InName, const FString& InText, const FVector2D& InPosition, const FVector2D& InSize, const FLinearColor& InColor = FLinearColor::White, bool bUndo = true);
	virtual bool AddNode(const FName& InFunctionName, const FVector2D& InPosition = FVector2D::ZeroVector, const FName& InName = NAME_None, bool bUndo = true);
	virtual bool RemoveNode(const FName& InName, bool bUndo = true);
	virtual bool SetNodePosition(const FName& InName, const FVector2D& InPosition, bool bUndo = true);
	virtual bool SetNodeSize(const FName& InName, const FVector2D& InSize, bool bUndo = true);
	virtual bool SetNodeColor(const FName& InName, const FLinearColor& InColor, bool bUndo = true);
	virtual bool SetParameterType(const FName& InName, EControlRigModelParameterType InParameterType, bool bUndo = true);
	virtual bool SetCommentText(const FName& InName, const FString& InText, bool bUndo = true);
	virtual bool RenameNode(const FName& InOldNodeName, const FName& InNewNodeName, bool bUndo = true);
	virtual bool ClearSelection();
	virtual bool SetSelection(const TArray<FName>& InNodeSelection);
	virtual bool SelectNode(const FName& InName, bool bInSelected = true, bool bClearSelection = true);
	bool DeselectNode(const FName& InName);
	bool PrepareCycleCheckingForPin(const FName& InNodeName, const FName& InPinName, bool bIsInput);
	bool ResetCycleCheck();
	bool CanLink(const FName& InSourceNodeName, const FName& InSourceOutputPinName, const FName& InTargetNodeName, const FName& InTargetInputPinName, FString* OutFailureReason, bool bReportError = false) const;
	bool MakeLink(const FName& InSourceNodeName, const FName& InSourceOutputPinName, const FName& InTargetNodeName, const FName& InTargetInputPinName, FString* OutFailureReason = nullptr, bool bUndo = true);
	bool BreakLink(const FName& InSourceNodeName, const FName& InSourceOutputPinName, const FName& InTargetNodeName, const FName& InTargetInputPinName, bool bUndo = true);
	bool BreakLinks(const FName& InNodeName, const FName& InPinName, bool bIsInput, bool bUndo = true);

	virtual bool GetPinDefaultValue(const FName& InNodeName, const FName& InPinName, FString& OutDefaultValue) const;
	virtual bool SetPinDefaultValue(const FName& InNodeName, const FName& InPinName, const FString& InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueBool(const FName& InNodeName, const FName& InPinName, bool& OutDefaultValue) const;
	virtual bool SetPinDefaultValueBool(const FName& InNodeName, const FName& InPinName, bool InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueFloat(const FName& InNodeName, const FName& InPinName, float& OutDefaultValue) const;
	virtual bool SetPinDefaultValueFloat(const FName& InNodeName, const FName& InPinName, float InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueInt(const FName& InNodeName, const FName& InPinName, int32& OutDefaultValue) const;
	virtual bool SetPinDefaultValueInt(const FName& InNodeName, const FName& InPinName, int32 InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueName(const FName& InNodeName, const FName& InPinName, FName& OutDefaultValue) const;
	virtual bool SetPinDefaultValueName(const FName& InNodeName, const FName& InPinName, const FName& InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueVector(const FName& InNodeName, const FName& InPinName, FVector& OutDefaultValue) const;
	virtual bool SetPinDefaultValueVector(const FName& InNodeName, const FName& InPinName, const FVector& InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueQuat(const FName& InNodeName, const FName& InPinName, FQuat& OutDefaultValue) const;
	virtual bool SetPinDefaultValueQuat(const FName& InNodeName, const FName& InPinName, const FQuat& InDefaultValue, bool bLog = true, bool bUndo = true);
	virtual bool GetPinDefaultValueTransform(const FName& InNodeName, const FName& InPinName, FTransform& OutDefaultValue) const;
	virtual bool SetPinDefaultValueTransform(const FName& InNodeName, const FName& InPinName, const FTransform& InDefaultValue, bool bLog = true, bool bUndo = true);
	template<typename T>
	bool GetPinDefaultValueStruct(const FName& InNodeName, const FName& InPinName, T& OutDefaultValue) const;
	template<typename T>
	bool SetPinDefaultValueStruct(const FName& InNodeName, const FName& InPinName, const T& InDefaultValue, bool bLog = true, bool bUndo = true);

	virtual	bool AddArrayPin(const FName& InNodeName, const FName& InPinName, const FString& InDefaultValue = FString(), bool bUndo = true);
	virtual	bool PopArrayPin(const FName& InNodeName, const FName& InPinName, bool bUndo = true);
	virtual	bool ClearArrayPin(const FName& InNodeName, const FName& InPinNam, bool bUndo = true);
	virtual	bool SetArrayPinSize(const FName& InNodeName, const FName& InPinName, int32 InSize, const FString& InDefaultValue = FString(), bool bUndo = true);
	virtual bool ExpandPin(const FName& InNodeName, const FName& InPinName, bool bIsInput, bool bExpanded = true, bool bUndo = true);

	virtual bool Undo();
	virtual bool Redo();

	virtual bool ResendAllNotifications();
	virtual bool ResendAllPinDefaultNotifications();
	virtual void EnableMessageLog(bool bInEnabled = true);

#if CONTROLRIG_UNDO
	virtual bool OpenUndoBracket(const FString& Title);
	virtual bool CloseUndoBracket();
	virtual bool CancelUndoBracket();
#endif

	UControlRigModel::FModifiedEvent& OnModified();

private:

	bool GetPinDefaultValueRecursiveStruct(const FControlRigModelPin* InPin, FString& OutValue) const;
	bool SetPinDefaultValueRecursiveStruct(const FControlRigModelPin* OutPin, const FString& InValue, bool bUndo);

	bool bSuspendLog;
	void LogMessage(const FString& Message) const;
	void LogWarning(const FString& Message) const;
	void LogError(const FString& Message) const;

	static bool FindPinTypeFromDataType(const FName& InDataType, FEdGraphPinType& OutPinType);
	bool EnsureModel() const;
	void HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload);

	EControlRigModelNotifType _LastModelNotification;
	FDelegateHandle _ModelModifiedHandle;
	UControlRigModel::FModifiedEvent _ModifiedEvent;

#if CONTROLRIG_UNDO
	TArray<TSharedPtr<UControlRigModel::FAction>> _UndoBrackets;
#endif
};

template<typename T>
inline bool UControlRigController::GetPinDefaultValueStruct(const FName& InNodeName, const FName& InPinName, T& OutDefaultValue) const
{
	if (!EnsureModel())
	{
		return false;
	}
	const FControlRigModelPin* Pin = Model->FindPin(InNodeName, InPinName);
	if (Pin == nullptr)
	{
		return false;
	}
	if (UScriptStruct* Struct = Cast<UScriptStruct>(Pin->Type.PinSubCategoryObject))
	{
		FString DefaultValueStr;
		if (!GetPinDefaultValueRecursiveStruct(Pin, DefaultValueStr))
		{
			return false;
		}
		Struct->ImportText(*DefaultValueStr, &OutDefaultValue, nullptr, EPropertyPortFlags::PPF_None, nullptr, Struct->GetName(), true);
		return true;
	}
	return false;
}

template<typename T>
inline bool UControlRigController::SetPinDefaultValueStruct(const FName& InNodeName, const FName& InPinName, const T& InDefaultValue, bool bLog, bool bUndo)
{
	if (!EnsureModel())
	{
		return false;
	}
	const FControlRigModelPin* Pin = Model->FindPin(InNodeName, InPinName);
	if (Pin == nullptr)
	{
		return false;
	}

	if (UScriptStruct* Struct = Cast<UScriptStruct>(Pin->Type.PinSubCategoryObject))
	{
#if CONTROLRIG_UNDO
		UControlRigModel::FAction Action;
		if (bUndo)
		{
			Action.Type = EControlRigModelNotifType::Invalid;
			Action.Title = TEXT("Set Pin Default from Struct");
			Model->CurrentActions.Push(&Action);
		}
#endif

		FString DefaultValueStr;
		Struct->ExportText(DefaultValueStr, &InDefaultValue, nullptr, nullptr, PPF_None, nullptr);
		bool bSucceeded = SetPinDefaultValueRecursiveStruct(Pin, DefaultValueStr, bUndo);

#if CONTROLRIG_UNDO
		if (bUndo)
		{
			Model->CurrentActions.Pop();
			if (bSucceeded)
			{
				Model->PushAction(Action);
			}
		}
#endif

		return bSucceeded;
	}
	return false;
}
