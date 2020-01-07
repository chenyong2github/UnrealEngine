// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Engine/Blueprint.h"
#include "Misc/Crc.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigCurveContainer.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "ControlRigController.h"
#include "ControlRigGizmoLibrary.h"
#include "ControlRigBlueprint.generated.h"

class UControlRigBlueprintGeneratedClass;
class USkeletalMesh;
class UControlRigGraph;

/** 
  * Source data used by the FControlRigBlueprintCompiler, can't be an editor plugin
  * because it is needed when running with -game.
  */

/** A link between two properties. Links become copies between property data at runtime. */
USTRUCT()
struct CONTROLRIGDEVELOPER_API FControlRigBlueprintPropertyLink
{
	GENERATED_BODY()

	FControlRigBlueprintPropertyLink() 
		: SourceLinkIndex(0)
		, DestLinkIndex(0)
		, SourcePropertyHash(0)
		, DestPropertyHash(0)
	{}

	FControlRigBlueprintPropertyLink(const FString& InSourcePropertyPath, const FString& InDestPropertyPath, uint32 InSourceLinkIndex, uint32 InDestLinkIndex)
		: SourcePropertyPath(InSourcePropertyPath)
		, DestPropertyPath(InDestPropertyPath)
		, SourceLinkIndex(InSourceLinkIndex)
		, DestLinkIndex(InDestLinkIndex)
		, SourcePropertyHash(FCrc::StrCrc32<TCHAR>(*SourcePropertyPath))
		, DestPropertyHash(FCrc::StrCrc32<TCHAR>(*DestPropertyPath))
	{
	}

	friend bool operator==(const FControlRigBlueprintPropertyLink& A, const FControlRigBlueprintPropertyLink& B)
	{
		return A.SourcePropertyHash == B.SourcePropertyHash && A.DestPropertyHash == B.DestPropertyHash;
	}

	const FString& GetSourcePropertyPath() const { return SourcePropertyPath; }
	const FString& GetDestPropertyPath() const { return DestPropertyPath; }

	int32 GetSourceLinkIndex() const { return SourceLinkIndex; }
	int32 GetDestLinkIndex() const { return DestLinkIndex; }

private:

	FString GetSourceUnitName() const { return GetUnitName(SourcePropertyPath); }
	FString GetDestUnitName() const { return GetUnitName(DestPropertyPath); }

	static FString GetUnitName(FString Input)
	{
		int32 ParseIndex = 0;
		if (Input.FindChar(TCHAR('.'), ParseIndex))
		{
			return Input.Left(ParseIndex);
		}
		return Input;
	}

	/** Path to the property we are linking from */
	UPROPERTY(VisibleAnywhere, Category="Links")
	FString SourcePropertyPath;

	/** Path to the property we are linking to */
	UPROPERTY(VisibleAnywhere, Category="Links")
	FString DestPropertyPath;

	/** Index of the link on the source unit */
	UPROPERTY()
	int32 SourceLinkIndex;

	/** Index of the link on the destination unit */
	UPROPERTY()
	int32 DestLinkIndex;

	// Hashed strings for faster comparisons
	UPROPERTY()
	uint32 SourcePropertyHash;

	// Hashed strings for faster comparisons
	UPROPERTY()
	uint32 DestPropertyHash;

	friend class FControlRigBlueprintCompilerContext;
};

UCLASS(BlueprintType)
class CONTROLRIGDEVELOPER_API UControlRigBlueprint : public UBlueprint, public IInterface_PreviewMeshProvider
{
	GENERATED_BODY()

public:
	UControlRigBlueprint();

	void InitializeModel();

	/** Get the (full) generated class for this control rig blueprint */
	UControlRigBlueprintGeneratedClass* GetControlRigBlueprintGeneratedClass() const;

	/** Get the (skeleton) generated class for this control rig blueprint */
	UControlRigBlueprintGeneratedClass* GetControlRigBlueprintSkeletonClass() const;

#if WITH_EDITOR

	// UBlueprint interface
	virtual UClass* GetBlueprintClass() const override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return false; }
	virtual void LoadModulesRequiredForCompilation() override;
	virtual void GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;	
	virtual void SetObjectBeingDebugged(UObject* NewObject) override;
	virtual bool RequiresMarkAsStructurallyModifiedOnUndo() const override { return false; }
	virtual void PostLoad() override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;

	virtual bool SupportsGlobalVariables() const override { return false; }
	virtual bool SupportsLocalVariables() const override { return false; }
	virtual bool SupportsFunctions() const override { return false; }
	virtual bool SupportsMacros() const override { return false; }
	virtual bool SupportsDelegates() const override { return false; }
	virtual bool SupportsEventGraphs() const override { return false; }
	virtual bool SupportsAnimLayers() const override { return false; }


#endif	// #if WITH_EDITOR

	/** Make a property link between the specified properties - used by the compiler */
	void MakePropertyLink(const FString& InSourcePropertyPath, const FString& InDestPropertyPath, int32 InSourceLinkIndex, int32 InDestLinkIndex);

	/** IInterface_PreviewMeshProvider interface */
	virtual void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty = true) override;
	virtual USkeletalMesh* GetPreviewMesh() const override;

	UPROPERTY(transient)
	UControlRigModel* Model;

	UPROPERTY(transient)
	UControlRigController* ModelController;

	// hack until RigVM refactoring
	TMap<FName, EControlRigModelParameterType> NodeToParameterType;

	bool bSuspendModelNotificationsForSelf;
	bool bSuspendModelNotificationsForOthers;
	FName LastNameFromNotification;

	void PopulateModelFromGraph(const UControlRigGraph* InGraph);
	void RebuildGraphFromModel();

	UControlRigModel::FModifiedEvent& OnModified();

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, config, Category = DefaultGizmo)
	TAssetPtr<UControlRigGizmoLibrary> GizmoLibrary;
#endif

private:
	/** Links between the various properties we have */
	UPROPERTY()
	TArray<FControlRigBlueprintPropertyLink> PropertyLinks;

	/** list of operators. Visible for debug purpose for now */
	UPROPERTY()
	TArray<FControlRigOperator> Operators_DEPRECATED;

	// need list of "allow query property" to "source" - whether rig unit or property itself
	// this will allow it to copy data to target
	UPROPERTY()
	TMap<FName, FString> AllowSourceAccessProperties;

public:
	UPROPERTY()
	FRigHierarchyContainer HierarchyContainer;

private:

	UPROPERTY()
	FRigBoneHierarchy Hierarchy_DEPRECATED;

	UPROPERTY()
	FRigCurveContainer CurveContainer_DEPRECATED;

	/** The default skeletal mesh to use when previewing this asset */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<USkeletalMesh> PreviewSkeletalMesh;

	/** The default skeletal mesh to use when previewing this asset */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<UObject> SourceHierarchyImport;

	UPROPERTY(DuplicateTransient, AssetRegistrySearchable)
	TSoftObjectPtr<UObject> SourceCurveImport;

	UControlRigModel::FModifiedEvent _ModifiedEvent;
	void HandleModelModified(const UControlRigModel* InModel, EControlRigModelNotifType InType, const void* InPayload);
	bool UpdateParametersOnControlRig(UControlRig* InRig = nullptr);
	bool PerformArrayOperation(const FString& InPropertyPath, TFunctionRef<bool(FScriptArrayHelper&, int32)> InOperation, bool bCallModify, bool bPropagateToInstances);
	void CleanupBoneHierarchyDeprecated();

	void PropagatePoseFromInstanceToBP();
	void PropagateHierarchyFromBPToInstances(bool bInitialize = true);
#if WITH_EDITOR
	void HandleOnElementAdded(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);
	void HandleOnElementReparented(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void HandleOnElementSelected(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey, bool bSelected);
#endif

	friend class FControlRigBlueprintCompilerContext;
	friend class SRigHierarchy;
	friend class SRigCurveContainer;
	friend class FControlRigEditor;
	friend class UEngineTestControlRig;
	friend class FControlRigEditMode;
};
