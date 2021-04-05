// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/GizmoMath.h"
#include "BaseGizmos/ParameterSourcesFloat.h"
#include "BaseGizmos/ParameterSourcesVec2.h"
#include "BaseGizmos/ParameterToTransformAdapters.h"
#include "EditorModeManager.h"
#include "EditorParameterToTransformAdapters.generated.h"

//
// Various 1D and 2D ParameterSource converters intended to be used to create 3D transformation gizmos.
// Based on base classes in ParameterSourcesFloat.h and ParameterSourcesVec2.h
// 

/**
 * UGizmoEditorAxisTranslationParameterSource is an IGizmoFloatParameterSource implementation that
 * interprets the float value as the parameter of a line equation, and maps this parameter to a 3D translation 
 * along a line with origin/direction given by an IGizmoAxisSource. This translation is applied to the mode tools 
 * manager's widget location and its super methods are called to also apply it to an IGizmoTransformSource.
 *
 * This ParameterSource is intended to be used to create Editor 3D Axis Translation Gizmos.
 */

//@todo rename this UGizmoEditorAxisParameterSource
 
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoEditorAxisTranslationParameterSource : public UGizmoBaseFloatParameterSource
{
	GENERATED_BODY()
public:

	virtual float GetParameter() const override
	{
		return AxisTranslationParameterSource->GetParameter();
	}

	virtual void SetParameter(float NewValue) override
	{
		Parameter = NewValue;
		LastChange.CurrentValue = NewValue;

		// construct translation as delta from initial position
		FVector Translation = LastChange.GetChangeDelta() * CurTranslationAxis;

		// @todo should this be handled in a parameter source instead? to properly handle world transformation
		FEditorModeTools& EditorModeTools = GLevelEditorModeTools();
		EditorModeTools.PivotLocation = LastPivotLocation + Translation;
		EditorModeTools.SnappedLocation = LastPivotLocation + Translation;

		AxisTranslationParameterSource->SetParameter(Parameter);
	}

	virtual void BeginModify()
	{
		check(AxisSource);
		LastChange = FGizmoFloatParameterChange(Parameter);
		CurTranslationAxis = AxisSource->GetDirection();
		CurTranslationOrigin = AxisSource->GetOrigin();

		FEditorModeTools& EditorModeTools = GLevelEditorModeTools();
		LastPivotLocation = EditorModeTools.PivotLocation;
		LastSnappedLocation = EditorModeTools.SnappedLocation;

		AxisTranslationParameterSource->BeginModify();
	}

	virtual void EndModify()
	{
		AxisTranslationParameterSource->EndModify();

	}

public:
	/** The Parameter line-equation value is converted to a 3D Translation along this Axis */
	UPROPERTY()
	TScriptInterface<IGizmoAxisSource> AxisSource;

	/** Wrapped parameter source */
	TObjectPtr<UGizmoAxisTranslationParameterSource> AxisTranslationParameterSource;

public:
	/** Parameter is the line-equation parameter that this FloatParameterSource provides */
	UPROPERTY()
	float Parameter = 0.0f;

	/** Active parameter change (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FGizmoFloatParameterChange LastChange;

	/** translation axis for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationAxis;

	/** translation origin for current parameter edit (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector CurTranslationOrigin;

	/** Last pivot location before translation began (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector LastPivotLocation = FVector::ZeroVector;

	/** Last snapped location before translation began (only valid between BeginModify/EndModify) */
	UPROPERTY()
	FVector LastSnappedLocation = FVector::ZeroVector;


public:
	/**
	 * Create a standard instance of this ParameterSource, with the given AxisSource and TransformSource
	 */
	static UGizmoEditorAxisTranslationParameterSource* Construct(
		IGizmoAxisSource* InAxisSource,
		IGizmoTransformSource* InTransformSource,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoEditorAxisTranslationParameterSource* NewSource = NewObject<UGizmoEditorAxisTranslationParameterSource>(Outer);
		
		UGizmoAxisTranslationParameterSource* ParameterSource = UGizmoAxisTranslationParameterSource::Construct(InAxisSource, InTransformSource, Outer);
		NewSource->AxisTranslationParameterSource = ParameterSource;
		NewSource->AxisSource = Cast<UObject>(InAxisSource);
		return NewSource;
	}
};



