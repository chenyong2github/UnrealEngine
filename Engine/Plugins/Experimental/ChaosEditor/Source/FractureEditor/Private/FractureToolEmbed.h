// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolEmbed.generated.h"


UCLASS()
class UFractureToolAddEmbeddedGeometry : public UFractureActionTool
{
public:
	GENERATED_BODY()

		UFractureToolAddEmbeddedGeometry(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
	virtual bool CanExecute() const override;

private:
	static TArray<UStaticMeshComponent*>  GetSelectedStaticMeshComponents();
};


UCLASS()
class UFractureToolAutoEmbedGeometry : public UFractureActionTool
{
public:
	GENERATED_BODY()

	UFractureToolAutoEmbedGeometry(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	// UFractureActionTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;
	virtual bool CanExecute() const override;

private:
	static TArray<UStaticMeshComponent*>  GetSelectedStaticMeshComponents();
};
