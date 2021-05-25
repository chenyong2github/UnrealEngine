// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCSEditorExtensionContext.generated.h"

class SSCSEditor;
class SSubobjectEditor;
class SSubobjectInstanceEditor;
class SSubobjectBlueprintEditor;

// #TODO_BH Rename this file to Subobject Extension context
UCLASS()
class KISMET_API USCSEditorExtensionContext : public UObject
{
	GENERATED_BODY()

public:
	
	UE_DEPRECATED(5.0, "GetSCSEditor has been deprecated. Use GetSubobjectEditor instead.")
	const TWeakPtr<SSCSEditor>& GetSCSEditor() const { return SCSEditor; }
	
	const TWeakPtr<SSubobjectEditor>& GetSubobjectEditor() const { return SubobjectEditor; }

private:
	friend SSCSEditor;
	friend SSubobjectEditor;
	friend SSubobjectInstanceEditor;
	friend SSubobjectBlueprintEditor;

	TWeakPtr<SSCSEditor> SCSEditor;
	TWeakPtr<SSubobjectEditor> SubobjectEditor;

};
