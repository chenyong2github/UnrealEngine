// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshBrushTool.h"
#include "Changes\ValueWatcher.h"
#include "Changes\IndexedAttributeChange.h"
#include "DynamicVerticesOctree3.h"
#include "MeshDescription.h"
#include "MeshAttributePaintTool.generated.h"


struct FMeshDescription;


/**
 * Maps float values to linear color ramp.
 */
class FFloatAttributeColorMapper
{
public:
	virtual ~FFloatAttributeColorMapper() {}

	FLinearColor LowColor = FLinearColor(0.9f, 0.9f, 0.9f, 1.0f);
	FLinearColor HighColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	virtual FLinearColor ToColor(float Value)
	{
		float t = FMath::Clamp(Value, 0.0f, 1.0f);
		return FLinearColor(
			FMathf::Lerp(LowColor.R, HighColor.R, t),
			FMathf::Lerp(LowColor.G, HighColor.G, t),
			FMathf::Lerp(LowColor.B, HighColor.B, t),
			1.0f);
	}
};


/**
 * Abstract interface to a single-channel indexed floating-point attribute
 */
class IMeshVertexAttributeAdapter
{
public:
	virtual ~IMeshVertexAttributeAdapter() {}

	virtual int ElementNum() const = 0;
	virtual float GetValue(int32 Index) const = 0;
	virtual void SetValue(int32 Index, float Value) = 0;
	virtual FInterval1f GetValueRange() = 0;
};



/**
 * Abstract interface to a set of single-channel indexed floating-point attributes
 */
class IMeshVertexAttributeSource
{
public:
	virtual ~IMeshVertexAttributeSource() {}

	virtual TArray<FName> GetAttributeList() = 0;
	virtual TUniquePtr<IMeshVertexAttributeAdapter> GetAttribute(FName AttributeName) = 0;
	/** @return number of indices in each attribute */
	virtual int32 GetAttributeElementNum() = 0;
};






/**
 * Tool Builder for Attribute Paint Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshAttributePaintToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	/** Optional color map customization */
	TUniqueFunction<TUniquePtr<FFloatAttributeColorMapper>()> ColorMapFactory;

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};




/**
 * Selected-Attribute settings Attribute Paint Tool
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshAttributePaintToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = Attribute)
	TArray<FName> Attributes;

	UPROPERTY(EditAnywhere, Category = Attribute, meta = (ArrayClamp = "Attributes"))
	int SelectedAttribute = 0;

	UPROPERTY(VisibleAnywhere, Category = Attribute)
	FString AttributeName;
};






UENUM()
enum class EMeshAttributePaintToolActions
{
	NoAction
};



UCLASS()
class MESHMODELINGTOOLS_API UMeshAttributePaintEditActions : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UMeshAttributePaintTool> ParentTool;

	void Initialize(UMeshAttributePaintTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EMeshAttributePaintToolActions Action);
};



/**
 * FCommandChange for color map changes
 */
class MESHMODELINGTOOLS_API FMeshAttributePaintChange : public TCustomIndexedValuesChange<float, int32>
{
public:
	virtual FString ToString() const override
	{
		return FString(TEXT("Paint Attribute"));
	}
};



/**
 * UMeshAttributePaintTool paints single-channel float attributes on a MeshDescription.
 * 
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshAttributePaintTool : public UDynamicMeshBrushTool
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;
	virtual void OnTick(float DeltaTime) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }

	// UBaseBrushTool overrides
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

	virtual void RequestAction(EMeshAttributePaintToolActions ActionType);

	virtual void SetColorMap(TUniquePtr<FFloatAttributeColorMapper> ColorMap);

protected:
	virtual void ApplyStamp(const FBrushStampData& Stamp);

	virtual void OnShutdown(EToolShutdownType ShutdownType) override;
	

protected:
	UPROPERTY()
	UMeshAttributePaintToolProperties* AttribProps;

	TValueWatcher<int32> SelectedAttributeWatcher;

	//UPROPERTY()
	//UMeshAttributePaintEditActions* AttributeEditActions;

protected:
	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	bool bInRemoveStroke = false;
	bool bInSmoothStroke = false;
	FBrushStampData StartStamp;
	FBrushStampData LastStamp;
	bool bStampPending;

	TUniquePtr<FMeshDescription> EditedMesh;

	double CalculateBrushFalloff(double Distance);
	TDynamicVerticesOctree3<FDynamicMesh3> VerticesOctree;
	TArray<int> PreviewBrushROI;
	void CalculateVertexROI(const FBrushStampData& Stamp, TArray<int>& VertexROI);

	TUniquePtr<FFloatAttributeColorMapper> ColorMapper;
	TUniquePtr<IMeshVertexAttributeSource> AttributeSource;

	struct FAttributeData
	{
		FName Name;
		TUniquePtr<IMeshVertexAttributeAdapter> Attribute;
		TArray<float> CurrentValues;
		TArray<float> InitialValues;
	};
	TArray<FAttributeData> Attributes;
	int32 AttributeBufferCount;
	int32 CurrentAttributeIndex;
	FInterval1f CurrentValueRange;

	// actions

	bool bHavePendingAction = false;
	EMeshAttributePaintToolActions PendingAction;
	virtual void ApplyAction(EMeshAttributePaintToolActions ActionType);

	bool bVisibleAttributeValid = false;
	int32 PendingNewSelectedIndex = -1;
	
	void InitializeAttributes();
	void StoreCurrentAttribute();
	void UpdateVisibleAttribute();
	void UpdateSelectedAttribute(int32 NewSelectedIndex);

	TUniquePtr<TIndexedValuesChangeBuilder<float, FMeshAttributePaintChange>> ActiveChangeBuilder;
	void BeginChange();
	TUniquePtr<FMeshAttributePaintChange> EndChange();
	void ExternalUpdateValues(int32 AttribIndex, const TArray<int32>& VertexIndices, const TArray<float>& NewValues);
};







