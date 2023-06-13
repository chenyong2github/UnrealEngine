// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/DebugSkelMeshComponent.h"
#include "SkeletalMesh/SkeletalMeshEditionInterface.h"

#include "UObject/Object.h"
#include "UObject/WeakInterfacePtr.h"

#include "SkeletalMeshEditorUtils.generated.h"

class UInteractiveToolsContext;
class UInteractiveToolManager;
class ISkeletalMeshEditor;
class USkeletalMeshEditorContextObject;

namespace UE
{
	
namespace SkeletalMeshEditorUtils
{
	SKELETALMESHMODELINGTOOLS_API bool RegisterEditorContextObject(UInteractiveToolsContext* InToolsContext);
	SKELETALMESHMODELINGTOOLS_API bool UnregisterEditorContextObject(UInteractiveToolsContext* InToolsContext);

	SKELETALMESHMODELINGTOOLS_API USkeletalMeshEditorContextObject* GetEditorContextObject(UInteractiveToolsContext* InToolsContext);
}
	
}

/**
 * USkeletalMeshEditorContextObject
 */

UCLASS()
class SKELETALMESHMODELINGTOOLS_API USkeletalMeshEditorContextObject : public USkeletalMeshEditorContextObjectBase
{
	GENERATED_BODY()

public:
	void Register(UInteractiveToolManager* InToolManager);
	void Unregister(UInteractiveToolManager* InToolManager);

	void Init(const TWeakPtr<ISkeletalMeshEditor>& InEditor);

	virtual void HideSkeleton() override;
	virtual void ShowSkeleton() override;

	virtual void BindTo(ISkeletalMeshEditingInterface* InEditingInterface) override;
	virtual void UnbindFrom(ISkeletalMeshEditingInterface* InEditingInterface) override;
	
private:
	TWeakPtr<ISkeletalMeshEditor> Editor = nullptr;

	UPROPERTY()
	ESkeletonDrawMode SkeletonDrawMode = ESkeletonDrawMode::Default;

	TSharedPtr<ISkeletalMeshEditorBinding> GetBinding() const;

	struct FBindData
	{
		FDelegateHandle ToToolNotifierHandle;
		FDelegateHandle FromToolNotifierHandle;		
	};
	TMap< ISkeletalMeshEditingInterface*, FBindData > Bindings;
	
	bool bRegistered = false;
};
