// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "EditorAxisSources.generated.h"

/**
 * UGizmoEditorAxisSource is an IGizmoAxisSource implementation that provides one of the
 * X/Y/Z axes of the Level Editor local coordinate system, mapped to World, based on an integer AxisIndex in range [0,2].
 * The Axis Origin is the Level Editor's pivot location. 
 */

UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoEditorAxisSource : public UObject, public IGizmoAxisSource
{
	GENERATED_BODY()
public:
	virtual FVector GetOrigin() const;

	virtual FVector GetDirection() const;

	/** Clamped internally to 0,1,2 */
	UPROPERTY()
	int AxisIndex = 2;

	/** If false, returns World axes */
	UPROPERTY()
	bool bLocalAxes = true;

public:
	static UGizmoEditorAxisSource* Construct(
		int LocalAxisIndex,
		bool bUseLocalAxes,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoEditorAxisSource* NewSource = NewObject<UGizmoEditorAxisSource>(Outer);
		NewSource->AxisIndex = LocalAxisIndex;
		NewSource->bLocalAxes = bUseLocalAxes;
		return NewSource;
	}
};