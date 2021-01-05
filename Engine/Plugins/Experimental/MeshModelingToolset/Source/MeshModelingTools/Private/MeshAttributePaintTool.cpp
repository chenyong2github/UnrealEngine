// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshAttributePaintTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "Drawing/MeshDebugDrawing.h"
#include "ToolSetupUtil.h"

#include "MeshDescription.h"


#define LOCTEXT_NAMESPACE "UMeshAttributePaintTool"



class FMeshDescriptionVertexAttributeAdapter : public IMeshVertexAttributeAdapter
{
public:
	FMeshDescription* Mesh;
	FName AttributeName;
	TVertexAttributesRef<float> Attribute;

	FMeshDescriptionVertexAttributeAdapter(FMeshDescription* MeshIn, FName AttribNameIn, TVertexAttributesRef<float> AttribIn)
		: Mesh(MeshIn), AttributeName(AttribNameIn), Attribute(AttribIn)
	{
	}

	virtual int32 ElementNum() const override
	{
		return Attribute.GetNumElements();
	}

	virtual float GetValue(int32 Index) const override
	{
		return Attribute.Get(FVertexID(Index));
	}

	virtual void SetValue(int32 Index, float Value) override
	{
		Attribute.Set(FVertexID(Index), Value);
	}

	virtual FInterval1f GetValueRange() override
	{
		return FInterval1f(0.0f, 1.0f);
	}
};





class FMeshDescriptionVertexAttributeSource : public IMeshVertexAttributeSource
{
public:
	FMeshDescription* Mesh = nullptr;

	FMeshDescriptionVertexAttributeSource(FMeshDescription* MeshIn)
	{
		Mesh = MeshIn;
	}

	virtual int32 GetAttributeElementNum() override
	{
		return Mesh->Vertices().Num();
	}

	virtual TArray<FName> GetAttributeList() override
	{
		TAttributesSet<FVertexID>& VertexAttribs = Mesh->VertexAttributes();
		TArray<FName> Result;

		VertexAttribs.ForEach([&](const FName AttributeName, auto AttributesRef)
		{
			if (VertexAttribs.HasAttributeOfType<float>(AttributeName))
			{
				Result.Add(AttributeName);
			}
		});
		return Result;
	}


	virtual TUniquePtr<IMeshVertexAttributeAdapter> GetAttribute(FName AttributeName) override
	{
		TAttributesSet<FVertexID>& VertexAttribs = Mesh->VertexAttributes();
		TVertexAttributesRef<float> Attrib = VertexAttribs.GetAttributesRef<float>(AttributeName);
		if (Attrib.IsValid())
		{
			return MakeUnique<FMeshDescriptionVertexAttributeAdapter>(Mesh, AttributeName, Attrib);

		}
		return nullptr;
	}

};





void UMeshAttributePaintEditActions::PostAction(EMeshAttributePaintToolActions Action)
{
	if (ParentTool.IsValid() && Cast<UMeshAttributePaintTool>(ParentTool))
	{
		Cast<UMeshAttributePaintTool>(ParentTool)->RequestAction(Action);
	}
}




/*
 * ToolBuilder
 */

UMeshSurfacePointTool* UMeshAttributePaintToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshAttributePaintTool* SelectionTool = NewObject<UMeshAttributePaintTool>(SceneState.ToolManager);
	SelectionTool->SetWorld(SceneState.World);
	SelectionTool->SetAssetAPI(AssetAPI);

	if (ColorMapFactory)
	{
		SelectionTool->SetColorMap(ColorMapFactory());
	}

	return SelectionTool;
}




void UMeshAttributePaintTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UMeshAttributePaintTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void UMeshAttributePaintTool::Setup()
{
	UDynamicMeshBrushTool::Setup();

	// hide strength and falloff
	BrushProperties->RestoreProperties(this);

	AttribProps = NewObject<UMeshAttributePaintToolProperties>(this);
	AttribProps->RestoreProperties(this);
	AddToolPropertySource(AttribProps);

	//AttributeEditActions = NewObject<UMeshAttributePaintEditActions>(this);
	//AttributeEditActions->Initialize(this);
	//AddToolPropertySource(AttributeEditActions);

	// configure preview mesh
	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);
	//PreviewMesh->EnableWireframe(SelectionProps->bShowWireframe);
	PreviewMesh->GetRootComponent()->bCastDynamicShadow = false;

	// enable vtx colors on preview mesh
	PreviewMesh->EditMesh([](FDynamicMesh3& Mesh)
	{
		Mesh.DiscardVertexColors();
		Mesh.EnableVertexColors(FVector3f::Zero());
	});

	// build octree
	VerticesOctree.Initialize(PreviewMesh->GetMesh(), true);

	UMaterialInterface* VtxColorMaterial = ToolSetupUtil::GetVertexColorMaterial(GetToolManager());
	if (VtxColorMaterial != nullptr)
	{
		PreviewMesh->SetOverrideRenderMaterial(VtxColorMaterial);
	}

	RecalculateBrushRadius();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartAttribPaint", "Paint per-vertex attribute maps. Ctrl to Erase/Subtract, Shift to Smooth"),
		EToolMessageLevel::UserNotification);

	ColorMapper = MakeUnique<FFloatAttributeColorMapper>();

	EditedMesh = MakeUnique<FMeshDescription>();
	*EditedMesh = *ComponentTarget->GetMesh();

	AttributeSource = MakeUnique<FMeshDescriptionVertexAttributeSource>(EditedMesh.Get());
	AttribProps->Attributes = AttributeSource->GetAttributeList();

	if (AttribProps->Attributes.Num() == 0)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("StartAttribPaintFailed", "No Float attributes exist for this mesh. Use the Attribute Editor to create one."),
			EToolMessageLevel::UserWarning);
	}

	InitializeAttributes();
	PendingNewSelectedIndex = 0;

	SelectedAttributeWatcher.Initialize([this]() { return AttribProps->SelectedAttribute; },
		[this](int32 NewValue) { PendingNewSelectedIndex = NewValue; }, AttribProps->SelectedAttribute);

	bVisibleAttributeValid = false;
}



void UMeshAttributePaintTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	UDynamicMeshBrushTool::RegisterActions(ActionSet);
}




void UMeshAttributePaintTool::RequestAction(EMeshAttributePaintToolActions ActionType)
{
	if (bHavePendingAction)
	{
		return;
	}
	PendingAction = ActionType;
	bHavePendingAction = true;
}


void UMeshAttributePaintTool::SetColorMap(TUniquePtr<FFloatAttributeColorMapper> ColorMap)
{
	ColorMapper = MoveTemp(ColorMap);
}


void UMeshAttributePaintTool::OnTick(float DeltaTime)
{
	SelectedAttributeWatcher.CheckAndUpdate();

	if (bStampPending)
	{
		ApplyStamp(LastStamp);
		bStampPending = false;
	}

	if (bHavePendingAction)
	{
		ApplyAction(PendingAction);
		bHavePendingAction = false;
		PendingAction = EMeshAttributePaintToolActions::NoAction;
	}

	if (PendingNewSelectedIndex >= 0)
	{
		UpdateSelectedAttribute(PendingNewSelectedIndex);
		PendingNewSelectedIndex = -1;
	}

	if (bVisibleAttributeValid == false)
	{
		UpdateVisibleAttribute();
		bVisibleAttributeValid = true;
	}
}






void UMeshAttributePaintTool::OnBeginDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnBeginDrag(WorldRay);

	PreviewBrushROI.Reset();
	if (IsInBrushStroke())
	{
		bInRemoveStroke = GetCtrlToggle();
		bInSmoothStroke = GetShiftToggle();
		BeginChange();
		StartStamp = UBaseBrushTool::LastBrushStamp;
		LastStamp = StartStamp;
		bStampPending = true;
	}
}



void UMeshAttributePaintTool::OnUpdateDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnUpdateDrag(WorldRay);
	if (IsInBrushStroke())
	{
		LastStamp = UBaseBrushTool::LastBrushStamp;
		bStampPending = true;
	}
}



void UMeshAttributePaintTool::OnEndDrag(const FRay& Ray)
{
	UDynamicMeshBrushTool::OnEndDrag(Ray);

	bInRemoveStroke = bInSmoothStroke = false;
	bStampPending = false;

	// close change record
	TUniquePtr<FMeshAttributePaintChange> Change = EndChange();
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AttributeValuesChange", "Paint"));
	GetToolManager()->EmitObjectChange(this, MoveTemp(Change), LOCTEXT("AttributeValuesChange", "Paint"));
	GetToolManager()->EndUndoTransaction();
}


bool UMeshAttributePaintTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UDynamicMeshBrushTool::OnUpdateHover(DevicePos);

	// todo get rid of this redundant hit test!
	FHitResult OutHit;
	if (UDynamicMeshBrushTool::HitTest(DevicePos.WorldRay, OutHit))
	{
		PreviewBrushROI.Reset();
		CalculateVertexROI(LastBrushStamp, PreviewBrushROI);
	}

	return true;
}



void UMeshAttributePaintTool::CalculateVertexROI(const FBrushStampData& Stamp, TArray<int>& VertexROI)
{
	FTransform3d Transform(ComponentTarget->GetWorldTransform());
	FVector3d StampPosLocal = Transform.InverseTransformPosition(Stamp.WorldPosition);

	float Radius = GetCurrentBrushRadiusLocal();
	float RadiusSqr = Radius * Radius;
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	FAxisAlignedBox3d QueryBox(StampPosLocal, Radius);
	VerticesOctree.RangeQuery(QueryBox,
		[&](int32 VertexID) { return Mesh->GetVertex(VertexID).DistanceSquared(StampPosLocal) < RadiusSqr; },
		VertexROI);
}








void UMeshAttributePaintTool::InitializeAttributes()
{
	AttributeBufferCount = AttributeSource->GetAttributeElementNum();
	TArray<FName> AttributeNames = AttributeSource->GetAttributeList();

	Attributes.SetNum(AttributeNames.Num());
	for (int32 k = 0; k < AttributeNames.Num(); ++k)
	{
		Attributes[k].Name = AttributeNames[k];
		Attributes[k].Attribute = AttributeSource->GetAttribute(AttributeNames[k]);
		Attributes[k].CurrentValues.SetNum(AttributeBufferCount);
		for (int32 i = 0; i < AttributeBufferCount; ++i)
		{
			Attributes[k].CurrentValues[i] = Attributes[k].Attribute->GetValue(i);
		}
		Attributes[k].InitialValues = Attributes[k].CurrentValues;
	}

	CurrentAttributeIndex = -1;
	PendingNewSelectedIndex = -1;
}




void UMeshAttributePaintTool::StoreCurrentAttribute()
{
	if (CurrentAttributeIndex >= 0)
	{
		FAttributeData& AttribData = Attributes[CurrentAttributeIndex];
		for (int32 k = 0; k < AttributeBufferCount; ++k)
		{
			AttribData.Attribute->SetValue(k, AttribData.CurrentValues[k]);
		}
		CurrentAttributeIndex = -1;
		CurrentValueRange = FInterval1f(0.0f, 1.0f);
	}
}


void UMeshAttributePaintTool::UpdateVisibleAttribute()
{
	// copy current value set back to attribute  (should we just always be doing this??)
	StoreCurrentAttribute();

	CurrentAttributeIndex = AttribProps->SelectedAttribute;

	if (CurrentAttributeIndex >= 0)
	{
		FAttributeData& AttribData = Attributes[CurrentAttributeIndex];
		CurrentValueRange = AttribData.Attribute->GetValueRange();

		// update mesh with new value colors
		PreviewMesh->EditMesh([&](FDynamicMesh3& Mesh)
		{
			for (int32 vid : Mesh.VertexIndicesItr())
			{
				float Value = AttribData.CurrentValues[vid];
				FVector3f Color = ColorMapper->ToColor(Value);
				Mesh.SetVertexColor(vid, Color);
			}
		});

		AttribProps->AttributeName = AttribData.Name.ToString();
	}
}











double UMeshAttributePaintTool::CalculateBrushFalloff(double Distance)
{
	double f = FMathd::Clamp(1.0 - BrushProperties->BrushFalloffAmount, 0.0, 1.0);
	double d = Distance / GetCurrentBrushRadiusLocal();
	double w = 1;
	if (d > f)
	{
		d = FMathd::Clamp((d - f) / (1.0 - f), 0.0, 1.0);
		w = (1.0 - d * d);
		w = w * w * w;
	}
	return w;
}



void UMeshAttributePaintTool::ApplyStamp(const FBrushStampData& Stamp)
{
	if (CurrentAttributeIndex < 0)
	{
		return;
	}

	FTransform3d Transform(ComponentTarget->GetWorldTransform());
	FVector3d StampPosLocal = Transform.InverseTransformPosition(Stamp.WorldPosition);

	TArray<int32> ROIVertices;
	CalculateVertexROI(Stamp, ROIVertices);
	int NumVertices = ROIVertices.Num();

	TArray<float> ROIBefore, ROIAfter;
	ROIBefore.SetNum(NumVertices);
	ROIAfter.SetNum(NumVertices);

	FAttributeData& AttribData = Attributes[CurrentAttributeIndex];

	const FDynamicMesh3* CurrentMesh = PreviewMesh->GetMesh();
	if (bInSmoothStroke)
	{
		float SmoothSpeed = 0.25f;

		for (int32 k = 0; k < NumVertices; ++k)
		{
			int32 vid = ROIVertices[k];
			FVector3d Position = CurrentMesh->GetVertex(vid);
			float ValueSum = 0, WeightSum = 0;
			for (int32 NbrVID : CurrentMesh->VtxVerticesItr(vid))
			{
				FVector3d NbrPos = CurrentMesh->GetVertex(NbrVID);
				float Weight = FMathf::Clamp(1.0f / NbrPos.DistanceSquared(Position), 0.0001f, 1000.0f);
				ValueSum += Weight * AttribData.CurrentValues[NbrVID];
				WeightSum += Weight;
			}
			ValueSum /= WeightSum;

			float Falloff = (float)CalculateBrushFalloff(Position.Distance(StampPosLocal));
			float NewValue = FMathf::Lerp(AttribData.CurrentValues[vid], ValueSum, SmoothSpeed*Falloff);

			ROIBefore[k] = AttribData.CurrentValues[vid];
			ROIAfter[k] = CurrentValueRange.Clamp(NewValue);
		}
	}
	else
	{
		bool bInvert = bInRemoveStroke;
		float Sign = (bInvert) ? -1.0f : 1.0f;
		float UseStrength = Sign * BrushProperties->BrushStrength * CurrentValueRange.Length();
		for (int32 k = 0; k < NumVertices; ++k)
		{
			int32 vid = ROIVertices[k];
			FVector3d Position = CurrentMesh->GetVertex(vid);
			float Falloff = (float)CalculateBrushFalloff(Position.Distance(StampPosLocal));
			ROIBefore[k] = AttribData.CurrentValues[vid];
			ROIAfter[k] = CurrentValueRange.Clamp(ROIBefore[k] + UseStrength * Falloff);
		}
	}

	// track changes
	if (ActiveChangeBuilder)
	{
		ActiveChangeBuilder->UpdateValues(ROIVertices, ROIBefore, ROIAfter);
	}

	// update values and colors
	PreviewMesh->DeferredEditMesh([&](FDynamicMesh3& Mesh)
	{
		for (int32 k = 0; k < NumVertices; ++k)
		{
			int32 vid = ROIVertices[k];
			AttribData.CurrentValues[vid] = ROIAfter[k];
			FVector3f NewColor = ColorMapper->ToColor(ROIAfter[k]);
			Mesh.SetVertexColor(vid, NewColor);
		}
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexColors, false);
}




void UMeshAttributePaintTool::ApplyAction(EMeshAttributePaintToolActions ActionType)
{
	//switch (ActionType)
	//{
	//}
}



void UMeshAttributePaintTool::UpdateSelectedAttribute(int32 NewSelectedIndex)
{
	AttribProps->SelectedAttribute = 0;
	AttribProps->Attributes = AttributeSource->GetAttributeList();
	AttribProps->SelectedAttribute = FMath::Clamp(NewSelectedIndex, 0, AttribProps->Attributes.Num() - 1);
	bVisibleAttributeValid = false;
}




void UMeshAttributePaintTool::OnShutdown(EToolShutdownType ShutdownType)
{
	BrushProperties->SaveProperties(this);

	StoreCurrentAttribute();

	if (ShutdownType == EToolShutdownType::Accept)
	{
		// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshAttributePaintTool", "Edit Attributes"));

		ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			*CommitParams.MeshDescription = *EditedMesh;
		});
		GetToolManager()->EndUndoTransaction();
	}
}





void UMeshAttributePaintTool::BeginChange()
{
	if (! ActiveChangeBuilder)
	{
		ActiveChangeBuilder = MakeUnique<TIndexedValuesChangeBuilder<float, FMeshAttributePaintChange>>();
	}
	ActiveChangeBuilder->BeginNewChange();
	ActiveChangeBuilder->Change->CustomData = CurrentAttributeIndex;
}


TUniquePtr<FMeshAttributePaintChange> UMeshAttributePaintTool::EndChange()
{
	TUniquePtr<FMeshAttributePaintChange> Result = ActiveChangeBuilder->ExtractResult();
	
	Result->ApplyFunction = [](UObject* Object, const int32& AttribIndex, const TArray<int32>& Indices, const TArray<float>& Values)
	{
		UMeshAttributePaintTool* Tool = CastChecked<UMeshAttributePaintTool>(Object);
		Tool->ExternalUpdateValues(AttribIndex, Indices, Values);
	};
	Result->RevertFunction = [](UObject* Object, const int32& AttribIndex, const TArray<int32>& Indices, const TArray<float>& Values)
	{
		UMeshAttributePaintTool* Tool = CastChecked<UMeshAttributePaintTool>(Object);
		Tool->ExternalUpdateValues(AttribIndex, Indices, Values);
	};

	return MoveTemp(Result);
}



void UMeshAttributePaintTool::ExternalUpdateValues(int32 AttribIndex, const TArray<int32>& VertexIndices, const TArray<float>& NewValues)
{
	check(Attributes.IsValidIndex(AttribIndex));
	FAttributeData& AttribData = Attributes[AttribIndex];

	int32 NumV = VertexIndices.Num();
	for (int32 k = 0; k < NumV; ++k)
	{
		AttribData.CurrentValues[VertexIndices[k]] = NewValues[k];
	}

	if (AttribIndex == CurrentAttributeIndex)
	{
		PreviewMesh->EditMesh([&](FDynamicMesh3& Mesh)
		{
			for (int32 vid : VertexIndices)
			{
				FVector3f NewColor = ColorMapper->ToColor(AttribData.CurrentValues[vid]);
				Mesh.SetVertexColor(vid, NewColor);
			}
		});
	}
}




#undef LOCTEXT_NAMESPACE
