// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Elements/SMInstance/SMInstanceManager.h"
#include "SMInstanceElementDetailsProxyObject.generated.h"

UCLASS(Transient)
class USMInstanceElementDetailsProxyObject : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(const FSMInstanceElementId& InSMInstanceElementId);
	void Shutdown();

	//~ UObject interface
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	UPROPERTY(EditAnywhere, Category="Transform", meta=(ShowOnlyInnerProperties))
	FTransform Transform;

private:
	void SyncProxyStateFromInstance();

	FSMInstanceManager GetSMInstance() const;

	TWeakObjectPtr<UInstancedStaticMeshComponent> ISMComponent;
	uint64 ISMInstanceId = 0;

	FDelegateHandle TickHandle;
	bool bIsWithinInteractiveTransformEdit = false;
};
