// Copyright Epic Games, Inc. All Rights Reserved.

#include "AttributeEditorTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"
#include "MathUtil.h"
#include "MeshDescriptionToDynamicMesh.h"   // for FMeshDescriptionToDynamicMesh::IsReservedAttributeName
#include "AssetUtils/MeshDescriptionUtil.h"

#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshOperations.h"

// for lightmap access
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"

#include "Components/PrimitiveComponent.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UAttributeEditorTool"

/*
 * ToolBuilder
 */


const FToolTargetTypeRequirements& UAttributeEditorToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UAttributeEditorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) > 0;
}

UInteractiveTool* UAttributeEditorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UAttributeEditorTool* NewTool = NewObject<UAttributeEditorTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
	NewTool->SetWorld(SceneState.World);

	return NewTool;
}




void UAttributeEditorActionPropertySet::PostAction(EAttributeEditorToolActions Action)
{
	if (ParentTool.IsValid() && Cast<UAttributeEditorTool>(ParentTool))
	{
		Cast<UAttributeEditorTool>(ParentTool)->RequestAction(Action);
	}
}




/*
 * Tool
 */



TArray<FString> UAttributeEditorUVActions::GetUVLayerNamesFunc()
{
	return UVLayerNamesList;
}


TArray<FString> UAttributeEditorModifyAttributeActions::GetAttributeNamesFunc()
{
	return AttributeNamesList;
}




UAttributeEditorTool::UAttributeEditorTool()
{
}

void UAttributeEditorTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UAttributeEditorTool::Setup()
{
	UInteractiveTool::Setup();

	OptimizeActions = NewObject<UAttributeEditorOptimizeActions>(this);
	OptimizeActions->Initialize(this);
	AddToolPropertySource(OptimizeActions);

	NormalsActions = NewObject<UAttributeEditorNormalsActions>(this);
	NormalsActions->Initialize(this);
	AddToolPropertySource(NormalsActions);

	if (Targets.Num() == 1)
	{
		UVActions = NewObject<UAttributeEditorUVActions>(this);
		UVActions->Initialize(this);
		AddToolPropertySource(UVActions);

		LightmapUVActions = NewObject<UAttributeEditorLightmapUVActions>(this);
		LightmapUVActions->Initialize(this);
		AddToolPropertySource(LightmapUVActions);

		NewAttributeProps = NewObject<UAttributeEditorNewAttributeActions>(this);
		NewAttributeProps->Initialize(this);
		AddToolPropertySource(NewAttributeProps);

		ModifyAttributeProps = NewObject<UAttributeEditorModifyAttributeActions>(this);
		ModifyAttributeProps->Initialize(this);
		AddToolPropertySource(ModifyAttributeProps);
		//SetToolPropertySourceEnabled(ModifyAttributeProps, false);

		CopyAttributeProps = NewObject<UAttributeEditorCopyAttributeActions>(this);
		CopyAttributeProps->Initialize(this);
		AddToolPropertySource(CopyAttributeProps);
		SetToolPropertySourceEnabled(CopyAttributeProps, false);

		AttributeProps = NewObject<UAttributeEditorAttribProperties>(this);
		AddToolPropertySource(AttributeProps);

		InitializeAttributeLists();
	}

	SetToolDisplayName(LOCTEXT("ToolName", "Edit Attributes"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartAttribEditor", "Inspect and Modify Attributes of a StaticMesh Asset"),
		EToolMessageLevel::UserNotification);
}


template<typename AttribSetType>
void ExtractAttribList(FMeshDescription* Mesh, AttribSetType& AttribSet, EAttributeEditorElementType ElemType, TArray<FAttributeEditorAttribInfo>& AttribList, TArray<FString>& StringList)
{
	AttribList.Reset();
	StringList.Reset();

	static FString EnumStrings[] = {
		"Int32", "Boolean", "Float", "Vector2", "Vector3", "Vector4", "String", "Unknown"
	};

	AttribSet.ForEach([&](const FName AttributeName, auto AttributesRef)
	{
		FAttributeEditorAttribInfo AttribInfo;
		AttribInfo.Name = AttributeName;
		AttribInfo.ElementType = ElemType;
		AttribInfo.DataType = EAttributeEditorAttribType::Unknown;
		if (AttribSet.template HasAttributeOfType<int32>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::Int32;
		}
		else if (AttribSet.template HasAttributeOfType<float>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::Float;
		}
		else if (AttribSet.template HasAttributeOfType<bool>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::Boolean;
		}
		else if (AttribSet.template HasAttributeOfType<FVector2D>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::Vector2;
		}
		else if (AttribSet.template HasAttributeOfType<FVector>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::Vector3;
		}
		else if (AttribSet.template HasAttributeOfType<FVector4>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::Vector4;
		}
		else if (AttribSet.template HasAttributeOfType<FName>(AttributeName))
		{
			AttribInfo.DataType = EAttributeEditorAttribType::String;
		}
		AttribList.Add(AttribInfo);

		bool bIsAutoGen = (AttributesRef.GetFlags() & EMeshAttributeFlags::AutoGenerated) != EMeshAttributeFlags::None;
		
		FString UIString = (bIsAutoGen) ?
			FString::Printf(TEXT("%s - %s (autogen)"), *(AttribInfo.Name.ToString()), *EnumStrings[(int32)AttribInfo.DataType])
			: FString::Printf(TEXT("%s - %s"), *(AttribInfo.Name.ToString()), *EnumStrings[(int32)AttribInfo.DataType]);
		StringList.Add(UIString);
	});
}



static FAttributesSetBase* GetAttributeSetByType(FMeshDescription* Mesh, EAttributeEditorElementType ElemType)
{
	switch (ElemType)
	{
	case EAttributeEditorElementType::Vertex:
		return &Mesh->VertexAttributes();
	case EAttributeEditorElementType::VertexInstance:
		return &Mesh->VertexInstanceAttributes();
	case EAttributeEditorElementType::Triangle:
		return &Mesh->TriangleAttributes();
	case EAttributeEditorElementType::Polygon:
		return &Mesh->PolygonAttributes();
	case EAttributeEditorElementType::Edge:
		return &Mesh->EdgeAttributes();
	case EAttributeEditorElementType::PolygonGroup:
		return &Mesh->PolygonGroupAttributes();
	}
	check(false);
	return nullptr;
}

static bool HasAttribute(FMeshDescription* Mesh, EAttributeEditorElementType ElemType, FName AttributeName)
{
	FAttributesSetBase* AttribSetBase = GetAttributeSetByType(Mesh, ElemType);
	return (AttribSetBase) ? AttribSetBase->HasAttribute(AttributeName) : false;
}

static bool AddAttribute(FMeshDescription* Mesh, EAttributeEditorElementType ElemType, EAttributeEditorAttribType AttribType, FName AttributeName)
{
	FAttributesSetBase* AttribSetBase = GetAttributeSetByType(Mesh, ElemType);
	if (AttribSetBase != nullptr)
	{
		switch (AttribType)
		{
		case EAttributeEditorAttribType::Int32:
			AttribSetBase->RegisterAttribute<int32>(AttributeName, 1, 0, EMeshAttributeFlags::None);
			return true;
		case EAttributeEditorAttribType::Boolean:
			AttribSetBase->RegisterAttribute<bool>(AttributeName, 1, false, EMeshAttributeFlags::None);
			return true;
		case EAttributeEditorAttribType::Float:
			AttribSetBase->RegisterAttribute<float>(AttributeName, 1, 0.0f, EMeshAttributeFlags::Lerpable);
			return true;
		case EAttributeEditorAttribType::Vector2:
			AttribSetBase->RegisterAttribute<FVector2D>(AttributeName, 1, FVector2D::ZeroVector, EMeshAttributeFlags::Lerpable);
			return true;
		case EAttributeEditorAttribType::Vector3:
			AttribSetBase->RegisterAttribute<FVector>(AttributeName, 1, FVector::ZeroVector, EMeshAttributeFlags::Lerpable);
			return true;
		case EAttributeEditorAttribType::Vector4:
			AttribSetBase->RegisterAttribute<FVector4>(AttributeName, 1, FVector4(0,0,0,1), EMeshAttributeFlags::Lerpable);
			return true;
		}
	}
	return false;
}



static bool RemoveAttribute(FMeshDescription* Mesh, EAttributeEditorElementType ElemType, FName AttributeName)
{
	FAttributesSetBase* AttribSetBase = GetAttributeSetByType(Mesh, ElemType);
	if (AttribSetBase != nullptr)
	{
		AttribSetBase->UnregisterAttribute(AttributeName);
		return true;
	}
	return false;
}



void UAttributeEditorTool::InitializeAttributeLists()
{
	FMeshDescription* Mesh = TargetMeshProviderInterface(0)->GetMeshDescription();


	TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
		Mesh->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);

	UVActions->UVLayerNamesList.Reset();
	for ( int32 k = 0; k < InstanceUVs.GetNumChannels(); ++k )
	{
		UVActions->UVLayerNamesList.Add(FString::Printf(TEXT("UV%d"), k));
	}
	UVActions->UVLayer = UVActions->UVLayerNamesList[0];

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(TargetComponentInterface(0)->GetOwnerComponent());
	if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh() != nullptr)
	{
		const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		const FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
		LightmapUVActions->bGenerateLightmapUVs = BuildSettings.bGenerateLightmapUVs;
		LightmapUVActions->SourceUVIndex = BuildSettings.SrcLightmapIndex;
		LightmapUVActions->DestinationUVIndex = BuildSettings.DstLightmapIndex;

		bHaveAutoGeneratedLightmapUVSet = (LightmapUVActions->DestinationUVIndex >= InstanceUVs.GetNumChannels());
	}

	ExtractAttribList(Mesh, Mesh->VertexAttributes(), EAttributeEditorElementType::Vertex, this->VertexAttributes, AttributeProps->VertexAttributes);
	ExtractAttribList(Mesh, Mesh->VertexInstanceAttributes(), EAttributeEditorElementType::VertexInstance, this->InstanceAttributes, AttributeProps->InstanceAttributes);
	ExtractAttribList(Mesh, Mesh->TriangleAttributes(), EAttributeEditorElementType::Triangle, this->TriangleAttributes, AttributeProps->TriangleAttributes);
	ExtractAttribList(Mesh, Mesh->PolygonAttributes(), EAttributeEditorElementType::Polygon, this->PolygonAttributes, AttributeProps->PolygonAttributes);
	ExtractAttribList(Mesh, Mesh->EdgeAttributes(), EAttributeEditorElementType::Edge, this->EdgeAttributes, AttributeProps->EdgeAttributes);
	ExtractAttribList(Mesh, Mesh->PolygonGroupAttributes(), EAttributeEditorElementType::PolygonGroup, this->GroupAttributes, AttributeProps->GroupAttributes);

	ModifyAttributeProps->AttributeNamesList.Reset();
	CopyAttributeProps->FromAttribute.Reset();
	CopyAttributeProps->ToAttribute.Reset();

	//TArray<TArray<FAttributeEditorAttribInfo>*> AttribInfos = {
	//	&this->VertexAttributes, &this->InstanceAttributes,
	//	&this->TriangleAttributes, & this->PolygonAttributes,
	//	& this->EdgeAttributes, & this->GroupAttributes };
	TArray<TArray<FAttributeEditorAttribInfo>*> AttribInfos = {
		&this->VertexAttributes, &this->PolygonAttributes };

	for (TArray<FAttributeEditorAttribInfo>* AttribInfoList : AttribInfos)
	{
		for (FAttributeEditorAttribInfo& AttribInfo : *AttribInfoList)
		{
			//if (IsReservedName(AttribInfo.Name) == false)
			//{
				ModifyAttributeProps->AttributeNamesList.Add(AttribInfo.Name.ToString());
			//}
			//CopyAttributeProps->FromAttribute.Add(AttribInfo.Name);
			//CopyAttributeProps->ToAttribute.Add(AttribInfo.Name);
		}
	}

	bAttributeListsValid = true;
}



void UAttributeEditorTool::Shutdown(EToolShutdownType ShutdownType)
{
}


void UAttributeEditorTool::RequestAction(EAttributeEditorToolActions ActionType)
{
	if (PendingAction == EAttributeEditorToolActions::NoAction)
	{
		PendingAction = ActionType;
	}
}


void UAttributeEditorTool::OnTick(float DeltaTime)
{
	switch (PendingAction)
	{
	case EAttributeEditorToolActions::OptimizeForEditing:
		OptimizeForEditing();
		break;
	case EAttributeEditorToolActions::ClearNormals:
		ClearNormals();
		break;
	case EAttributeEditorToolActions::ClearAllUVs:
		ClearUVs();
		break;
	case EAttributeEditorToolActions::AddUVSet:
		AddUVSet();
		break;
	case EAttributeEditorToolActions::DeleteSelectedUVSet:
		DeleteSelectedUVSet();
		break;
	case EAttributeEditorToolActions::DuplicateSelectedUVSet:
		DuplicateSelectedUVSet();
		break;
	case EAttributeEditorToolActions::AddAttribute:
		AddNewAttribute();
		break;
	case EAttributeEditorToolActions::AddWeightMapLayer:
		AddNewWeightMap();
		break;
	case EAttributeEditorToolActions::AddPolyGroupLayer:
		AddNewGroupsLayer();
		break;
	case EAttributeEditorToolActions::DeleteAttribute:
		DeleteAttribute();
		break;

	case EAttributeEditorToolActions::EnableLightmapUVs:
		SetLightmapUVsEnabled(true);
		break;
	case EAttributeEditorToolActions::DisableLightmapUVs:
		SetLightmapUVsEnabled(false);
		break;
	case EAttributeEditorToolActions::ResetLightmapUVChannels:
		ResetLightmapUVsChannels();
		break;
	}
	PendingAction = EAttributeEditorToolActions::NoAction;

	if (bAttributeListsValid == false && Targets.Num() == 1)
	{
		InitializeAttributeLists();
	}
}




void UAttributeEditorTool::OptimizeForEditing()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("OptimizeForEditing", "Optimize For Editing"));

	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(TargetComponentInterface(ComponentIdx)->GetOwnerComponent());
		if (StaticMeshComponent == nullptr || StaticMeshComponent->GetStaticMesh() == nullptr)
		{
			continue;
		}

		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		StaticMesh->Modify();
		StaticMesh->SetNumSourceModels(1);		// discard extra source models
		FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
		BuildSettings.bGenerateLightmapUVs = false;
		BuildSettings.bBuildReversedIndexBuffer = false;
		BuildSettings.bRemoveDegenerates = false;

		// dramatically reduce distance field resolution to speed up editing
		BuildSettings.DistanceFieldResolutionScale = 0.01;

		// this will call StaticMesh->PostEditChange()...
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			TEdgeAttributesRef<bool> EdgeHardnesses = CommitParams.MeshDescriptionOut->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
			if (EdgeHardnesses.IsValid())
			{
				for (FEdgeID ElID : CommitParams.MeshDescriptionOut->Edges().GetElementIDs())
				{
					EdgeHardnesses[ElID] = false;
				}
			}

			// force computation of normals/tangents if they are auto-generated
			if (BuildSettings.bRecomputeNormals || BuildSettings.bRecomputeTangents)
			{
				UE::MeshDescription::InitializeAutoGeneratedAttributes(*CommitParams.MeshDescriptionOut, StaticMeshComponent, 0);
			}

			// now clear these build settings
			BuildSettings.bUseMikkTSpace = false;
			BuildSettings.bRecomputeNormals = false;
			BuildSettings.bRecomputeTangents = false;
		});

	}
	GetToolManager()->EndUndoTransaction();

	// update attrib lists
	bAttributeListsValid = false;
}


void UAttributeEditorTool::ClearNormals()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ClearNormalsTransactionMessage", "Clear Normals"));

	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			TEdgeAttributesRef<bool> EdgeHardnesses = CommitParams.MeshDescriptionOut->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
			if (EdgeHardnesses.IsValid())
			{
				for (FEdgeID ElID : CommitParams.MeshDescriptionOut->Edges().GetElementIDs())
				{
					EdgeHardnesses[ElID] = false;
				}
			}
			FStaticMeshOperations::ComputeTriangleTangentsAndNormals(*CommitParams.MeshDescriptionOut, FMathf::Epsilon);
			FStaticMeshOperations::RecomputeNormalsAndTangentsIfNeeded(*CommitParams.MeshDescriptionOut, EComputeNTBsFlags::WeightedNTBs | EComputeNTBsFlags::Normals);
		});
	}
	GetToolManager()->EndUndoTransaction();
}




void UAttributeEditorTool::ClearUVs()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ClearUVsTransactionMessage", "Clear Selected UVs"));
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
				CommitParams.MeshDescriptionOut->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			int32 NumChannels = InstanceUVs.GetNumChannels();
			const FVertexInstanceArray& Instances = CommitParams.MeshDescriptionOut->VertexInstances();
			for (int LayerIndex = NumChannels-1; LayerIndex >= 0; LayerIndex--)
			{
				if (!FStaticMeshOperations::RemoveUVChannel(*CommitParams.MeshDescriptionOut, LayerIndex))
				{
					for (const FVertexInstanceID& ElID : Instances.GetElementIDs())
					{
						InstanceUVs.Set(ElID, LayerIndex, FVector2D::ZeroVector);
					}
				}
			}

			if (bHaveAutoGeneratedLightmapUVSet)
			{
				UpdateAutoGeneratedLightmapUVChannel(TargetComponentInterface(ComponentIdx), InstanceUVs.GetNumChannels());
			}
		});
	}
	GetToolManager()->EndUndoTransaction();

	// update attrib lists
	bAttributeListsValid = false;
}




void UAttributeEditorTool::DeleteSelectedUVSet()
{
	int32 DeleteIndex = UVActions->UVLayerNamesList.IndexOfByKey(UVActions->UVLayer);
	if (DeleteIndex == INDEX_NONE)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("CannotFindUVSet", "Selected UV Set Not Found"), EToolMessageLevel::UserWarning);
		return;
	}
	if (DeleteIndex == 0 && UVActions->UVLayerNamesList.Num() == 1)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("CannotDeleteLastUVSet", "Cannot Delete Last UV Set. UVs will be cleared to Zero."), EToolMessageLevel::UserWarning);
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("ClearUVsTransactionMessage", "Clear Selected UVs"));
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
				CommitParams.MeshDescriptionOut->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			if (!FStaticMeshOperations::RemoveUVChannel(*CommitParams.MeshDescriptionOut, DeleteIndex))
			{
				const FVertexInstanceArray& Instances = CommitParams.MeshDescriptionOut->VertexInstances();
				for (const FVertexInstanceID& InstanceID : Instances.GetElementIDs())
				{
					InstanceUVs.Set(InstanceID, DeleteIndex, FVector2D::ZeroVector);
				}
			}

			if (bHaveAutoGeneratedLightmapUVSet)
			{
				UpdateAutoGeneratedLightmapUVChannel(TargetComponentInterface(ComponentIdx), InstanceUVs.GetNumChannels());
			}
		});
	}
	GetToolManager()->EndUndoTransaction();

	bAttributeListsValid = false;
}


void UAttributeEditorTool::AddUVSet()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AddUVSetMessage", "Add UV Set"));
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
				CommitParams.MeshDescriptionOut->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			int32 NewChannelIndex = InstanceUVs.GetNumChannels();
			if (!FStaticMeshOperations::AddUVChannel(*CommitParams.MeshDescriptionOut))
			{
				GetToolManager()->DisplayMessage(LOCTEXT("FailedToAddUVSet", "Adding UV Set Failed"), EToolMessageLevel::UserWarning);
			}
			else
			{
				GetToolManager()->DisplayMessage(FText::Format(LOCTEXT("AddedNewUVSet", "Added UV{0}"), FText::FromString(FString::FromInt(NewChannelIndex))), EToolMessageLevel::UserWarning);

				if (bHaveAutoGeneratedLightmapUVSet)
				{
					UpdateAutoGeneratedLightmapUVChannel(TargetComponentInterface(ComponentIdx), InstanceUVs.GetNumChannels());
				}
			}
		});
	}
	GetToolManager()->EndUndoTransaction();

	bAttributeListsValid = false;
}



void UAttributeEditorTool::DuplicateSelectedUVSet()
{
	int32 SourceIndex = UVActions->UVLayerNamesList.IndexOfByKey(UVActions->UVLayer);
	if (SourceIndex == INDEX_NONE)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("CannotFindUVSet", "Selected UV Set Not Found"), EToolMessageLevel::UserWarning);
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("DuplicateUVSetMessage", "Duplicate UV Set"));
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
				CommitParams.MeshDescriptionOut->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			int32 NewChannelIndex = InstanceUVs.GetNumChannels();
			if (!FStaticMeshOperations::AddUVChannel(*CommitParams.MeshDescriptionOut))
			{
				GetToolManager()->DisplayMessage(LOCTEXT("FailedToAddUVSet", "Adding UV Set Failed"), EToolMessageLevel::UserWarning);
			}
			else
			{
				const FVertexInstanceArray& Instances = CommitParams.MeshDescriptionOut->VertexInstances();
				for (const FVertexInstanceID& InstanceID : Instances.GetElementIDs())
				{
					FVector2D SourceUV = InstanceUVs.Get(InstanceID, SourceIndex);
					InstanceUVs.Set(InstanceID, NewChannelIndex, SourceUV);
				}

				if (bHaveAutoGeneratedLightmapUVSet)
				{
					UpdateAutoGeneratedLightmapUVChannel(TargetComponentInterface(ComponentIdx), InstanceUVs.GetNumChannels());
				}

				GetToolManager()->DisplayMessage(FText::Format(LOCTEXT("Copied UV Set", "Copied UV{0} to UV{1}"), 
					FText::FromString(FString::FromInt(SourceIndex)), FText::FromString(FString::FromInt(NewChannelIndex))), EToolMessageLevel::UserWarning);
			}
		});
	}
	GetToolManager()->EndUndoTransaction();

	bAttributeListsValid = false;
}





void UAttributeEditorTool::AddNewAttribute(EAttributeEditorElementType ElemType, EAttributeEditorAttribType DataType, FName AttributeName)
{
	if (AttributeName.IsNone())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidAttributeName", "Invalid attribute name"), EToolMessageLevel::UserWarning);
		return;
	}

	FMeshDescription* CurMesh = TargetMeshProviderInterface(0)->GetMeshDescription();
	if (HasAttribute(CurMesh, ElemType, AttributeName))
	{
		GetToolManager()->DisplayMessage(LOCTEXT("ErrorAddingDuplicateNameMessage", "Attribute with this name already exists"), EToolMessageLevel::UserWarning);
		return;
	}

	FMeshDescription NewMesh = *CurMesh;
	if (AddAttribute(&NewMesh, ElemType, DataType, AttributeName) == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("FailedAddingNewMessage", "Unknown error adding new Attribute"), EToolMessageLevel::UserWarning);
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("NewAttributeTransactionMessage", "Add Attribute"));
	TargetMeshCommitterInterface(0)->CommitMeshDescription([&NewMesh](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
	{
		*CommitParams.MeshDescriptionOut = NewMesh;
	});
	GetToolManager()->EndUndoTransaction();

	bAttributeListsValid = false;
}


void UAttributeEditorTool::AddNewAttribute()
{
	if (NewAttributeProps->DataType == EAttributeEditorAttribType::Unknown)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("ErrorAddingTypeMessage", "Currently cannot add this attribute type"), EToolMessageLevel::UserWarning);
		return;
	}

	AddNewAttribute(NewAttributeProps->ElementType, NewAttributeProps->DataType, FName(NewAttributeProps->NewName));
}

void UAttributeEditorTool::AddNewWeightMap()
{
	AddNewAttribute(EAttributeEditorElementType::Vertex, EAttributeEditorAttribType::Float, FName(NewAttributeProps->NewName));
}

void UAttributeEditorTool::AddNewGroupsLayer()
{
	AddNewAttribute(EAttributeEditorElementType::Triangle, EAttributeEditorAttribType::Int32, FName(NewAttributeProps->NewName));
}


void UAttributeEditorTool::ClearAttribute()
{
}

void UAttributeEditorTool::DeleteAttribute()
{
	FMeshDescription* CurMesh = TargetMeshProviderInterface(0)->GetMeshDescription();
	FName SelectedName(ModifyAttributeProps->Attribute);

	// We check on the skeletal mesh attributes because it is a superset of the static mesh
	// attributes.
	if (FSkeletalMeshAttributes::IsReservedAttributeName(SelectedName))
	{
		GetToolManager()->DisplayMessage(LOCTEXT("CannotDeleteReservedNameError", "Cannot delete reserved mesh Attributes"), EToolMessageLevel::UserWarning);
		return;
	}

	bool bIsVertex = HasAttribute(CurMesh, EAttributeEditorElementType::Vertex, SelectedName);
	bool bIsPoly = HasAttribute(CurMesh, EAttributeEditorElementType::PolygonGroup, SelectedName);
	if (bIsVertex == false && bIsPoly == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("CannotDeleteAttribError", "Cannot delete the selected attribute"), EToolMessageLevel::UserWarning);
		return;
	}

	EAttributeEditorElementType ElemType = (bIsVertex) ? EAttributeEditorElementType::Vertex : EAttributeEditorElementType::Polygon;

	FMeshDescription NewMesh = *CurMesh;
	if (RemoveAttribute(&NewMesh, ElemType, SelectedName) == false)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("FailedRemovingNewMessage", "Unknown error removing Attribute"), EToolMessageLevel::UserWarning);
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("RemoveAttributeTransactionMessage", "Remove Attribute"));
	TargetMeshCommitterInterface(0)->CommitMeshDescription([&NewMesh](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
	{
		*CommitParams.MeshDescriptionOut = NewMesh;
	});
	GetToolManager()->EndUndoTransaction();

	bAttributeListsValid = false;
}



void UAttributeEditorTool::SetLightmapUVsEnabled(bool bEnabled)
{
	if (bEnabled)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("EnableLightmapVUs", "Enable Lightmap UVs"));
	}
	else
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("DisableLightmapUVs", "Disable Lightmap UVs"));
	}
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(TargetComponentInterface(ComponentIdx)->GetOwnerComponent());
		if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh() != nullptr)
		{
			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			StaticMesh->Modify();
			FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
			BuildSettings.bGenerateLightmapUVs = bEnabled;

			StaticMesh->PostEditChange();
		}
	}
	GetToolManager()->EndUndoTransaction();

	// update attrib lists
	bAttributeListsValid = false;
}



void UAttributeEditorTool::ResetLightmapUVsChannels()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ResetLightmapUVs", "Reset Lightmap UVs"));
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TVertexInstanceAttributesRef<FVector2D> InstanceUVs = 
			TargetMeshProviderInterface(ComponentIdx)->GetMeshDescription()->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		int32 SetChannel = FMath::Max(InstanceUVs.GetNumChannels(), 1);

		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(TargetComponentInterface(ComponentIdx)->GetOwnerComponent());
		if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh() != nullptr)
		{
			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			StaticMesh->Modify();
			FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
			BuildSettings.SrcLightmapIndex = 0;
			BuildSettings.DstLightmapIndex = SetChannel;
			StaticMesh->PostEditChange();
		}
	}

	GetToolManager()->EndUndoTransaction();

	// update attrib lists
	bAttributeListsValid = false;
}



void UAttributeEditorTool::UpdateAutoGeneratedLightmapUVChannel(IPrimitiveComponentBackedTarget* Target, int32 NewMaxUVChannels)
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Target->GetOwnerComponent());
	if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh() != nullptr)
	{
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		StaticMesh->Modify();

		FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(0).BuildSettings;
		BuildSettings.DstLightmapIndex = NewMaxUVChannels;
	}
}







#undef LOCTEXT_NAMESPACE
