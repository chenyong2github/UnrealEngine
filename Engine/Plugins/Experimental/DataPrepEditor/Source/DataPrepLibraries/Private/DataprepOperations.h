// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepOperation.h"

#include "DataPrepOperationsLibrary.h"

#include "EditorStaticMeshLibrary.h"
#include "Engine/EngineTypes.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"

class SWidget;
class UMaterialInterface;
class UDataTable;

#include "DataprepOperations.generated.h"

/** Local struct used by UDataprepSetLODsOperation to better control UX */
USTRUCT(BlueprintType)
struct FDataprepSetLODsReductionSettings
{
	GENERATED_BODY()

	FDataprepSetLODsReductionSettings()
		: PercentTriangles(0.5f)
		, ScreenSize(0.5f)
	{ }

	// Percentage of triangles to keep. Ranges from 0.0 to 1.0: 1.0 = no reduction, 0.0 = no triangles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SetLODsReductionSettings, meta=(UIMin = "0.0", UIMax = "1.0"))
	float PercentTriangles;

	// ScreenSize to display this LOD. Ranges from 0.0 to 1.0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SetLODsReductionSettings, meta=(UIMin = "0.0", UIMax = "1.0"))
	float ScreenSize;
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Set LODs", ToolTip = "For each static mesh to process, replace the existing static mesh's LODs with new ones based on the set of reduction settings") )
class UDataprepSetLODsOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSetLODsOperation()
		: bAutoComputeLODScreenSize(true)
	{
	}

public:
	// If true, the screen sizes at which LODs swap are computed automatically
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (DisplayName = "Auto Screen Size", ToolTip = "If true, the screen sizes at which LODs swap are automatically computed"))
	bool bAutoComputeLODScreenSize;

	// Array of reduction settings to apply to each new LOD.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, Meta = (ToolTip = "Array of LOD reduction settings") )
	TArray<FDataprepSetLODsReductionSettings> ReductionSettings;

	// #ueent_todo: Limit size of array to MAX_STATIC_MESH_LODS

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Set LOD Group", ToolTip = "For each static mesh to process, replace the existing static mesh's LODs with new ones based on selected group") )
class UDataprepSetLODGroupOperation : public UDataprepOperation
{
	GENERATED_BODY()

public:
	UDataprepSetLODGroupOperation();

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface

private:
	// Name of the pre-defined LOD group to apply on the selected objects
	UPROPERTY(EditAnywhere, Category = SetLOGGroup_Internal, meta = (ToolTip = ""))
	FName GroupName;

	friend class FDataprepSetLOGGroupDetails;
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Set Simple Collision", ToolTip = "For each static mesh to process, replace the existing static mesh's collision setup with a simple one based on selected shape") )
class UDataprepSetSimpleCollisionOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSetSimpleCollisionOperation()
		: ShapeType(EScriptingCollisionShapeType::Box)
	{
	}

public:
	// Shape's of the collision geometry encompassing the static mesh
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Shape's of the collision geometry encompassing the static mesh"))
	EScriptingCollisionShapeType ShapeType;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Set Convex Collision", ToolTip = "For each static mesh to process, replace the existing static mesh's collision setup with a convex decomposition one computed using the Hull settings") )
class UDataprepSetConvexDecompositionCollisionOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSetConvexDecompositionCollisionOperation()
		: HullCount(4)
		, MaxHullVerts(16)
		, HullPrecision(100000)
	{
	}

public:
	// Maximum number of convex pieces that will be created
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Maximum number of convex pieces that will be created"))
	int32 HullCount;
	
	// Maximum number of vertices allowed for any generated convex hulls
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Maximum number of vertices allowed for any generated convex hulls"))
	int32 MaxHullVerts;
	
	// Number of voxels to use when generating collision
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Number of voxels to use when generating collision"))
	int32 HullPrecision;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Enable Lightmap UVs", ToolTip = "For each static mesh to process, enable or disable the generation of lightmap UVs") )
class UDataprepSetGenerateLightmapUVsOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSetGenerateLightmapUVsOperation()
		: bGenerateLightmapUVs(true)
	{
	}

public:
	// The value to set for the generate lightmap uvs flag on each static mesh
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Maximum number of convex pieces that will be created"))
	bool bGenerateLightmapUVs;

protected:
	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = ActorOperation, Meta = (DisplayName="Set Mobility", ToolTip = "For each mesh actor to process, update its mobilty with the selected value") )
class UDataprepSetMobilityOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSetMobilityOperation()
		: MobilityType(EComponentMobility::Static)
	{
	}

public:
	// Type of mobility to set on mesh actors
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Type of mobility to set on mesh actors"))
	TEnumAsByte<EComponentMobility::Type> MobilityType;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::MeshOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Set Material", ToolTip = "On each static mesh or actor to process, replace any materials used with the specified one") )
class UDataprepSetMaterialOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSetMaterialOperation()
		: MaterialSubstitute(nullptr)
	{
	}

public:
	// Material to use as a substitute
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Material to use as a substitute"))
	UMaterialInterface* MaterialSubstitute;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Substitute Material", ToolTip = "On each static mesh or actor to process, replace the material matching the criteria with the specified one") )
class UDataprepSubstituteMaterialOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSubstituteMaterialOperation()
		: MaterialSearch(TEXT("*"))
		, StringMatch(EEditorScriptingStringMatchType::MatchesWildcard)
		, MaterialSubstitute(nullptr)
	{
	}

public:
	// Name of the material(s) to search for. Wildcard is supported
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Name of the material(s) to search for. Wildcard is supported"))
	FString MaterialSearch;

	// Type of matching to perform with MaterialSearch string
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Type of matching to perform with MaterialSearch string"))
	EEditorScriptingStringMatchType StringMatch;

	// Material to use as a substitute
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Material to use as a substitute"))
	UMaterialInterface* MaterialSubstitute;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = MeshOperation, Meta = (DisplayName="Substitute Material By Table", ToolTip = "On each static mesh or actor to process, replace the material found in the first column of the table with the one from the second column in the same row") )
class UDataprepSubstituteMaterialByTableOperation : public UDataprepOperation
{
	GENERATED_BODY()

	UDataprepSubstituteMaterialByTableOperation()
		: MaterialDataTable(nullptr)
	{
	}

public:
	// Data table to use for the substitution
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = MeshOperation, meta = (ToolTip = "Data table to use for the substitution"))
	UDataTable* MaterialDataTable;

	//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

UCLASS(Experimental, Category = ObjectOperation, Meta = (DisplayName="Remove Objects", ToolTip = "Remove any asset or actor to process") )
class UDataprepRemoveObjectsOperation : public UDataprepOperation
{
	GENERATED_BODY()

		//~ Begin UDataprepOperation Interface
public:
	virtual FText GetCategory_Implementation() const override
	{
		return FDataprepOperationCategories::ObjectOperation;
	}

protected:
	virtual void OnExecution_Implementation(const FDataprepContext& InContext) override;
	//~ End UDataprepOperation Interface
};

// Customization of the details of the Datasmith Scene for the data prep editor.
class FDataprepSetLOGGroupDetails : public IDetailCustomization
{
public:
	static TSharedRef< IDetailCustomization > MakeDetails() { return MakeShared<FDataprepSetLOGGroupDetails>(); };

	FDataprepSetLOGGroupDetails() : DataprepOperation(nullptr) {}

	/** Called when details should be customized */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

private:
	TSharedRef< SWidget > CreateWidget();
	void OnLODGroupChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);

private:
	UDataprepSetLODGroupOperation* DataprepOperation;

	/** LOD group options. */
	TArray< TSharedPtr< FString > > LODGroupOptions;
	TArray<FName>					LODGroupNames;

	TSharedPtr<IPropertyHandle> LodGroupPropertyHandle;
};
