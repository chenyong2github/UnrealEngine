// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCSEditorExtensionContext.generated.h"

class SSCSEditor;

UCLASS()
class KISMET_API USCSEditorExtensionContext : public UObject
{
	GENERATED_BODY()

public:
	const TWeakPtr<SSCSEditor>& GetSCSEditor() const { return SCSEditor; }

private:
	friend SSCSEditor;

	TWeakPtr<SSCSEditor> SCSEditor;
};
