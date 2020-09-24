// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMStruct.generated.h"

/**
 * The base class for all RigVM enabled structs.
 */
USTRUCT()
struct RIGVM_API FRigVMStruct
{
	GENERATED_BODY()

	virtual ~FRigVMStruct() {}
	virtual FString ProcessPinLabelForInjection(const FString& InLabel) const { return InLabel; }
	virtual FName GetEventName() const { return NAME_None; }

public:

	FORCEINLINE virtual int32 GetArraySize(const FName& InParameterName, const FRigVMUserDataArray& RigVMUserData) { return INDEX_NONE; }

	// loop realted
	FORCEINLINE virtual bool IsForLoop() const { return false; }
	FORCEINLINE virtual int32 GetNumSlices() const { return 1; }

#if WITH_EDITOR
	static bool ValidateStruct(UScriptStruct* InStruct, FString* OutErrorMessage);
	static bool CheckPinType(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType, FString* OutErrorMessage = nullptr);
	static bool CheckPinDirection(UScriptStruct* InStruct, const FName& PinName, const FName& InDirectionMetaName);
	static ERigVMPinDirection GetPinDirectionFromProperty(FProperty* InProperty);
	static bool CheckPinExists(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType = FString(), FString* OutErrorMessage = nullptr);
	static bool CheckMetadata(UScriptStruct* InStruct, const FName& PinName, const FName& InMetadataKey, FString* OutErrorMessage = nullptr);
	static bool CheckFunctionExists(UScriptStruct* InStruct, const FName& FunctionName, FString* OutErrorMessage = nullptr);
	static FString ExportToFullyQualifiedText(FProperty* InMemberProperty, const uint8* InMemberMemoryPtr);
	static FString ExportToFullyQualifiedText(UScriptStruct* InStruct, const uint8* InStructMemoryPtr);
#endif

	static const FName DeprecatedMetaName;
	static const FName InputMetaName;
	static const FName OutputMetaName;
	static const FName IOMetaName;
	static const FName HiddenMetaName;
	static const FName VisibleMetaName;
	static const FName DetailsOnlyMetaName;
	static const FName AbstractMetaName;
	static const FName CategoryMetaName;
	static const FName DisplayNameMetaName;
	static const FName MenuDescSuffixMetaName;
	static const FName ShowVariableNameInTitleMetaName;
	static const FName CustomWidgetMetaName;
	static const FName ConstantMetaName;
	static const FName TitleColorMetaName;
	static const FName NodeColorMetaName;
	static const FName KeywordsMetaName;
	static const FName PrototypeNameMetaName;
	static const FName ExpandPinByDefaultMetaName;
	static const FName DefaultArraySizeMetaName;
	static const FName VaryingMetaName;
	static const FName SingletonMetaName;
	static const FName SliceContextMetaName;
	static const FName ExecuteName;
	static const FName ExecuteContextName;
	static const FName ForLoopCountPinName;
	static const FName ForLoopContinuePinName;
	static const FName ForLoopCompletedPinName;
	static const FName ForLoopIndexPinName;

protected:

	static float GetRatioFromIndex(int32 InIndex, int32 InCount);

};
