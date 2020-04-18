// Copyright Epic Games, Inc. All Rights Reserved.

#include "AttributeEditorTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"
#include "MathUtil.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

#include "Components/PrimitiveComponent.h"


#define LOCTEXT_NAMESPACE "UAttributeEditorTool"


/*
 * ToolBuilder
 */


bool UAttributeEditorToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* UAttributeEditorToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UAttributeEditorTool* NewTool = NewObject<UAttributeEditorTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
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

	int NumUVLayers = 1;
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		FMeshDescription* MeshDescription = ComponentTarget->GetMesh();
		TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
			MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
		NumUVLayers = FMath::Max(NumUVLayers, InstanceUVs.GetNumIndices());
	}

	NormalsActions = NewObject<UAttributeEditorNormalsActions>(this);
	NormalsActions->Initialize(this);
	AddToolPropertySource(NormalsActions);

	UVActions = NewObject<UAttributeEditorUVActions>(this);
	UVActions->Initialize(this);
	AddToolPropertySource(UVActions);
	UVActions->NumUVLayers = NumUVLayers;

	if (ComponentTargets.Num() == 1)
	{
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


static bool IsReservedName(FName AttributeName)
{
	return (AttributeName == MeshAttribute::Vertex::Position)
		|| (AttributeName == MeshAttribute::Vertex::CornerSharpness)
		|| (AttributeName == MeshAttribute::VertexInstance::TextureCoordinate)
		|| (AttributeName == MeshAttribute::VertexInstance::Normal)
		|| (AttributeName == MeshAttribute::VertexInstance::Tangent)
		|| (AttributeName == MeshAttribute::VertexInstance::BinormalSign)
		|| (AttributeName == MeshAttribute::VertexInstance::Color)
		|| (AttributeName == MeshAttribute::Edge::IsHard)
		|| (AttributeName == MeshAttribute::Edge::IsUVSeam)
		|| (AttributeName == MeshAttribute::Edge::CreaseSharpness)
		|| (AttributeName == MeshAttribute::Polygon::Normal)
		|| (AttributeName == MeshAttribute::Polygon::Tangent)
		|| (AttributeName == MeshAttribute::Polygon::Binormal)
		|| (AttributeName == MeshAttribute::Polygon::Center)
		|| (AttributeName == MeshAttribute::PolygonGroup::ImportedMaterialSlotName)
		|| (AttributeName == MeshAttribute::PolygonGroup::EnableCollision)
		|| (AttributeName == MeshAttribute::PolygonGroup::CastShadow);
}


void UAttributeEditorTool::InitializeAttributeLists()
{
	FMeshDescription* Mesh = ComponentTargets[0]->GetMesh();

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
	case EAttributeEditorToolActions::ClearNormals:
		ClearNormals();
		break;
	case EAttributeEditorToolActions::ClearSelectedUVs:
		ClearUVs(true);
		break;
	case EAttributeEditorToolActions::ClearAllUVs:
		ClearUVs(false);
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
	}
	PendingAction = EAttributeEditorToolActions::NoAction;

	if (bAttributeListsValid == false && ComponentTargets.Num() == 1)
	{
		InitializeAttributeLists();
	}
}



void UAttributeEditorTool::ClearNormals()
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ClearNormalsTransactionMessage", "Clear Normals"));

	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		Target->CommitMesh([&](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			TEdgeAttributesRef<bool> EdgeHardnesses = CommitParams.MeshDescription->EdgeAttributes().GetAttributesRef<bool>(MeshAttribute::Edge::IsHard);
			if (EdgeHardnesses.IsValid())
			{
				for (FEdgeID ElID : CommitParams.MeshDescription->Edges().GetElementIDs())
				{
					EdgeHardnesses[ElID] = false;
				}
			}
			FStaticMeshOperations::ComputePolygonTangentsAndNormals(*CommitParams.MeshDescription, FMathf::Epsilon);
			FStaticMeshOperations::RecomputeNormalsAndTangentsIfNeeded(*CommitParams.MeshDescription, EComputeNTBsFlags::WeightedNTBs | EComputeNTBsFlags::Normals);
		});
	}
	GetToolManager()->EndUndoTransaction();
}




void UAttributeEditorTool::ClearUVs(bool bSelectedOnly)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ClearUVsTransactionMessage", "Clear Selected UVs"));
	for (TUniquePtr<FPrimitiveComponentTarget>& Target : ComponentTargets)
	{
		Target->CommitMesh([&](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			bool RemoveLayers[8] =
			{
				UVActions->bClearUVLayer0,
				UVActions->bClearUVLayer1,
				UVActions->bClearUVLayer2,
				UVActions->bClearUVLayer3,
				UVActions->bClearUVLayer4,
				UVActions->bClearUVLayer5,
				UVActions->bClearUVLayer6,
				UVActions->bClearUVLayer7
			};

			TVertexInstanceAttributesRef<FVector2D> InstanceUVs =
				CommitParams.MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
			const FVertexInstanceArray& Instances = CommitParams.MeshDescription->VertexInstances();
			for (int LayerIndex = 7; LayerIndex >= 0; LayerIndex--)
			{
				bool bRemove = (bSelectedOnly == false) || RemoveLayers[LayerIndex];
				if (bRemove && LayerIndex < InstanceUVs.GetNumIndices())
				{
					if (!FStaticMeshOperations::RemoveUVChannel(*CommitParams.MeshDescription, LayerIndex))
					{
						for (const FVertexInstanceID& ElID : Instances.GetElementIDs())
						{
							InstanceUVs.Set(ElID, LayerIndex, FVector2D::ZeroVector);
						}
					}
				}
			}
		});
	}
	GetToolManager()->EndUndoTransaction();
}





void UAttributeEditorTool::AddNewAttribute(EAttributeEditorElementType ElemType, EAttributeEditorAttribType DataType, FName AttributeName)
{
	if (AttributeName.IsNone())
	{
		GetToolManager()->DisplayMessage(LOCTEXT("InvalidAttributeName", "Invalid attribute name"), EToolMessageLevel::UserWarning);
		return;
	}

	FMeshDescription* CurMesh = ComponentTargets[0]->GetMesh();
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
	ComponentTargets[0]->CommitMesh([&NewMesh](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
	{
		*CommitParams.MeshDescription = NewMesh;
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
	AddNewAttribute(EAttributeEditorElementType::Polygon, EAttributeEditorAttribType::Int32, FName(NewAttributeProps->NewName));
}


void UAttributeEditorTool::ClearAttribute()
{
}

void UAttributeEditorTool::DeleteAttribute()
{
	FMeshDescription* CurMesh = ComponentTargets[0]->GetMesh();
	FName SelectedName(ModifyAttributeProps->Attribute);

	if (IsReservedName(SelectedName))
	{
		GetToolManager()->DisplayMessage(LOCTEXT("CannotDeleteReservedNameError", "Cannot delete required mesh Attributes"), EToolMessageLevel::UserWarning);
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
	ComponentTargets[0]->CommitMesh([&NewMesh](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
	{
		*CommitParams.MeshDescription = NewMesh;
	});
	GetToolManager()->EndUndoTransaction();

	bAttributeListsValid = false;
}







#undef LOCTEXT_NAMESPACE
