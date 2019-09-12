// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractiveTool.h"
#include "ToolIndicatorSet.generated.h"


// UInterface for IInputBehavior
UINTERFACE(MinimalAPI)
class UToolIndicator : public UInterface
{
	GENERATED_BODY()
};


/**
 *
 */
class MODELINGCOMPONENTS_API IToolIndicator
{
	GENERATED_BODY()

public:

	virtual void Connect(UInteractiveTool* Tool) = 0;
	virtual void Disconnect() = 0;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) = 0;
	virtual void Tick(float DeltaTime) = 0;
};


/**
 *
 */
UCLASS(Transient)
class MODELINGCOMPONENTS_API UToolIndicatorSet : public UObject
{
	GENERATED_BODY()

public:
	UToolIndicatorSet();
	virtual ~UToolIndicatorSet();

	virtual void Connect(UInteractiveTool* Tool);
	virtual void Disconnect();

	virtual void AddIndicator(IToolIndicator* Indicator);

	virtual void Render(IToolsContextRenderAPI* RenderAPI);
	virtual void Tick(float DeltaTime);

protected:
	UInteractiveTool* Owner;

	UPROPERTY()
	TSet<UObject*> Indicators;		// these are IToolIndicator* but UPROPERTY requires storing as UObject
};