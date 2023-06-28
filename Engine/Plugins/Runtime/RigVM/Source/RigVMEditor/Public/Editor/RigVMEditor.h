// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditor.h"
#include "RigVMHost.h"
#include "RigVMBlueprint.h"

class RIGVMEDITOR_API FRigVMEditor : public FBlueprintEditor
{
public:

	FRigVMEditor();

	// returns the blueprint being edited
	URigVMBlueprint* GetRigVMBlueprint() const;

	// returns the currently debugged / viewed host
	URigVMHost* GetRigVMHost() const;

	// returns the class to use for detail wrapper objects (UI shim layer)
	virtual UClass* GetDetailWrapperClass() const;

	virtual void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	DECLARE_EVENT_OneParam(FRigVMEditor, FPreviewHostUpdated, FRigVMEditor*);
	FPreviewHostUpdated& OnPreviewHostUpdated() { return PreviewHostUpdated;  }

protected:

	void SetHost(URigVMHost* InHost);

private:

	/** Our currently running rig vm instance */
	TObjectPtr<URigVMHost> Host;

	FPreviewHostUpdated PreviewHostUpdated;

	friend class SRigVMExecutionStackView;
};
