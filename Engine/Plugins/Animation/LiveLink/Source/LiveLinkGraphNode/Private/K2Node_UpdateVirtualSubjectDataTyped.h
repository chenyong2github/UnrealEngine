// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "K2Node_UpdateVirtualSubjectDataBase.h"
#include "K2Node_UpdateVirtualSubjectDataTyped.generated.h"

UCLASS()
class LIVELINKGRAPHNODE_API UK2Node_UpdateVirtualSubjectStaticData : public UK2Node_UpdateVirtualSubjectDataBase
{
	GENERATED_BODY()

public:

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

protected:

	virtual UScriptStruct* GetStructTypeFromRole(ULiveLinkRole* Role) const override;
	virtual FName GetUpdateFunctionName() const override;
	virtual FText GetStructPinName() const override;
};

UCLASS()
class LIVELINKGRAPHNODE_API UK2Node_UpdateVirtualSubjectFrameData : public UK2Node_UpdateVirtualSubjectDataBase
{
	GENERATED_BODY()

public:

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

protected:

	virtual UScriptStruct* GetStructTypeFromRole(ULiveLinkRole* Role) const override;
	virtual FName GetUpdateFunctionName() const override;
	virtual FText GetStructPinName() const override;
};