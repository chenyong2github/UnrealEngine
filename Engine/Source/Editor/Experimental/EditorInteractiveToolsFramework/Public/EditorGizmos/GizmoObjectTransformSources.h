// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/TransformProxy.h"
#include "BaseGizmos/TransformSources.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "GizmoObjectTransformSources.generated.h"


/**
 * UGizmoObjectWorldTransformSource implements IGizmoTransformSource (via UGizmoBaseTransformSource)
 * based on the internal transform of a UGizmoBaseObject;
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoObjectWorldTransformSource : public UGizmoBaseTransformSource
{
	GENERATED_BODY()
public:

	virtual FTransform GetTransform() const override;

	virtual void SetTransform(const FTransform& NewTransform) override;

	UPROPERTY()
	TObjectPtr<UGizmoBaseObject> Object;

	/** If true, Object->Modify() is called on SetTransform */
	UPROPERTY()
	bool bModifyObjectOnTransform = true;

public:
	/**
	 * Construct a default instance of UGizmoObjectWorldTransformSource with the given Component
	 */
	static UGizmoObjectWorldTransformSource* Construct(
		UGizmoBaseObject* InObject,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoObjectWorldTransformSource* NewSource = NewObject<UGizmoObjectWorldTransformSource>(Outer);
		NewSource->Object = InObject;
		return NewSource;
	}
};

