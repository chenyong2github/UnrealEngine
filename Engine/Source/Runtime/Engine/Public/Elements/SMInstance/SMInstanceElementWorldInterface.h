// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "SMInstanceElementWorldInterface.generated.h"

struct FSMInstanceId;

UCLASS()
class ENGINE_API USMInstanceElementWorldInterface : public UTypedElementWorldInterface
{
	GENERATED_BODY()

public:
	virtual bool CanEditElement(const FTypedElementHandle& InElementHandle) override;
	virtual bool IsTemplateElement(const FTypedElementHandle& InElementHandle) override;
	virtual ULevel* GetOwnerLevel(const FTypedElementHandle& InElementHandle) override;
	virtual UWorld* GetOwnerWorld(const FTypedElementHandle& InElementHandle) override;
	virtual bool GetBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds) override;
	virtual bool GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform) override;
	virtual bool SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform) override;
	virtual bool GetRelativeTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform) override;
	virtual bool SetRelativeTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform) override;

	static bool CanEditSMInstance(const FSMInstanceId& InSMInstanceId);
};
