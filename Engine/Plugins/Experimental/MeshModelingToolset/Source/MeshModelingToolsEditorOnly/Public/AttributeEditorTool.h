// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolBuilder.h"
#include "AttributeEditorTool.generated.h"


/**
 *
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};






UENUM()
enum class EAttributeEditorElementType : uint8
{
	Vertex = 0,
	VertexInstance = 1,
	Triangle = 2,
	Polygon = 3,
	Edge = 4,
	PolygonGroup = 5
};


UENUM()
enum class EAttributeEditorAttribType : uint8
{
	Int32 = 0,
	Boolean = 1,
	Float = 2,
	Vector2 = 3,
	Vector3 = 4,
	Vector4 = 5,
	String = 6,
	Unknown = 7
};


struct FAttributeEditorAttribInfo
{
	FName Name;
	EAttributeEditorElementType ElementType;
	EAttributeEditorAttribType DataType;
};


UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorAttribProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = AttributesInspector)
	TArray<FString> VertexAttributes;

	UPROPERTY(VisibleAnywhere, Category = AttributesInspector)
	TArray<FString> InstanceAttributes;

	UPROPERTY(VisibleAnywhere, Category = AttributesInspector)
	TArray<FString> TriangleAttributes;

	UPROPERTY(VisibleAnywhere, Category = AttributesInspector)
	TArray<FString> PolygonAttributes;

	UPROPERTY(VisibleAnywhere, Category = AttributesInspector)
	TArray<FString> EdgeAttributes;

	UPROPERTY(VisibleAnywhere, Category = AttributesInspector)
	TArray<FString> GroupAttributes;
};




UENUM()
enum class EAttributeEditorToolActions
{
	NoAction,
	ClearNormals,
	ClearSelectedUVs,
	ClearAllUVs,
	AddAttribute,
	AddWeightMapLayer,
	AddPolyGroupLayer,
	DeleteAttribute,
	ClearAttribute,
	CopyAttributeFromTo
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UAttributeEditorTool> ParentTool;

	void Initialize(UAttributeEditorTool* ParentToolIn) { ParentTool = ParentToolIn; }
	void PostAction(EAttributeEditorToolActions Action);

};


UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorNormalsActions : public UAttributeEditorActionPropertySet
{
	GENERATED_BODY()
public:
	/** Remove any hard edges / split normals, setting all normals to a single vertex normal */
	UFUNCTION(CallInEditor, Category = Normals, meta = (DisplayPriority = 1))
	void ResetHardNormals()
	{
		PostAction(EAttributeEditorToolActions::ClearNormals);
	}
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorUVActions : public UAttributeEditorActionPropertySet
{
	GENERATED_BODY()
public:

	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 0", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 0"))
	bool bClearUVLayer0;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 1", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 1"))
	bool bClearUVLayer1;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 2", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 2"))
	bool bClearUVLayer2;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 3", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 3"))
	bool bClearUVLayer3;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 4", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 4"))
	bool bClearUVLayer4;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 5", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 5"))
	bool bClearUVLayer5;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 6", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 6"))
	bool bClearUVLayer6;
	/** Clear data from UV layer */
	UPROPERTY(EditAnywhere, Category = UVs, meta = (DisplayName = "Layer 7", HideEditConditionToggle, EditConditionHides, EditCondition = "NumUVLayers > 7"))
	bool bClearUVLayer7;

	UPROPERTY()
	int NumUVLayers = 0;

	/** Clear the selected UV layers, setting all UV values to (0,0) */
	UFUNCTION(CallInEditor, Category = UVs, meta = (DisplayPriority = 1))
	void ClearSelectedUVSets()
	{
		PostAction(EAttributeEditorToolActions::ClearSelectedUVs);
	}

	/** Clear all UV layers, setting all UV values to (0,0) */
	UFUNCTION(CallInEditor, Category = UVs, meta = (DisplayPriority = 2))
	void ClearAllUVSets()
	{
		PostAction(EAttributeEditorToolActions::ClearAllUVs);
	}
};



UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorNewAttributeActions : public UAttributeEditorActionPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = NewAttribute, meta = (DisplayName = "New Attribute Name") )
	FString NewName;

	//UPROPERTY(EditAnywhere, Category = NewAttribute)
	UPROPERTY()
	EAttributeEditorElementType ElementType;

	//UPROPERTY(EditAnywhere, Category = NewAttribute)
	UPROPERTY()
	EAttributeEditorAttribType DataType;

	//UFUNCTION(CallInEditor, Category = NewAttribute, meta = (DisplayPriority = 1))
	//void AddNew()
	//{
	//	PostAction(EAttributeEditorToolActions::AddAttribute);
	//}

	/** Add a new Per-Vertex Weight Map layer with the given Name */
	UFUNCTION(CallInEditor, Category = NewAttribute, meta = (DisplayPriority = 2))
	void AddWeightMapLayer()
	{
		PostAction(EAttributeEditorToolActions::AddWeightMapLayer);
	}

	/** Add a new PolyGroup layer with the given Name */
	UFUNCTION(CallInEditor, Category = NewAttribute, meta = (DisplayPriority = 3))
	void AddPolyGroupLayer()
	{
		PostAction(EAttributeEditorToolActions::AddPolyGroupLayer);
	}

};


UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorModifyAttributeActions : public UAttributeEditorActionPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = ModifyAttribute, meta = (GetOptions = GetAttributeNamesFunc))
	FString Attribute;

	UFUNCTION()
	TArray<FString> GetAttributeNamesFunc();

	UPROPERTY()
	TArray<FString> AttributeNamesList;

	/** Remove the selected Attribute Name from the mesh */
	UFUNCTION(CallInEditor, Category = ModifyAttribute, meta = (DisplayPriority = 1))
	void DeleteSelected()
	{
		PostAction(EAttributeEditorToolActions::DeleteAttribute);
	}

	//UFUNCTION(CallInEditor, Category = ModifyAttribute, meta = (DisplayPriority = 2))
	//void Clear()
	//{
	//	PostAction(EAttributeEditorToolActions::ClearAttribute);
	//}

};


UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorCopyAttributeActions : public UAttributeEditorActionPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = CopyAttribute)
	TArray<FString> FromAttribute;

	UPROPERTY(EditAnywhere, Category = CopyAttribute)
	TArray<FString> ToAttribute;

	UFUNCTION(CallInEditor, Category = CopyAttribute, meta = (DisplayPriority = 1))
	void CopyFromTo()
	{
		PostAction(EAttributeEditorToolActions::CopyAttributeFromTo);
	}
};






/**
 * Mesh Attribute Editor Tool
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAttributeEditorTool : public UMultiSelectionTool
{
	GENERATED_BODY()

public:

	UAttributeEditorTool();

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void OnTick(float DeltaTime) override;

	virtual void SetWorld(UWorld* World);

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }

	virtual void RequestAction(EAttributeEditorToolActions ActionType);

protected:

	UPROPERTY()
	UAttributeEditorNormalsActions* NormalsActions;

	UPROPERTY()
	UAttributeEditorUVActions* UVActions;

	UPROPERTY()
	UAttributeEditorAttribProperties* AttributeProps;

	UPROPERTY()
	UAttributeEditorNewAttributeActions* NewAttributeProps;

	UPROPERTY()
	UAttributeEditorModifyAttributeActions* ModifyAttributeProps;

	UPROPERTY()
	UAttributeEditorCopyAttributeActions* CopyAttributeProps;


protected:
	UWorld* TargetWorld;

	TArray<FAttributeEditorAttribInfo> VertexAttributes;
	TArray<FAttributeEditorAttribInfo> InstanceAttributes;
	TArray<FAttributeEditorAttribInfo> TriangleAttributes;
	TArray<FAttributeEditorAttribInfo> PolygonAttributes;
	TArray<FAttributeEditorAttribInfo> EdgeAttributes;
	TArray<FAttributeEditorAttribInfo> GroupAttributes;


	bool bAttributeListsValid = false;
	void InitializeAttributeLists();



	EAttributeEditorToolActions PendingAction = EAttributeEditorToolActions::NoAction;
	void ClearNormals();
	void ClearUVs(bool bSelectedOnly);
	void AddNewAttribute();
	void AddNewWeightMap();
	void AddNewGroupsLayer();
	void DeleteAttribute();
	void ClearAttribute();

	void AddNewAttribute(EAttributeEditorElementType ElemType, EAttributeEditorAttribType AttribType, FName AttributeName);
};
