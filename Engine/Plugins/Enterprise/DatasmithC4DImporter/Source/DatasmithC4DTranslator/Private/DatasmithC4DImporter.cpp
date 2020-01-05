// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef _MELANGE_SDK_

#include "DatasmithC4DImporter.h"

#include "DatasmithAssetImportData.h"
#include "DatasmithC4DExtraMelangeDefinitions.h"
#include "DatasmithC4DImportException.h"
#include "DatasmithC4DImportOptions.h"
#include "DatasmithC4DTranslatorModule.h"
#include "DatasmithC4DUtils.h"
#include "DatasmithDefinitions.h"
#include "DatasmithImportOptions.h"
#include "DatasmithLightImporter.h"
#include "DatasmithMaterialExpressions.h"
#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneExporter.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithImporterUtils.h"

#include "AssetRegistryModule.h"
#include "AssetSelection.h"
#include "Curves/RichCurve.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Math/Matrix.h"
#include "Math/Transform.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "RawMesh.h"
#include "StaticMeshAttributes.h"

#include "ImathMatrixAlgo.h"

DECLARE_CYCLE_STAT(TEXT("C4DImporter - Load File"), STAT_C4DImporter_LoadFile, STATGROUP_C4DImporter);

DEFINE_LOG_CATEGORY(LogDatasmithC4DImport);

#define LOCTEXT_NAMESPACE "DatasmithC4DImportPlugin"

// What we multiply the C4D light brightness values with when the lights are not
// using photometric units. Those are chosen so that 100% brightness C4D point lights matches the
// default value of 8 candelas of UE4 point lights, and 100% brightness C4D infinite lights matches
// the default 10 lux of UE4 directional lights
#define UnitlessGlobalLightIntensity 10.0
#define UnitlessIESandPointLightIntensity 8.0

FDatasmithC4DImporter::FDatasmithC4DImporter(TSharedRef<IDatasmithScene>& OutScene, UDatasmithC4DImportOptions* InOptions)
	: Options(InOptions)
	, DatasmithScene(OutScene)
{
	check(Options);
}

FDatasmithC4DImporter::~FDatasmithC4DImporter()
{
	using namespace melange;

	if (C4dDocument != nullptr)
	{
		DeleteObj(C4dDocument);
		C4dDocument = nullptr;
	}
}

void FDatasmithC4DImporter::SetImportOptions(UDatasmithC4DImportOptions* InOptions)
{
	Options = InOptions;
}

namespace
{
	FMD5Hash ComputePolygonDataHash(melange::PolygonObject* PolyObject)
	{
		melange::Int32 PointCount = PolyObject->GetPointCount();
		melange::Int32 PolygonCount = PolyObject->GetPolygonCount();
		const melange::Vector* Points = PolyObject->GetPointR();
		const melange::CPolygon* Polygons = PolyObject->GetPolygonR();
		melange::Vector32* Normals = PolyObject->CreatePhongNormals();
		melange::GeData Data;

		FMD5 MD5;
		MD5.Update(reinterpret_cast<const uint8*>(Points), sizeof(melange::Vector)*PointCount);
		MD5.Update(reinterpret_cast<const uint8*>(Polygons), sizeof(melange::CPolygon)*PolygonCount);
		if (Normals)
		{
			MD5.Update(reinterpret_cast<const uint8*>(Normals), sizeof(melange::Vector32)*PointCount);
			melange::DeleteMem(Normals);
		}

		//Tags
		for (melange::BaseTag* Tag = PolyObject->GetFirstTag(); Tag; Tag = Tag->GetNext())
		{
			melange::Int32 TagType = Tag->GetType();
			if (TagType == Tuvw)
			{
				melange::ConstUVWHandle UVWHandle = static_cast<melange::UVWTag*>(Tag)->GetDataAddressR();
				for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
				{
					melange::UVWStruct UVWStruct;
					melange::UVWTag::Get(UVWHandle, PolygonIndex, UVWStruct);
					MD5.Update(reinterpret_cast<const uint8*>(&UVWStruct), sizeof(melange::UVWStruct));
				}
			}
			else if (TagType == Tpolygonselection)
			{
				melange::SelectionTag* SelectionTag = static_cast<melange::SelectionTag*>(Tag);
				melange::BaseSelect* BaseSelect = SelectionTag->GetBaseSelect();

				FString SelectionName = MelangeGetString(SelectionTag, melange::POLYGONSELECTIONTAG_NAME);
				uint32 NameHash = GetTypeHash(SelectionName);
				MD5.Update(reinterpret_cast<const uint8*>(&NameHash), sizeof(NameHash));

				TArray<melange::Int32> PolygonSelections;
				PolygonSelections.Reserve(BaseSelect->GetCount());

				melange::Int32 Segment = 0;
				melange::Int32 RangeStart = 0;
				melange::Int32 RangeEnd = 0;
				melange::Int32 Selection = 0;
				while (BaseSelect->GetRange(Segment++, &RangeStart, &RangeEnd))
				{
					for (Selection = RangeStart; Selection <= RangeEnd; ++Selection)
					{
						PolygonSelections.Add(Selection);
					}
				}
				MD5.Update(reinterpret_cast<const uint8*>(PolygonSelections.GetData()), PolygonSelections.Num() * sizeof(melange::Int32));
			}
		}

		FMD5Hash Result;
		Result.Set(MD5);
		return Result;
	}
}

// In C4D the CraneCamera is an object with many attributes that can be manipulated like a
// real-life crane camera. This describes all of its controllable attributes.
// Angles are in degrees, distances in cm. These correspond to the C4D coordinate system
// TODO: Add support Link/Target attributes
struct FCraneCameraAttributes
{
	float BaseHeight = 75.0f;
	float BaseHeading = 0.0f;
	float ArmLength = 300.0f;
	float ArmPitch = 30.0f;
	float HeadHeight = 50.0f;
	float HeadHeading = 0.0f;
	float HeadWidth = 35.0f;
	float CamPitch = 0.0f;
	float CamBanking = 0.0f;
	float CamOffset = 25.0f;
	bool bCompensatePitch = true;
	bool bCompensateHeading = false;

	// Sets one of the attributes using the IDs defined in DatasmithC4DExtraMelangeDefinitions.h
	// Expects the value to be in radians, cm or true/false, depending on attribute
	void SetAttributeByID(int32 AttributeID, double AttributeValue)
	{
		switch (AttributeID)
		{
		case melange::CRANECAMERA_BASE_HEIGHT:
			BaseHeight = (float)AttributeValue;
			break;
		case melange::CRANECAMERA_BASE_HEADING:
			BaseHeading = (float)FMath::RadiansToDegrees(AttributeValue);
			break;
		case melange::CRANECAMERA_ARM_LENGTH:
			ArmLength = (float)AttributeValue;
			break;
		case melange::CRANECAMERA_ARM_PITCH:
			ArmPitch = (float)FMath::RadiansToDegrees(AttributeValue);
			break;
		case melange::CRANECAMERA_HEAD_HEIGHT:
			HeadHeight = (float)AttributeValue;
			break;
		case melange::CRANECAMERA_HEAD_HEADING:
			HeadHeading = (float)FMath::RadiansToDegrees(AttributeValue);
			break;
		case melange::CRANECAMERA_HEAD_WIDTH:
			HeadWidth = (float)AttributeValue;
			break;
		case melange::CRANECAMERA_CAM_PITCH:
			CamPitch = (float)FMath::RadiansToDegrees(AttributeValue);
			break;
		case melange::CRANECAMERA_CAM_BANKING:
			CamBanking = (float)FMath::RadiansToDegrees(AttributeValue);
			break;
		case melange::CRANECAMERA_CAM_OFFSET:
			CamOffset = (float)AttributeValue;
			break;
		case melange::CRANECAMERA_COMPENSATE_PITCH:
			bCompensatePitch = static_cast<bool>(AttributeValue);
			break;
		case melange::CRANECAMERA_COMPENSATE_HEADING:
			bCompensateHeading = static_cast<bool>(AttributeValue);
			break;
		default:
			break;
		}
	}
};

// Extracts all of the relevant parameters from a Tcrane tag and packs them in a FCraneCameraAttributes
TSharedRef<FCraneCameraAttributes> ExtractCraneCameraAttributes(melange::BaseTag* CraneTag)
{
	TSharedRef<FCraneCameraAttributes> Result = MakeShared<FCraneCameraAttributes>();

	melange::GeData Data;
	if (CraneTag->GetParameter(melange::CRANECAMERA_BASE_HEIGHT, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_BASE_HEIGHT, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_BASE_HEADING, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_BASE_HEADING, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_ARM_LENGTH, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_ARM_LENGTH, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_ARM_PITCH, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_ARM_PITCH, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_HEAD_HEIGHT, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_HEAD_HEIGHT, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_HEAD_HEADING, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_HEAD_HEADING, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_HEAD_WIDTH, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_HEAD_WIDTH, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_CAM_PITCH, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_CAM_PITCH, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_CAM_BANKING, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_CAM_BANKING, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_CAM_OFFSET, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_CAM_OFFSET, Data.GetFloat());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_COMPENSATE_PITCH, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_COMPENSATE_PITCH, Data.GetInt32());
	}
	if (CraneTag->GetParameter(melange::CRANECAMERA_COMPENSATE_HEADING, Data))
	{
		Result->SetAttributeByID(melange::CRANECAMERA_COMPENSATE_HEADING, Data.GetInt32());
	}
	return Result;
}

// Composes the effect of the CraneCamera attributes into a single transform in the Melange
// coordinate system
FTransform CalculateCraneCameraTransform(const FCraneCameraAttributes& Params)
{
	// We will first construct a transformation in the UE4 coordinate system, as that is
	// easier to visualize and test

	// Local rotation of 90deg around the Y axis in Melange.
	// Will compensate the difference in convention between UE4 (camera shoots out the +X) and
	// C4D (camera shoots out the +Z)
	FTransform Conv = FTransform(FRotator(0, -90, 0), FVector(0, 0, 0));

	// Note: FRotator constructor is Pitch, Yaw and Roll (i.e. Y, Z, X), and these
	// are wrt a camera rotated 90 degrees due to Conv, so a roll will become a pitch, etc
	FTransform Cam  = FTransform(FRotator(0, 0, 0), FVector(0, -Params.CamOffset, 0)) *
					  FTransform(FRotator(-Params.CamBanking, 0, 0), FVector(0, 0, 0)) *
					  FTransform(FRotator(0, 0, Params.CamPitch), FVector(0, 0, 0));

	FTransform Head = FTransform(FRotator(0, 0, 0), FVector(Params.HeadWidth, 0, 0)) *
					  FTransform(FRotator(0, -Params.HeadHeading, 0), FVector(0, 0, 0)) *
					  FTransform(FRotator(0, 0, 0), FVector(0, 0, -Params.HeadHeight));

	FTransform Arm  = FTransform(FRotator(0, 0, 0), FVector(0, -Params.ArmLength, 0)) *
		              FTransform(FRotator(0, 0, Params.ArmPitch), FVector(0, 0, 0));

	FTransform Base = FTransform(FRotator(0, Params.BaseHeading, 0), FVector(0, 0, 0)) *
		              FTransform(FRotator(0, 0, 0), FVector(0, 0, Params.BaseHeight));

	// With Compensate Pitch on, the camera rotates about the end of the arm
	// to compensate the arm pitch, so we need to apply a rotation to undo
	// the effects of the pitch before the arm is accounted for
	if (Params.bCompensatePitch)
	{
		Arm = FTransform(FRotator(0, 0, -Params.ArmPitch), FVector(0, 0, 0)) *
			  Arm;
	}

	// With Compensate Heading on, the camera rotates about the end of the arm
	// to compensate the base's heading, so we need to apply a rotation to undo
	// the effects of the heading before the arm is accounted for
	if (Params.bCompensateHeading)
	{
		Arm = FTransform(FRotator(0, -Params.BaseHeading, 0), FVector(0, 0, 0)) *
			  Arm;
	}

	FTransform FinalTransUE4 = Conv * Cam * Head * Arm * Base;
	FVector TranslationUE4 = FinalTransUE4.GetTranslation();
	FVector EulerUE4 = FinalTransUE4.GetRotation().Euler();

	// Convert FinalTransUE4 into the melange coordinate system, so that this can be treated
	// like the other types of animations in ImportAnimations.
	// More specifically, convert them so that that ConvertDirectionLeftHandedYup and
	// the conversion for Ocamera rotations gets them back into UE4's coordinate system
	// Note: Remember that FRotator's constructor is Pitch, Yaw and Roll (i.e. Y, Z, X)
	return FTransform(FRotator(EulerUE4.Y, EulerUE4.X, -EulerUE4.Z-90),
					  FVector(TranslationUE4.X, TranslationUE4.Z, -TranslationUE4.Y));
}

void FDatasmithC4DImporter::ImportSpline(melange::SplineObject* SplineActor)
{
	// ActorObject has fewer keys, but uses bezier control points
	// Cache has more keys generated by subdivision, should be parsed with linear interpolation
	melange::SplineObject* SplineCache = static_cast<melange::SplineObject*>(GetBestMelangeCache(SplineActor));

	if (SplineActor && SplineCache)
	{
		int32 NumPoints = SplineCache->GetPointCount();
		if (NumPoints < 2)
		{
			return;
		}

		TArray<FRichCurve>& XYZCurves = SplineCurves.FindOrAdd(SplineActor);
		XYZCurves.SetNum(3);
		FRichCurve& XCurve = XYZCurves[0];
		FRichCurve& YCurve = XYZCurves[1];
		FRichCurve& ZCurve = XYZCurves[2];

		float PercentageDenominator = (float)(NumPoints-1);

		// If the spline is closed we have to manually add a final key equal to the first
		if (SplineActor->GetIsClosed())
		{
			// The extra point we manually add will become 1.0
			++PercentageDenominator;
		}

		melange::Matrix Trans = SplineCache->GetMg();

		const melange::Vector* Points = SplineCache->GetPointR();
		for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			const melange::Vector& Point = Trans * Points[PointIndex];
			float Percent = PointIndex / PercentageDenominator;
			XCurve.AddKey(Percent, (float)Point.x);
			YCurve.AddKey(Percent, (float)Point.y);
			ZCurve.AddKey(Percent, (float)Point.z);
		}

		if (SplineActor->GetIsClosed())
		{
			const melange::Vector& FirstPoint = Trans * Points[0];
			XCurve.AddKey(1.0f, (float)FirstPoint.x);
			YCurve.AddKey(1.0f, (float)FirstPoint.y);
			ZCurve.AddKey(1.0f, (float)FirstPoint.z);
		}
	}
}

melange::BaseObject* FDatasmithC4DImporter::GetBestMelangeCache(melange::BaseObject* Object)
{
	if (Object == nullptr)
	{
		return nullptr;
	}

	//When primitives types (cube, cone, cylinder...) are exported with the "Save Project for Melange" option,
	//they will have a cache that represent their PolygonObject equivalent.
	melange::BaseObject* ObjectCache = Object->GetCache();

	//When the primitive has a deformer, the resulting PolygonObject will be in a sub-cache
	if (ObjectCache)
	{
		if (ObjectCache->GetDeformCache())
		{
			ObjectCache = ObjectCache->GetDeformCache();
		}
	}
	else
	{
		ObjectCache = Object->GetDeformCache();
	}

	if (ObjectCache)
	{
		CachesOriginalObject.Add(ObjectCache, Object);
	}

	return ObjectCache;
}

FString FDatasmithC4DImporter::MelangeObjectID(melange::BaseObject* Object)
{
	//Make sure that Object is not in a cache
	FString HierarchyPosition;
	bool InCache = false;
	melange::BaseObject* ParentObject = Object;
	while (ParentObject)
	{
		int ObjectHierarchyIndex = 0;
		melange::BaseObject* PrevObject = ParentObject->GetPred();
		while (PrevObject)
		{
			ObjectHierarchyIndex++;
			PrevObject = PrevObject->GetPred();
		}
		HierarchyPosition = "_" + FString::FromInt(ObjectHierarchyIndex) + HierarchyPosition;

		melange::BaseObject** OriginalObject = CachesOriginalObject.Find(ParentObject);
		if (OriginalObject)
		{
			InCache = true;
			Object = *OriginalObject;
			ParentObject = Object;
			HierarchyPosition = "_C" + HierarchyPosition;
		}
		else
		{
			ParentObject = ParentObject->GetUp();
		}
	}

	FString ObjectID = GetMelangeBaseList2dID(Object);
	if (InCache)
	{
		ObjectID += HierarchyPosition.Right(HierarchyPosition.Len() - HierarchyPosition.Find("_C", ESearchCase::CaseSensitive) - 2);
	}
	return ObjectID;
}

// Returns whether we can remove this actor when optimizing the actor hierarchy
bool CanRemoveActor(const TSharedPtr<IDatasmithActorElement>& Actor, const TSet<FString>& ActorNamesToKeep)
{
	if (Actor->IsA(EDatasmithElementType::Camera | EDatasmithElementType::Light))
	{
		return false;
	}

	if (Actor->IsA(EDatasmithElementType::StaticMeshActor))
	{
		TSharedPtr<IDatasmithMeshActorElement> MeshActor = StaticCastSharedPtr<IDatasmithMeshActorElement>(Actor);
		if (MeshActor->GetStaticMeshPathName() != FString())
		{
			return false;
		}
	}

	if (ActorNamesToKeep.Contains(FString(Actor->GetName())))
	{
		return false;
	}

	return true;
}

void RemoveEmptyActorsRecursive(TSharedPtr<IDatasmithActorElement>& Actor, const TSet<FString>& NamesOfActorsToKeep)
{
	// We can't access the parent of a IDatasmithActorElement, so we have to analyze children and remove grandchildren
	// This is also why we need a RootActor in the scene, or else we won't be able to analyze top-level actors
	for (int32 ChildIndex = Actor->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
	{
		// Have to recurse first or else we will also iterate on our grandchildren
		TSharedPtr<IDatasmithActorElement> Child = Actor->GetChild(ChildIndex);

		RemoveEmptyActorsRecursive(Child, NamesOfActorsToKeep);

		// Move grandchildren to children
		if (Child->GetChildrenCount() <= 1 && CanRemoveActor(Child, NamesOfActorsToKeep))
		{
			for (int32 GrandChildIndex = Child->GetChildrenCount() - 1; GrandChildIndex >= 0; --GrandChildIndex)
			{
				TSharedPtr<IDatasmithActorElement> GrandChild = Child->GetChild(GrandChildIndex);

				Child->RemoveChild(GrandChild);
				Actor->AddChild(GrandChild);
			}

			Actor->RemoveChild(Child);
		}
	}
}

void FDatasmithC4DImporter::RemoveEmptyActors()
{
	TSet<FString> NamesOfActorsToKeep;
	NamesOfActorsToKeep.Append(NamesOfCameraTargetActors);
	NamesOfActorsToKeep.Append(NamesOfAnimatedActors);

	for (int32 ActorIndex = 0; ActorIndex < DatasmithScene->GetActorsCount(); ++ActorIndex)
	{
		TSharedPtr<IDatasmithActorElement> Actor = DatasmithScene->GetActor(ActorIndex);
		RemoveEmptyActorsRecursive(Actor, NamesOfActorsToKeep);
	}
}

// For now, we can't remove parents of animated nodes because animations are stored wrt local coordinate
// system. If we optimized an otherwise useless intermediate node, we'd need to bake its transform into all animations of
// child nodes, which I'm not sure is the ideal behavior as imported animation curves would look very different
bool KeepParentsOfAnimatedNodes(const TSharedPtr<IDatasmithActorElement>& Actor, TSet<FString>& NamesOfAnimatedActors)
{
	bool bKeepThisNode = NamesOfAnimatedActors.Contains(Actor->GetName());

	for (int32 ChildIndex = 0; ChildIndex < Actor->GetChildrenCount(); ++ChildIndex)
	{
		bKeepThisNode |= KeepParentsOfAnimatedNodes(Actor->GetChild(ChildIndex), NamesOfAnimatedActors);
	}

	if (bKeepThisNode)
	{
		NamesOfAnimatedActors.Add(Actor->GetName());
	}

	return bKeepThisNode;
}

TSharedPtr<IDatasmithMetaDataElement> FDatasmithC4DImporter::CreateMetadataForActor(const IDatasmithActorElement& Actor)
{
	TSharedPtr<IDatasmithMetaDataElement>& Metadata = ActorMetadata.FindOrAdd(&Actor);
	if (!Metadata.IsValid())
	{
		Metadata = FDatasmithSceneFactory::CreateMetaData(Actor.GetName());
	}
	DatasmithScene->AddMetaData(Metadata);
	return Metadata;
}

namespace
{
	void AddMetadataVector(IDatasmithMetaDataElement* Metadata, const FString& Key, const FVector& Value)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*Key);
		MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Vector);
		MetadataPropertyPtr->SetValue(*Value.ToString());

		Metadata->AddProperty(MetadataPropertyPtr);
	}

	void AddMetadataColor(IDatasmithMetaDataElement* Metadata, const FString& Key, const FVector& Value)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*Key);
		MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Color);
		MetadataPropertyPtr->SetValue(*Value.ToString());

		Metadata->AddProperty(MetadataPropertyPtr);
	}

	void AddMetadataFloat(IDatasmithMetaDataElement* Metadata, const FString& Key, float Value)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*Key);
		MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
		MetadataPropertyPtr->SetValue(*LexToString(Value));

		Metadata->AddProperty(MetadataPropertyPtr);
	}

	void AddMetadataTexture(IDatasmithMetaDataElement* Metadata, const FString& Key, const FString& FilePath)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*Key);
		MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
		MetadataPropertyPtr->SetValue(*FilePath);

		Metadata->AddProperty(MetadataPropertyPtr);
	}

	void AddMetadataBool(IDatasmithMetaDataElement* Metadata, const FString& Key, bool bValue)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*Key);
		MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Bool);
		MetadataPropertyPtr->SetValue(bValue? TEXT("True") : TEXT("False"));

		Metadata->AddProperty(MetadataPropertyPtr);
	}

	void AddMetadataString(IDatasmithMetaDataElement* Metadata, const FString& Key, const FString& Value)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*Key);
		MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::String);
		MetadataPropertyPtr->SetValue(*Value);

		Metadata->AddProperty(MetadataPropertyPtr);
	}
}

void FDatasmithC4DImporter::AddChildActor(melange::BaseObject* Object, TSharedPtr<IDatasmithActorElement> ParentActor, melange::Matrix WorldTransformMatrix, const TSharedPtr<IDatasmithActorElement>& Actor)
{
	melange::DynamicDescription* DynamicDescription = Object->GetDynamicDescription();
	if (DynamicDescription)
	{
		TSharedPtr<IDatasmithMetaDataElement> Metadata = CreateMetadataForActor(*Actor);

		void* BrowserHandle = DynamicDescription->BrowseInit();
		melange::DescID DescId;
		const melange::BaseContainer* DescContainer;
		while (DynamicDescription->BrowseGetNext(BrowserHandle, &DescId, &DescContainer))
		{
			melange::Int32 UserDataType = DescContainer->GetInt32(21);
			melange::GeData Data;
			if (Object->GetParameter(DescId, Data) && Data.GetType() != melange::DA_NIL)
			{
				FString BaseDataName = MelangeStringToFString(DescContainer->GetString(1));
				FString DataName = BaseDataName;
				FString CurrentValue;
				int DataNameIndex = 0;
				if (UserDataType == melange::DA_VECTOR)
				{
					FVector ConvertedVector = ConvertMelangePosition(Data.GetVector());
					AddMetadataVector(Metadata.Get(), *DataName, ConvertedVector);
				}
				else if (UserDataType == melange::DA_REAL)
				{
					AddMetadataFloat(Metadata.Get(), *DataName, static_cast<float>(Data.GetFloat()));
				}
				else if (UserDataType == 1000492 /*color*/)
				{
					AddMetadataColor(Metadata.Get(), *DataName, MelangeVectorToFVector(Data.GetVector()));
				}
				else if (UserDataType == 1000484 /*texture*/)
				{
					AddMetadataTexture(Metadata.Get(), *DataName, *GeDataToString(Data));
				}
				else if (UserDataType == 400006001 /*boolean*/)
				{
					AddMetadataBool(Metadata.Get(), *DataName, Data.GetInt32() != 0);
				}
				else
				{
					FString ValueString = GeDataToString(Data);
					if (!ValueString.IsEmpty())
					{
						AddMetadataString(Metadata.Get(), *DataName, *ValueString);
					}
				}

			}
		}
		DynamicDescription->BrowseFree(BrowserHandle);
	}

	DatasmithC4DImportCheck(!NamesOfAllActors.Contains(Actor->GetName()));
	NamesOfAllActors.Add(Actor->GetName());

	ActorElementToC4DObject.Add(Actor.Get(), Object);

	if (Object->GetType() == Ocamera || Object->GetType() == Olight)
	{
		// Compensates the fact that in C4D cameras/lights shoot out towards +Z, while in
		// UE4 they shoot towards +X
		melange::Matrix CameraRotation(
			melange::Vector(0.0, 0.0, 0.0),
			melange::Vector(0.0, 0.0, 1.0),
			melange::Vector(0.0, 1.0, 0.0),
			melange::Vector(-1.0, 0.0, 0.0));
		WorldTransformMatrix = WorldTransformMatrix * CameraRotation;
	}

	// Convert to a float array so we can use Imath
	float FloatMatrix[16];
	FloatMatrix[0]  = static_cast<float>(WorldTransformMatrix.v1.x);
	FloatMatrix[1]  = static_cast<float>(WorldTransformMatrix.v1.y);
	FloatMatrix[2]  = static_cast<float>(WorldTransformMatrix.v1.z);
	FloatMatrix[3]  = 0;
	FloatMatrix[4]  = static_cast<float>(WorldTransformMatrix.v2.x);
	FloatMatrix[5]  = static_cast<float>(WorldTransformMatrix.v2.y);
	FloatMatrix[6]  = static_cast<float>(WorldTransformMatrix.v2.z);
	FloatMatrix[7]  = 0;
	FloatMatrix[8]  = static_cast<float>(WorldTransformMatrix.v3.x);
	FloatMatrix[9]  = static_cast<float>(WorldTransformMatrix.v3.y);
	FloatMatrix[10] = static_cast<float>(WorldTransformMatrix.v3.z);
	FloatMatrix[11] = 0;
	FloatMatrix[12] = static_cast<float>(WorldTransformMatrix.off.x);
	FloatMatrix[13] = static_cast<float>(WorldTransformMatrix.off.y);
	FloatMatrix[14] = static_cast<float>(WorldTransformMatrix.off.z);
	FloatMatrix[15] = 1.0;

	// We use Imath::extractAndRemoveScalingAndShear() because FMatrix::ExtractScaling() is deemed unreliable.
	// Set up a scaling and rotation matrix.
	Imath::Matrix44<float> Matrix(FloatMatrix[0], FloatMatrix[1], FloatMatrix[2],  0.0,
		FloatMatrix[4], FloatMatrix[5], FloatMatrix[6],  0.0,
		FloatMatrix[8], FloatMatrix[9], FloatMatrix[10], 0.0,
		0.0,            0.0,             0.0, 1.0);

	// Remove any scaling from the matrix and get the scale vector that was initially present.
	Imath::Vec3<float> Scale;
	Imath::Vec3<float> Shear;
	bool bExtracted = Imath::extractAndRemoveScalingAndShear<float>(Matrix, Scale, Shear, false);
	if (!bExtracted)
	{
		// TODO: Append a message to the build summary.
		FString Msg = FString::Printf(TEXT("WARNING: Actor %ls (%ls) has some zero scaling"), Actor->GetName(), Actor->GetLabel());
		return;
	}

	// Initialize a rotation quaternion with the rotation matrix.
	Imath::Quat<float> Quaternion = Imath::extractQuat<float>(Matrix);

	// Switch Z and Y axes for the scale due to coordinate system conversions
	FVector WorldScale = FVector(Scale.x, Scale.z, Scale.y);

	// Convert the left-handed Y-up coordinate rotation into an Unreal left-handed Z-up coordinate rotation.
	// This is done by doing a 90 degree rotation about the X axis.
	float Y = Quaternion.v.y;
	float Z = Quaternion.v.z;
	Quaternion.v.y = -Z;
	Quaternion.v.z =  Y;
	Quaternion.normalize();

	// Make sure Unreal will be able to handle the rotation quaternion.
	float              Angle = Quaternion.angle();
	Imath::Vec3<float> Axis  = Quaternion.axis();
	FQuat WorldRotation = FQuat(FVector(Axis.x, Axis.y, Axis.z), Angle);

	// Scale and convert the world transform translation into a Datasmith actor world translation.
	FVector WorldTranslation = ConvertMelangePosition(FVector(FloatMatrix[12], FloatMatrix[13], FloatMatrix[14]));

	// Remove our children or else the ConvertChildsToRelative + ConvertChildsToWorld combo within SetTranslation/Rotation/Scale will
	// cause our children to maintain their relative transform to Actor, which is not what we want. When we set a Trans/Rot/Scale we
	// are setting the final, absolute world space value
	int32 ChildCount = Actor->GetChildrenCount();
	TArray<TSharedPtr<IDatasmithActorElement>> Children;
	Children.SetNum(ChildCount);
	for (int32 ChildIndex = ChildCount - 1; ChildIndex >= 0; --ChildIndex)
	{
		const TSharedPtr<IDatasmithActorElement>& Child = Actor->GetChild(ChildIndex);

		Children[ChildIndex] = Child;
		Actor->RemoveChild(Child);
	}

	Actor->SetTranslation(WorldTranslation);
	Actor->SetScale(WorldScale);
	Actor->SetRotation(WorldRotation);

	ParentActor->AddChild(Actor);
	for (const TSharedPtr<IDatasmithActorElement>& Child : Children)
	{
		Actor->AddChild(Child, EDatasmithActorAttachmentRule::KeepWorldTransform);
	}
}

TSharedPtr<IDatasmithActorElement> FDatasmithC4DImporter::ImportNullActor(melange::BaseObject* Object, const FString& DatasmithName, const FString& DatasmithLabel)
{
	TSharedPtr<IDatasmithActorElement> ActorElement = FDatasmithSceneFactory::CreateActor(*DatasmithName);
	ActorElement->SetLabel(*DatasmithLabel);
	return ActorElement;
}

namespace
{
	TSharedPtr<IDatasmithLightActorElement> CreateDatasmithLightActorElement(int32 MelangeLightTypeId, const FString& Name, const FString& Label)
	{
		TSharedPtr<IDatasmithLightActorElement> Result = nullptr;
		switch (MelangeLightTypeId)
		{
		case melange::LIGHT_TYPE_OMNI:
			Result = FDatasmithSceneFactory::CreatePointLight(*Name);
			break;
		case melange::LIGHT_TYPE_SPOT:
			Result = FDatasmithSceneFactory::CreateSpotLight(*Name);
			break;
		case melange::LIGHT_TYPE_SPOTRECT:
			Result = FDatasmithSceneFactory::CreateSpotLight(*Name);
			break;
		case melange::LIGHT_TYPE_DISTANT:
			Result = FDatasmithSceneFactory::CreateDirectionalLight(*Name);
			break;
		case melange::LIGHT_TYPE_PARALLEL:
			Result = FDatasmithSceneFactory::CreateSpotLight(*Name);
			break;
		case melange::LIGHT_TYPE_PARSPOTRECT:
			Result = FDatasmithSceneFactory::CreateSpotLight(*Name);
			break;
		case melange::LIGHT_TYPE_TUBE:
			Result = FDatasmithSceneFactory::CreateSpotLight(*Name);
			break;
		case melange::LIGHT_TYPE_AREA:
			Result = FDatasmithSceneFactory::CreateAreaLight(*Name);
			break;
		case melange::LIGHT_TYPE_PHOTOMETRIC:
			Result = FDatasmithSceneFactory::CreatePointLight(*Name);
			break;
		default:
			break;
		}

		if (Result.IsValid())
		{
			Result->SetLabel(*Label);
		}

		return Result;
	}

	EDatasmithLightUnits GetDatasmithLightIntensityUnits(int32 MelangeLightUnitId)
	{
		switch (MelangeLightUnitId)
		{
		case melange::LIGHT_PHOTOMETRIC_UNIT_LUMEN:
			return EDatasmithLightUnits::Lumens;
			break;
		case melange::LIGHT_PHOTOMETRIC_UNIT_CANDELA:
			return EDatasmithLightUnits::Candelas;
			break;
		default:
			break;
		}

		return EDatasmithLightUnits::Unitless;
	}

	// Is called when a LightType is Ligth Area to fits its shape
	EDatasmithLightShape GetDatasmithAreaLightShape(int32 AreaLightC4DId)
	{
		switch (AreaLightC4DId)
		{
		case melange::LIGHT_AREADETAILS_SHAPE_DISC:
			return EDatasmithLightShape::Disc;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_RECTANGLE:
			return EDatasmithLightShape::Rectangle;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_SPHERE:
			return EDatasmithLightShape::Sphere;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_CYLINDER:
			return EDatasmithLightShape::Cylinder;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_CUBE:
			return EDatasmithLightShape::Rectangle;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_HEMISPHERE:
			return EDatasmithLightShape::Sphere;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_OBJECT:
			return EDatasmithLightShape::None;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_LINE:
			return EDatasmithLightShape::Cylinder;
			break;
		case melange::LIGHT_AREADETAILS_SHAPE_PCYLINDER:
			return EDatasmithLightShape::Cylinder;
			break;
		default:
			break;
		}

		return EDatasmithLightShape::None;
	}
}

namespace
{
	melange::Int32 MelangeColorProfile = melange::DOCUMENT_COLORPROFILE_SRGB;

	FVector ToLinearColor(const FVector& Color)
	{
		// Document is already linear, nothing to do
		if (MelangeColorProfile == melange::DOCUMENT_COLORPROFILE_LINEAR)
		{
			return Color;
		}

		// The default seems to be sRGB
		FLinearColor ActuallyLinearColor = FLinearColor(FLinearColor(Color).QuantizeRound());
		return FVector(ActuallyLinearColor.R, ActuallyLinearColor.G, ActuallyLinearColor.B);
	}

	// Gets a color weighted by its brightness
	FVector MelangeGetLayerColor(melange::BaseList2D* MelangeObject, melange::Int32 ColorAttributeID, melange::Int32 BrightnessAttributeID)
	{
		FVector Result;
		if (MelangeObject)
		{
			float Brightness = MelangeGetFloat(MelangeObject, BrightnessAttributeID);
			FVector Color = MelangeGetVector(MelangeObject, ColorAttributeID);
			Result = ToLinearColor(Color * Brightness);
		}
		return Result;
	}

	// In here instead of utils because it depends on the document color profile
	FVector MelangeGetColor(melange::BaseList2D* MelangeObject, melange::Int32 MelangeDescId)
	{
		FVector Result;
		if (MelangeObject)
		{
			Result = ToLinearColor(MelangeGetVector(MelangeObject, MelangeDescId));
		}
		return Result;
	}

	void AddColorToMaterial(const TSharedPtr<IDatasmithMasterMaterialElement>& Material, const FString& DatasmithPropName, const FLinearColor& LinearColor)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*DatasmithPropName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Color);
		MaterialPropertyPtr->SetValue(*LinearColor.ToString());

		Material->AddProperty(MaterialPropertyPtr);
	}

	void AddFloatToMaterial(const TSharedPtr<IDatasmithMasterMaterialElement>& Material, const FString& DatasmithPropName, float Value)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*DatasmithPropName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
		MaterialPropertyPtr->SetValue(*LexToString(Value));

		Material->AddProperty(MaterialPropertyPtr);
	}

	void AddBoolToMaterial(const TSharedPtr<IDatasmithMasterMaterialElement>& Material, const FString& DatasmithPropName, bool bValue)
	{
		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*DatasmithPropName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Bool);
		MaterialPropertyPtr->SetValue(bValue ? TEXT("True") : TEXT("False"));

		Material->AddProperty(MaterialPropertyPtr);
	}

	void AddTextureToMaterial(const TSharedPtr<IDatasmithMasterMaterialElement>& Material, const FString& DatasmithPropName, TSharedPtr<IDatasmithTextureElement> Texture)
	{
		if (Texture == nullptr)
		{
			return;
		}

		TSharedPtr<IDatasmithKeyValueProperty> MaterialPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(*DatasmithPropName);
		MaterialPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
		MaterialPropertyPtr->SetValue(Texture->GetName());

		Material->AddProperty(MaterialPropertyPtr);
	}
}

TSharedPtr<IDatasmithLightActorElement> FDatasmithC4DImporter::ImportLight(melange::BaseObject* InC4DLightPtr, const FString& DatasmithName, const FString& DatasmithLabel)
{
	melange::GeData C4DData;

	// Actor type
	int32 LightTypeId = MelangeGetInt32(InC4DLightPtr, melange::LIGHT_TYPE);
	TSharedPtr<IDatasmithLightActorElement> LightActor = CreateDatasmithLightActorElement(LightTypeId, DatasmithName, DatasmithLabel);
	if (!LightActor.IsValid())
	{
		UE_LOG(LogDatasmithC4DImport, Warning, TEXT("Failed to create DatasmithLightActorElement for light '%s'"), *MelangeObjectName(InC4DLightPtr));
		return nullptr;
	}

	// Color
	FLinearColor Color = MelangeGetColor(InC4DLightPtr, melange::LIGHT_COLOR);

	// Temperature
	bool bUseTemperature = MelangeGetBool(InC4DLightPtr, melange::LIGHT_TEMPERATURE);
	double Temperature = MelangeGetDouble(InC4DLightPtr, melange::LIGHT_TEMPERATURE_MAIN);
	if (Temperature == 0)
	{
		Temperature = 6500.0;
	}

	// IES light
	// We won't use IES Brightness Scale from the file for now, just use regular light brightness
	FString IESPath;
	if (MelangeGetBool(InC4DLightPtr, melange::LIGHT_PHOTOMETRIC_DATA))
	{
		IESPath = MelangeGetString(InC4DLightPtr, melange::LIGHT_PHOTOMETRIC_FILE);
		IESPath = SearchForFile(IESPath, C4dDocumentFilename);
	}

	// Units
	EDatasmithLightUnits Units = EDatasmithLightUnits::Unitless;
	if (MelangeGetBool(InC4DLightPtr, melange::LIGHT_PHOTOMETRIC_UNITS))
	{
		Units = GetDatasmithLightIntensityUnits(MelangeGetInt32(InC4DLightPtr, melange::LIGHT_PHOTOMETRIC_UNIT));
	}

	// Intensity
	double Intensity = MelangeGetDouble(InC4DLightPtr, melange::LIGHT_BRIGHTNESS);
	if (Units == EDatasmithLightUnits::Unitless)
	{
		if (LightActor->IsA(EDatasmithElementType::PointLight))
		{
			Intensity *= UnitlessIESandPointLightIntensity;
		}
		else
		{
			Intensity *= UnitlessGlobalLightIntensity;
		}
	}

	// Set common parameters for all lights (including directional lights)
	LightActor->SetIntensity(Intensity);
	LightActor->SetUseIes(!IESPath.IsEmpty());
	LightActor->SetIesFile(*IESPath);
	LightActor->SetTemperature(Temperature);
	LightActor->SetUseTemperature(bUseTemperature);
	LightActor->SetColor(Color);

	// Set point light parameters
	if (LightActor->IsA(EDatasmithElementType::PointLight))
	{
		TSharedPtr<IDatasmithPointLightElement> PointLightActor = StaticCastSharedPtr<IDatasmithPointLightElement>(LightActor);

		PointLightActor->SetIntensityUnits(Units);

		// Attenuation radius
		PointLightActor->SetAttenuationRadius(MelangeGetFloat(InC4DLightPtr, melange::LIGHT_DETAILS_OUTERDISTANCE));
	}

	// Set spot light parameters
	if (LightActor->IsA(EDatasmithElementType::SpotLight))
	{
		TSharedPtr<IDatasmithSpotLightElement> SpotLightActor = StaticCastSharedPtr<IDatasmithSpotLightElement>(LightActor);

		// Inner angle
		float LightInnerAngleInRadians = MelangeGetFloat(InC4DLightPtr, melange::LIGHT_DETAILS_INNERANGLE);
		SpotLightActor->SetInnerConeAngle((FMath::RadiansToDegrees(LightInnerAngleInRadians) * 90) / 175);

		// Outer angle
		float LightOuterAngleInRadians = MelangeGetFloat(InC4DLightPtr, melange::LIGHT_DETAILS_OUTERANGLE);
		SpotLightActor->SetOuterConeAngle((FMath::RadiansToDegrees(LightOuterAngleInRadians) * 90) / 175);
	}

	// Set area light parameters
	if (LightActor->IsA(EDatasmithElementType::AreaLight))
	{
		TSharedPtr<IDatasmithAreaLightElement> AreaLightActor = StaticCastSharedPtr<IDatasmithAreaLightElement>(LightActor);

		// Area width
		AreaLightActor->SetWidth(MelangeGetFloat(InC4DLightPtr, melange::LIGHT_AREADETAILS_SIZEX));

		// Area length
		AreaLightActor->SetLength(MelangeGetFloat(InC4DLightPtr, melange::LIGHT_AREADETAILS_SIZEY));

		// Area shape and type
		EDatasmithLightShape AreaShape = GetDatasmithAreaLightShape(MelangeGetInt32(InC4DLightPtr, melange::LIGHT_AREADETAILS_SHAPE));

		// AreaLightType will default to Point, which is OK for most shapes except the planar shapes like Disc and Rectangle.
		// Also, if the user enabled the "Z Direction Only" checkbox we'll also use Rect type as the Point type is omnidirectional
		EDatasmithAreaLightType AreaType = EDatasmithAreaLightType::Point;
		bool bOnlyZ = MelangeGetBool(InC4DLightPtr, melange::LIGHT_DETAILS_ONLYZ);
		if (bOnlyZ || AreaShape == EDatasmithLightShape::Rectangle || AreaShape == EDatasmithLightShape::Disc)
		{
			AreaType = EDatasmithAreaLightType::Rect;
		}

		AreaLightActor->SetLightType(AreaType);
		AreaLightActor->SetLightShape(AreaShape);
	}

	return LightActor;
}

TSharedPtr<IDatasmithCameraActorElement> FDatasmithC4DImporter::ImportCamera(melange::BaseObject* InC4DCameraPtr, const FString& DatasmithName, const FString& DatasmithLabel)
{
	TSharedPtr<IDatasmithCameraActorElement> CameraActor = FDatasmithSceneFactory::CreateCameraActor(*DatasmithName);
	CameraActor->SetLabel(*DatasmithLabel);

	melange::GeData C4DData;

	melange::BaseTag* LookAtTag = InC4DCameraPtr->GetTag(Ttargetexpression);
	melange::BaseList2D* LookAtObject = LookAtTag ? MelangeGetLink(LookAtTag, melange::TARGETEXPRESSIONTAG_LINK) : nullptr;
	if (LookAtObject)
	{
		//LookAtObject can not be a cached object or an instanced object so GetMelangeBaseList2dID should be the final ID
		FString LookAtID = GetMelangeBaseList2dID(LookAtObject);
		CameraActor->SetLookAtActor(*LookAtID);
		CameraActor->SetLookAtAllowRoll(true);
		NamesOfCameraTargetActors.Add(LookAtID);
	}

	float CameraFocusDistanceInCM = MelangeGetFloat(InC4DCameraPtr, melange::CAMERAOBJECT_TARGETDISTANCE);
	CameraActor->SetFocusDistance(CameraFocusDistanceInCM);

	float CameraFocalLengthMilimeters = MelangeGetFloat(InC4DCameraPtr, melange::CAMERA_FOCUS);
	CameraActor->SetFocalLength(CameraFocalLengthMilimeters);

	float CameraHorizontalFieldOfViewInDegree = FMath::RadiansToDegrees(MelangeGetFloat(InC4DCameraPtr, melange::CAMERAOBJECT_FOV));
	float CameraSensorWidthInMillimeter = 2 * (CameraFocalLengthMilimeters * tan((0.5f * CameraHorizontalFieldOfViewInDegree) / 57.296f));
	CameraActor->SetSensorWidth(CameraSensorWidthInMillimeter);

	// Set the camera aspect ratio (width/height).
	melange::RenderData* SceneRenderer = C4dDocument->GetActiveRenderData();
	melange::Float AspectRatioOfRenderer, RendererWidth, RendererHeight, PixelAspectRatio;
	SceneRenderer->GetResolution(RendererWidth, RendererHeight, PixelAspectRatio, AspectRatioOfRenderer);
	double AspectRatio = RendererWidth / RendererHeight;
	CameraActor->SetSensorAspectRatio(static_cast<float>(AspectRatio));

	// We only use manual exposure control with aperture, shutter speed and ISO if the exposure checkbox is enabled
	// Aperture is always used for depth of field effects though, which is why its outside of this
	if (MelangeGetBool(InC4DCameraPtr, melange::CAMERAOBJECT_EXPOSURE))
	{
		float ShutterSpeed = MelangeGetFloat(InC4DCameraPtr, melange::CAMERAOBJECT_SHUTTER_SPEED_VALUE);
		CameraActor->GetPostProcess()->SetCameraShutterSpeed(ShutterSpeed ? 1.0f/ShutterSpeed : -1.0f);

		float ISO = MelangeGetFloat(InC4DCameraPtr, melange::CAMERAOBJECT_ISO_VALUE);
		CameraActor->GetPostProcess()->SetCameraISO(ISO ? ISO : -1.0f);
	}
	float Aperture = MelangeGetFloat(InC4DCameraPtr, melange::CAMERAOBJECT_FNUMBER_VALUE);
	CameraActor->SetFStop(Aperture ? Aperture : -1.0f);

	melange::BaseTag* Tag = InC4DCameraPtr->GetFirstTag();
	while (Tag)
	{
		melange::Int32 TagType = Tag->GetType();
		if (TagType == Tcrane)
		{
			TSharedRef<FCraneCameraAttributes> Attributes = ExtractCraneCameraAttributes(Tag);
			CraneCameraToAttributes.Add(InC4DCameraPtr, Attributes);
			break;
		}
		Tag = Tag->GetNext();
	}

	return CameraActor;
}

TSharedPtr<IDatasmithTextureElement> FDatasmithC4DImporter::ImportTexture(const FString& TexturePath, EDatasmithTextureMode TextureMode)
{
	if (TexturePath.IsEmpty())
	{
		return nullptr;
	}

	FString TextureName = FString::Printf(TEXT("%ls_%d"), *FMD5::HashAnsiString(*TexturePath), int32(TextureMode));
	if (TSharedPtr<IDatasmithTextureElement>* FoundImportedTexture = ImportedTextures.Find(TextureName))
	{
		return *FoundImportedTexture;
	}

	TSharedPtr<IDatasmithTextureElement> Texture = FDatasmithSceneFactory::CreateTexture(*TextureName);
	Texture->SetTextureMode(TextureMode);
	Texture->SetLabel(*FPaths::GetBaseFilename(TexturePath));
	Texture->SetFile(*TexturePath);
	DatasmithScene->AddTexture(Texture);

	return Texture;
}

FString FDatasmithC4DImporter::GetBaseShaderTextureFilePath(melange::BaseList2D* BaseShader)
{
	FString TextureFilePath;

	while (BaseShader && TextureFilePath.IsEmpty())
	{
		switch (BaseShader->GetType())
		{
		case Xbitmap:
		{
			FString Filepath = MelangeFilenameToPath(static_cast<melange::BaseShader*>(BaseShader)->GetFileName());
			TextureFilePath = SearchForFile(Filepath, C4dDocumentFilename);
			break;
		}
		default:
			TextureFilePath = GetBaseShaderTextureFilePath(static_cast<melange::BaseShader*>(BaseShader)->GetDown());
			break;
		}

		BaseShader = BaseShader->GetNext();
	}

	return TextureFilePath;
}

TSharedPtr<IDatasmithMasterMaterialElement> FDatasmithC4DImporter::ImportMaterial(melange::Material* InC4DMaterialPtr)
{
	FString DatasmithName  = GetMelangeBaseList2dID(InC4DMaterialPtr);
	FString DatasmithLabel = FDatasmithUtils::SanitizeObjectName(MelangeObjectName(InC4DMaterialPtr));

	TSharedPtr<IDatasmithMasterMaterialElement> MaterialPtr = FDatasmithSceneFactory::CreateMasterMaterial(*DatasmithName);
	MaterialPtr->SetLabel(*DatasmithLabel);
	MaterialPtr->SetMaterialType(EDatasmithMasterMaterialType::Opaque);

	// Color
	bool bUseColor = InC4DMaterialPtr->GetChannelState(CHANNEL_COLOR);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_Color"), bUseColor);
	if (bUseColor)
	{
		FVector Color = MelangeGetLayerColor(InC4DMaterialPtr, melange::MATERIAL_COLOR_COLOR, melange::MATERIAL_COLOR_BRIGHTNESS);
		AddColorToMaterial(MaterialPtr, TEXT("Color"), Color);

		melange::BaseList2D* MaterialShader = MelangeGetLink(InC4DMaterialPtr, melange::MATERIAL_COLOR_SHADER);
		FString TextureFilePath = GetBaseShaderTextureFilePath(MaterialShader);
		TSharedPtr<IDatasmithTextureElement> ColorMap = ImportTexture(TextureFilePath, EDatasmithTextureMode::Diffuse);
		AddTextureToMaterial(MaterialPtr, TEXT("ColorMap"), ColorMap);

		bool bUseColorMap = !TextureFilePath.IsEmpty();
		AddBoolToMaterial(MaterialPtr, TEXT("Use_ColorMap"), bUseColorMap);
		if (bUseColorMap)
		{
			AddFloatToMaterial(MaterialPtr, TEXT("Exposure"), 0);

			// Check for the good type of Texture Mixing and Blending
			int32 MixingTypeId = MelangeGetInt32(InC4DMaterialPtr, melange::MATERIAL_COLOR_TEXTUREMIXING);
			switch (MixingTypeId)
			{
			case melange::MATERIAL_TEXTUREMIXING_ADD:
				AddBoolToMaterial(MaterialPtr, TEXT("MixMode_Add"), true);
				break;
			case melange::MATERIAL_TEXTUREMIXING_SUBTRACT:
				AddBoolToMaterial(MaterialPtr, TEXT("MixMode_Subtract"), true);
				break;
			case melange::MATERIAL_TEXTUREMIXING_MULTIPLY:
				AddBoolToMaterial(MaterialPtr, TEXT("MixMode_Multiply"), true);
				break;
			default: // MATERIAL_TEXTUREMIXING_NORMAL
				AddBoolToMaterial(MaterialPtr, TEXT("MixMode_Normal"), true);
				break;
			}

			float MixStrength = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_COLOR_TEXTURESTRENGTH);
			AddFloatToMaterial(MaterialPtr, TEXT("Mix_Strength"), MixStrength);
		}
	}

	// Emissive
	bool bUseEmissive = InC4DMaterialPtr->GetChannelState(CHANNEL_LUMINANCE);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_Emissive"), bUseEmissive);
	if (bUseEmissive)
	{
		float EmissiveGlowStrength = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_LUMINANCE_BRIGHTNESS);
		AddFloatToMaterial(MaterialPtr, TEXT("Emissive_Glow_Strength"), EmissiveGlowStrength);

		FLinearColor EmissiveColor = MelangeGetColor(InC4DMaterialPtr, melange::MATERIAL_LUMINANCE_COLOR);
		AddColorToMaterial(MaterialPtr, TEXT("Emissive_Color"), EmissiveColor);

		melange::BaseList2D* LuminanceShader = MelangeGetLink(InC4DMaterialPtr, melange::MATERIAL_LUMINANCE_SHADER);
		FString LuminanceFilePath = GetBaseShaderTextureFilePath(LuminanceShader);
		TSharedPtr<IDatasmithTextureElement> EmissiveMap = ImportTexture(LuminanceFilePath, EDatasmithTextureMode::Other);
		AddTextureToMaterial(MaterialPtr, TEXT("Emissive_Map"), EmissiveMap);

		bool bUseEmissiveMap = !LuminanceFilePath.IsEmpty();
		AddBoolToMaterial(MaterialPtr, TEXT("Use_EmissiveMap"), bUseEmissiveMap);
		if (bUseEmissiveMap)
		{
			float EmissiveMapExposure = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_LUMINANCE_TEXTURESTRENGTH);
			AddFloatToMaterial(MaterialPtr, TEXT("Emissive_Map_Exposure"), EmissiveMapExposure);
		}
	}

	// Transparency
	bool bUseTransparency = InC4DMaterialPtr->GetChannelState(CHANNEL_TRANSPARENCY);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_Transparency"), bUseTransparency);
	if (bUseTransparency)
	{
		MaterialPtr->SetMaterialType(EDatasmithMasterMaterialType::Transparent);

		melange::BaseList2D* TransparencyShader = MelangeGetLink(InC4DMaterialPtr, melange::MATERIAL_TRANSPARENCY_SHADER);
		FString TransparencyMapPath = GetBaseShaderTextureFilePath(TransparencyShader);
		TSharedPtr<IDatasmithTextureElement> TransparencyMap = ImportTexture(TransparencyMapPath, EDatasmithTextureMode::Other);
		AddTextureToMaterial(MaterialPtr, TEXT("Transparency_Map"), TransparencyMap);

		bool bUseTransparencyMap = !TransparencyMapPath.IsEmpty();
		AddBoolToMaterial(MaterialPtr, TEXT("Use_TransparencyMap"), bUseTransparencyMap);
		if (bUseTransparencyMap)
		{
			float TextureStrength = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_TRANSPARENCY_TEXTURESTRENGTH);
			AddFloatToMaterial(MaterialPtr, TEXT("TransparencyMap_Amount"), TextureStrength);
		}
		else
		{
			float BrightnessValue = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_TRANSPARENCY_BRIGHTNESS);
			FVector TransparencyColor = MelangeGetVector(InC4DMaterialPtr, melange::MATERIAL_TRANSPARENCY_COLOR);

			// In Cinema4D Transparency Color seems to be used just as another multiplier for the opacity, not as an actual color
			AddFloatToMaterial(MaterialPtr, TEXT("Transparency_Amount"), BrightnessValue * TransparencyColor.X * TransparencyColor.Y * TransparencyColor.Z);
		}

		float TransparencyRefraction = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_TRANSPARENCY_REFRACTION);
		AddFloatToMaterial(MaterialPtr, TEXT("Transparency_Refraction"), TransparencyRefraction);
	}

	melange::GeData C4DData;

	// Specular
	bool bUseSpecular = InC4DMaterialPtr->GetChannelState(CHANNEL_REFLECTION);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_Specular"), bUseSpecular);
	if (bUseSpecular)
	{
		melange::Int32 ReflectionLayerCount = InC4DMaterialPtr->GetReflectionLayerCount();
		if (ReflectionLayerCount > 0)
		{
			bool bUseReflectionColor = false;

			// Grab the total base color from all diffuse layers
			FVector ReflectionColor(0, 0, 0);
			for (int32 LayerIndex = ReflectionLayerCount - 1; LayerIndex >= 0; --LayerIndex)
			{
				melange::ReflectionLayer* ReflectionLayer = InC4DMaterialPtr->GetReflectionLayerIndex(LayerIndex);
				if (ReflectionLayer == nullptr)
				{
					continue;
				}

				melange::Int32 ReflectionLayerBaseId = ReflectionLayer->GetDataID();
				melange::Int32 ReflectionLayerFlags = ReflectionLayer->flags;

				// Don't fetch colors from reflectance layers that, regardless of fresnel function, don't seem to contribute a lot to main base color
				int32 LayerType = MelangeGetInt32(InC4DMaterialPtr, ReflectionLayerBaseId+REFLECTION_LAYER_MAIN_DISTRIBUTION);
				if (LayerType == REFLECTION_DISTRIBUTION_SPECULAR_PHONG || LayerType == REFLECTION_DISTRIBUTION_SPECULAR_BLINN || LayerType == REFLECTION_DISTRIBUTION_IRAWAN)
				{
					continue;
				}

				// Whether the layer is marked as visible (eye icon left of layer name)
				if (ReflectionLayerFlags & REFLECTION_FLAG_ACTIVE)
				{
					// Dropdown for Normal/Add to the right of layer name
					int32 BlendMode = MelangeGetInt32(InC4DMaterialPtr, ReflectionLayerBaseId+REFLECTION_LAYER_MAIN_BLEND_MODE);

					// Slider/percentage value describing the layer opacity, to the right of Normal/Add dropdown
					float Opacity = MelangeGetFloat(InC4DMaterialPtr, ReflectionLayerBaseId+REFLECTION_LAYER_MAIN_OPACITY);

					bUseReflectionColor = true;
					FVector LayerColor = MelangeGetLayerColor(InC4DMaterialPtr, ReflectionLayerBaseId + REFLECTION_LAYER_COLOR_COLOR,
															  ReflectionLayerBaseId + REFLECTION_LAYER_COLOR_BRIGHTNESS);

					// This is a temporary solution in order to let some color from reflectance layers factor in to the final basecolor depending on their
					// fresnel function
					melange::Int32 FresnelMode = InC4DMaterialPtr->GetParameter(ReflectionLayerBaseId + REFLECTION_LAYER_FRESNEL_MODE, C4DData) ? C4DData.GetInt32() : REFLECTION_FRESNEL_NONE;
					switch (FresnelMode)
					{
					case REFLECTION_FRESNEL_NONE:
						Opacity *= 1.0f;  // The reflectance layer looks like a solid, opaque layer
						break;
					case REFLECTION_FRESNEL_DIELECTRIC:
						Opacity *= 0.0f;  // The reflectance layer is used mostly for highlights and specular reflections
						break;
					case REFLECTION_FRESNEL_CONDUCTOR:
						Opacity *= 0.4;  // The reflectance layer looks like a transparent coat or overlay
						break;
					default:
						break;
					}

					// Normal
					if (BlendMode == 0)
					{
						ReflectionColor = LayerColor * Opacity + ReflectionColor * (1 - Opacity);
					}
					// Add
					else if(BlendMode == 1)
					{
						ReflectionColor = LayerColor * Opacity + ReflectionColor;
					}
				}
			}

			AddBoolToMaterial(MaterialPtr, TEXT("Use_ReflectionColor"), bUseReflectionColor);
			if (bUseReflectionColor)
			{
				// Global Reflection Brightness and Specular Brightness on Layers tab
				float GlobalReflection = static_cast<float>(MelangeGetDouble(InC4DMaterialPtr, REFLECTION_LAYER_GLOBAL_REFLECTION));
				float GlobalSpecular = static_cast<float>(MelangeGetDouble(InC4DMaterialPtr, REFLECTION_LAYER_GLOBAL_SPECULAR));

				// Approximation of the combined effect of those. This doesn't make much sense
				// as these are different effects and applied differently, but this is all a
				// temp solution until we get proper material graphs
				float ReflectionChannelColorWeight = (GlobalReflection * 0.75f + GlobalSpecular * 0.25f);
				AddFloatToMaterial(MaterialPtr, TEXT("ReflectionColor_Strength"), ReflectionChannelColorWeight);
				AddColorToMaterial(MaterialPtr, TEXT("ReflectionColor"), ReflectionColor);
			}

			// Only set those one for the last Layer of reflection
			melange::ReflectionLayer* ReflectionLayer = InC4DMaterialPtr->GetReflectionLayerIndex(0);

			bool bUseReflectance = (ReflectionLayer != nullptr);
			AddBoolToMaterial(MaterialPtr, TEXT("Use_Reflectance"), bUseReflectance);
			if (bUseReflectance)
			{
				melange::Int32 ReflectionLayerBaseId = ReflectionLayer->GetDataID();

				float SpecularStrength = MelangeGetFloat(InC4DMaterialPtr, ReflectionLayerBaseId + REFLECTION_LAYER_MAIN_VALUE_SPECULAR);
				AddFloatToMaterial(MaterialPtr, TEXT("Specular_Strength"), SpecularStrength);

				melange::BaseList2D* RoughnessShader = MelangeGetLink(InC4DMaterialPtr, ReflectionLayerBaseId + REFLECTION_LAYER_MAIN_SHADER_ROUGHNESS);
				FString RoughnessMapPath = GetBaseShaderTextureFilePath(RoughnessShader);
				TSharedPtr<IDatasmithTextureElement> RoughnessMap1 = ImportTexture(RoughnessMapPath, EDatasmithTextureMode::Diffuse);
				AddTextureToMaterial(MaterialPtr, TEXT("RoughnessMap1"), RoughnessMap1);

				bool bUseRoughnessMap = !RoughnessMapPath.IsEmpty();
				AddBoolToMaterial(MaterialPtr, TEXT("Use_RoughnessMap"), bUseRoughnessMap);
				if (bUseRoughnessMap)
				{
					float RoughnessMapStrength = MelangeGetFloat(InC4DMaterialPtr, ReflectionLayerBaseId + REFLECTION_LAYER_MAIN_VALUE_ROUGHNESS);
					AddFloatToMaterial(MaterialPtr, TEXT("RoughnessMap1_Strength"), RoughnessMapStrength);
				}
				else
				{
					float RoughnessStrength = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_SPECULAR_WIDTH /*appears to be the computed roughness*/);
					AddFloatToMaterial(MaterialPtr, TEXT("Roughness_Strength"), RoughnessStrength);
				}

				int32 FresnelMode = MelangeGetInt32(InC4DMaterialPtr, ReflectionLayerBaseId + REFLECTION_LAYER_FRESNEL_MODE);

				bool bUseMetalic = (FresnelMode == REFLECTION_FRESNEL_CONDUCTOR);
				AddBoolToMaterial(MaterialPtr, TEXT("Use_Metalic"), bUseMetalic);
				if (bUseMetalic)
				{
					AddFloatToMaterial(MaterialPtr, TEXT("Metalic_Amount"), 0.5f);

					melange::BaseList2D* MetallicShader = MelangeGetLink(InC4DMaterialPtr, ReflectionLayerBaseId + REFLECTION_LAYER_TRANS_TEXTURE);
					FString MetallicMapPath = GetBaseShaderTextureFilePath(MetallicShader);
					TSharedPtr<IDatasmithTextureElement> MetalicMap = ImportTexture(MetallicMapPath, EDatasmithTextureMode::Specular);
					AddTextureToMaterial(MaterialPtr, TEXT("MetalicMap"), MetalicMap);

					bool bUseMetalicMap = !MetallicMapPath.IsEmpty();
					AddBoolToMaterial(MaterialPtr, TEXT("Use_MetalicMap"), bUseMetalicMap);
				}
			}
		}
	}

	// AO
	bool bUseAO = InC4DMaterialPtr->GetChannelState(CHANNEL_DIFFUSION);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_AO"), bUseAO);
	if (bUseAO)
	{
		melange::BaseList2D* DiffusionShader = MelangeGetLink(InC4DMaterialPtr, melange::MATERIAL_DIFFUSION_SHADER);
		FString AOMapPath = GetBaseShaderTextureFilePath(DiffusionShader);
		TSharedPtr<IDatasmithTextureElement> AOMap = ImportTexture(AOMapPath, EDatasmithTextureMode::Diffuse);
		AddTextureToMaterial(MaterialPtr, TEXT("AO_Map"), AOMap);

		if (!AOMapPath.IsEmpty())
		{
			float AOStrength = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_DIFFUSION_TEXTURESTRENGTH);
			AddFloatToMaterial(MaterialPtr, TEXT("AO_Strength"), AOStrength);
		}
	}

	// Alpha
	bool bUseAlpha = !bUseTransparency && InC4DMaterialPtr->GetChannelState(CHANNEL_ALPHA);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_Alpha"), bUseAlpha);
	if (bUseAlpha)
	{
		MaterialPtr->SetMaterialType(EDatasmithMasterMaterialType::CutOut);

		melange::BaseList2D* AlphaShader = MelangeGetLink(InC4DMaterialPtr, melange::MATERIAL_ALPHA_SHADER);
		FString AlphaMapPath = GetBaseShaderTextureFilePath(AlphaShader);
		TSharedPtr<IDatasmithTextureElement> AlphaMap = ImportTexture(AlphaMapPath, EDatasmithTextureMode::Diffuse);
		AddTextureToMaterial(MaterialPtr, TEXT("Alpha_Map"), AlphaMap);

		bool bUseAlphaInvert = MelangeGetBool(InC4DMaterialPtr, melange::MATERIAL_ALPHA_INVERT);
		AddBoolToMaterial(MaterialPtr, TEXT("Use_Alpha_Invert"), bUseAlphaInvert);
	}

	// Normal
	bool bUseNormal = InC4DMaterialPtr->GetChannelState(CHANNEL_NORMAL);
	AddBoolToMaterial(MaterialPtr, TEXT("Use_Normal"), bUseNormal);
	if (bUseNormal)
	{
		melange::BaseList2D* NormalShader = MelangeGetLink(InC4DMaterialPtr, melange::MATERIAL_NORMAL_SHADER);
		FString NormalMapPath = GetBaseShaderTextureFilePath(NormalShader);
		TSharedPtr<IDatasmithTextureElement> NormalMap = ImportTexture(NormalMapPath, EDatasmithTextureMode::Normal);
		AddTextureToMaterial(MaterialPtr, TEXT("Normal_Map"), NormalMap);

		if (!NormalMapPath.IsEmpty())
		{
			float NormalMapStrength = MelangeGetFloat(InC4DMaterialPtr, melange::MATERIAL_NORMAL_STRENGTH);
			AddFloatToMaterial(MaterialPtr, TEXT("Normal_Strength"), NormalMapStrength);
		}
	}

	DatasmithScene->AddMaterial(MaterialPtr);
	return MaterialPtr;
}

void FDatasmithC4DImporter::ImportMaterialHierarchy(melange::BaseMaterial* InC4DMaterialPtr)
{
	// Reinitialize the scene material map and texture set.
	MaterialNameToMaterialElement.Empty();

	for (; InC4DMaterialPtr; InC4DMaterialPtr = InC4DMaterialPtr->GetNext())
	{
		if (InC4DMaterialPtr->GetType() == Mmaterial)
		{
			TSharedPtr<IDatasmithMasterMaterialElement> DatasmithMaterial = ImportMaterial(static_cast<melange::Material*>(InC4DMaterialPtr));
			MaterialNameToMaterialElement.Add(DatasmithMaterial->GetName(), DatasmithMaterial);
		}
	}
}

FString FDatasmithC4DImporter::CustomizeMaterial(const FString& InMaterialID, const FString& InMeshID, melange::TextureTag* InTextureTag)
{
	FString CustomMaterialID = InMaterialID + InMeshID;

	if (MaterialNameToMaterialElement.Contains(CustomMaterialID))
	{
		return CustomMaterialID;
	}

	if (MaterialNameToMaterialElement.Contains(InMaterialID))
	{
		melange::GeData Data;
		float OffsetX = MelangeGetFloat(InTextureTag, melange::TEXTURETAG_OFFSETX);
		float OffsetY = MelangeGetFloat(InTextureTag, melange::TEXTURETAG_OFFSETY);
		float TilesX = MelangeGetFloat(InTextureTag, melange::TEXTURETAG_TILESX);
		float TilesY = MelangeGetFloat(InTextureTag, melange::TEXTURETAG_TILESY);

		if (OffsetX != 0.0F || OffsetY != 0.0F || TilesX != 1.0F || TilesY != 1.0F)
		{
			TSharedPtr<IDatasmithMasterMaterialElement> CustomizedMaterial = FDatasmithSceneFactory::CreateMasterMaterial(*CustomMaterialID);

			// Create a copy of the original material
			TSharedPtr<IDatasmithMasterMaterialElement> OriginalMaterial = MaterialNameToMaterialElement[InMaterialID];
			for (int32 PropertyIndex = 0; PropertyIndex < OriginalMaterial->GetPropertiesCount(); ++PropertyIndex)
			{
				CustomizedMaterial->AddProperty(OriginalMaterial->GetProperty(PropertyIndex));
			}
			CustomizedMaterial->SetLabel(OriginalMaterial->GetLabel());

			AddFloatToMaterial(CustomizedMaterial, TEXT("Offset_U"), OffsetX);
			AddFloatToMaterial(CustomizedMaterial, TEXT("Offset_V"), OffsetY);
			AddFloatToMaterial(CustomizedMaterial, TEXT("Tile_U"), TilesX);
			AddFloatToMaterial(CustomizedMaterial, TEXT("Tile_V"), TilesY);

			MaterialNameToMaterialElement.Add(CustomMaterialID, CustomizedMaterial);

			DatasmithScene->AddMaterial(CustomizedMaterial);
			return CustomMaterialID;
		}
	}

	return InMaterialID;
}

TSharedPtr<IDatasmithMeshActorElement> FDatasmithC4DImporter::ImportPolygon(melange::PolygonObject* PolyObject, TMap<FString, melange::PolygonObject*>* ClonerBaseChildrenHash, const FString& DatasmithName, const FString& DatasmithLabel, const TArray<melange::TextureTag*>& TextureTags)
{
	FMD5Hash PolygonHash;

	melange::PolygonObject* DataPolyObject = PolyObject;
	if (ClonerBaseChildrenHash)
	{
		PolygonHash = ComputePolygonDataHash(PolyObject);
		FString PolygonHashStr = BytesToHex(PolygonHash.GetBytes(), 16);

		if (melange::PolygonObject** BasePolygon = ClonerBaseChildrenHash->Find(PolygonHashStr))
		{
			DataPolyObject = *BasePolygon;
		}
		else
		{
			ClonerBaseChildrenHash->Add(PolygonHashStr, PolyObject);
		}
	}

	IDatasmithMeshElement* ResultMeshElement = nullptr;
	if (TSharedRef<IDatasmithMeshElement>* PreviousMesh = PolygonObjectToMeshElement.Find(DataPolyObject))
	{
		ResultMeshElement = &PreviousMesh->Get();
	}
	else
	{
		// Compute the hash if we haven't by chance above
		if (!PolygonHash.IsValid())
		{
			PolygonHash = ComputePolygonDataHash(PolyObject);
		}

		TSharedPtr<IDatasmithMeshElement> MeshElement = ImportMesh(PolyObject, MelangeObjectID(DataPolyObject), DatasmithLabel, TextureTags);
		ResultMeshElement = MeshElement.Get();

		// Set the polygon hash as the file hash. It will be checked by Datasmith in
		// FDatasmithImporter::FilterElementsToImport to know if a mesh has changed and
		// the asset needs to be replaced during reimport
		ResultMeshElement->SetFileHash(PolygonHash);
	}

	TSharedPtr<IDatasmithMeshActorElement> MeshActorElement = FDatasmithSceneFactory::CreateMeshActor(*DatasmithName);
	MeshActorElement->SetLabel(*DatasmithLabel);
	MeshActorElement->SetStaticMeshPathName(ResultMeshElement->GetName());
	return MeshActorElement;
}

namespace
{
	// Ignore the children of objects that are rendred using only their cache
	bool BrowseInstanceObjectChildren(melange::BaseObject* Object)
	{
		melange::Int32 ObjectType = Object->GetType();
		return (ObjectType != Ocloner && ObjectType != Oarray &&
			(ObjectType != Osymmetry && ObjectType != Osds /*Sub Division Surface*/ && ObjectType != Oboole));
	}

	void BrowseInstanceObjectsHierarchy(melange::BaseObject* Object, TArray<melange::BaseObject*>& InstanceObjects)
	{
		while (Object)
		{
			InstanceObjects.Add(Object);

			if (BrowseInstanceObjectChildren(Object))
			{
				BrowseInstanceObjectsHierarchy(Object->GetDown(), InstanceObjects);
			}

			Object = Object->GetNext();
		}
	}
}

const TArray<melange::BaseObject*>& FDatasmithC4DImporter::GetMelangeInstanceObjects(melange::BaseObject* InstanceRoot)
{
	TArray<melange::BaseObject*>* CachedPolygons = InstancesObjectsMap.Find(InstanceRoot);
	if (CachedPolygons)
	{
		return *CachedPolygons;
	}

	TArray<melange::BaseObject*>& Result = InstancesObjectsMap.Add(InstanceRoot);

	Result.Add(InstanceRoot);

	if (BrowseInstanceObjectChildren(InstanceRoot))
	{
		BrowseInstanceObjectsHierarchy(InstanceRoot->GetDown(), Result);
	}

	return Result;
}

void MarkActorsAsParticlesRecursive(melange::BaseObject* ActorObject, TSet<melange::BaseObject*>& ParticleActors)
{
	if (ActorObject == nullptr)
	{
		return;
	}

	ParticleActors.Add(ActorObject);

	MarkActorsAsParticlesRecursive(ActorObject->GetDown(), ParticleActors);
	MarkActorsAsParticlesRecursive(ActorObject->GetNext(), ParticleActors);
}

void FDatasmithC4DImporter::MarkActorsAsParticles(melange::BaseObject* EmitterObject, melange::BaseObject* EmittersCache)
{
	if (EmitterObject == nullptr || EmittersCache == nullptr)
	{
		return;
	}

	// C4D only emits mesh "particles" if this "Show Objects" checkbox is checked. Else it just emits actual particles
	melange::GeData Data;
	if (EmitterObject->GetParameter(melange::PARTICLEOBJECT_SHOWOBJECTS, Data) && Data.GetType() == melange::DA_LONG && Data.GetBool())
	{
		MarkActorsAsParticlesRecursive(EmittersCache->GetDown(), ParticleActors);
	}
}

namespace
{
	melange::Float MelangeFPS = 0;

	void AddFrameValueToAnimMap(
		melange::BaseObject* Object,
		int32 FrameNumber,
		int32 TransformVectorIndex,
		EDatasmithTransformType TransformType,
		melange::Float FrameValue,
		melange::Int32 MelangeTransformType,
		FVector& InitialSize,
		TMap<int32, TMap<EDatasmithTransformType, FVector>>& TransformFrames,
		TMap<EDatasmithTransformType, FVector> InitialValues
	)
	{
		TMap<EDatasmithTransformType, FVector>* FrameValues = TransformFrames.Find(FrameNumber);
		if (!FrameValues)
		{
			FrameValues = &TransformFrames.Add(FrameNumber);
		}
		FVector* TransformValues = FrameValues->Find(TransformType);
		if (!TransformValues)
		{
			TransformValues = &FrameValues->Add(TransformType);
			*TransformValues = *InitialValues.Find(TransformType);
		}
		float Value = static_cast<float>(FrameValue);
		if (TransformType == EDatasmithTransformType::Scale)
		{
			if (MelangeTransformType == 1100) //Size
			{
				//Value is the absolute size, so First key = scaling of 1.0
				if (InitialSize[TransformVectorIndex] == 0)
				{
					InitialSize[TransformVectorIndex] = Value;
					Value = 1.0;
				}
				else
				{
					Value /= InitialSize[TransformVectorIndex];
				}
			}
		}
		(*TransformValues)[TransformVectorIndex] = Value;
	}
}

void FDatasmithC4DImporter::ImportAnimations(TSharedPtr<IDatasmithActorElement> ActorElement)
{
	melange::BaseObject* Object = *ActorElementToC4DObject.Find(ActorElement.Get());
	melange::Int32 ObjectType = Object->GetType();

	TMap<EDatasmithTransformType, FVector> InitialValues;
	melange::Vector MelangeRotation = Object->GetRelRot();
	InitialValues.Add(EDatasmithTransformType::Rotation) = FVector(static_cast<float>(MelangeRotation.x), static_cast<float>(MelangeRotation.y), static_cast<float>(MelangeRotation.z));
	InitialValues.Add(EDatasmithTransformType::Translation) = MelangeVectorToFVector(Object->GetRelPos());
	InitialValues.Add(EDatasmithTransformType::Scale) = MelangeVectorToFVector(Object->GetRelScale());

	TMap<int32, TMap<EDatasmithTransformType, FVector>> TransformFrames;
	FVector InitialSize(0, 0, 0);

	// If we have AlignToSpline animations, the splines are stored with their points in world space,
	// so we must move them into the object's local space
	melange::Matrix WorldToLocal = ~Object->GetUpMg();

	melange::ROTATIONORDER RotationOrder = Object->GetRotationOrder();

	// Import animations on the object's tags
	for(melange::BaseTag* Tag = Object->GetFirstTag(); Tag; Tag=Tag->GetNext())
	{
		melange::Int32 TagType = Tag->GetType();

		if (TagType == Tcrane && ObjectType == Ocamera)
		{
			TSharedRef<FCraneCameraAttributes>* FoundAttributes = CraneCameraToAttributes.Find(Object);
			if (!FoundAttributes)
			{
				UE_LOG(LogDatasmithC4DImport, Warning, TEXT("Trying to parse animations for crane camera '%s', but it doesn't have crane camera attributes!"), *MelangeObjectName(Object));
				continue;
			}

			TMap<int32, melange::CCurve*> CurvesByAttribute;

			melange::BaseTime MinStartTime(FLT_MAX);
			melange::BaseTime MaxEndTime(-FLT_MAX);

			// Get tracks for all animated properties
			for (melange::CTrack* Track = Tag->GetFirstCTrack(); Track; Track = Track->GetNext())
			{
				melange::DescID TrackDescID = Track->GetDescriptionID();
				int32 Depth = TrackDescID.GetDepth();
				if (Depth != 1)
				{
					continue;
				}
				int32 AttributeID = TrackDescID[0].id;

				melange::CCurve* Curve = Track->GetCurve();
				if (!Curve || Curve->GetKeyCount() == 0)
				{
					continue;
				}

				MinStartTime = FMath::Min(MinStartTime, Curve->GetStartTime());
				MaxEndTime = FMath::Max(MaxEndTime, Curve->GetEndTime());

				CurvesByAttribute.Add(AttributeID, Curve);
			}

			// Bake every frame
			// We could get just the frames where at least one attribute has been keyed, but
			// the default is to have a sigmoid interpolation anyway, which means that the final
			// transform will almost always need to be baked frame-by-frame. We might as well
			// keep things simple
			int32 FirstFrame = MinStartTime.GetFrame(MelangeFPS);
			int32 LastFrame = MaxEndTime.GetFrame(MelangeFPS);
			for (int32 FrameNumber = FirstFrame; FrameNumber <= LastFrame; ++FrameNumber)
			{
				melange::BaseTime FrameTime = melange::BaseTime(MinStartTime.Get() + melange::Float((FrameNumber-FirstFrame) * (1.0 / MelangeFPS)));

				// Construct the FCraneCameraAttributes struct for this frame
				FCraneCameraAttributes AttributesForFrame = **FoundAttributes;
				for (const TPair<int, melange::CCurve*>& Pair : CurvesByAttribute)
				{
					const melange::CCurve* AttributeCurve = Pair.Value;
					const int32& AttributeID = Pair.Key;
					double AttributeValue = AttributeCurve->GetValue(FrameTime);

					AttributesForFrame.SetAttributeByID(AttributeID, AttributeValue);
				}

				// Note: bCompensatePitch and bCompensateHeading will also be fetched but as of SDK version 20.0_259890
				// the actual CCurve and tag attribute seem to always have false for them, regardless of whether these
				// options are checked or not in C4D. So we restore them to what is the frame-zero value for this camera,
				// which can be fetched correctly
				AttributesForFrame.bCompensatePitch = (*FoundAttributes)->bCompensatePitch;
				AttributesForFrame.bCompensateHeading = (*FoundAttributes)->bCompensateHeading;

				FTransform TransformForFrame = CalculateCraneCameraTransform(AttributesForFrame);
				FVector Translation = TransformForFrame.GetTranslation();
				FVector RotationEuler = TransformForFrame.GetRotation().Euler();

				for (int32 Component = 0; Component < 3; ++Component)
				{
					AddFrameValueToAnimMap(
						Object,
						FrameNumber,
						Component,
						EDatasmithTransformType::Translation,
						Translation[Component],
						melange::ID_BASEOBJECT_REL_POSITION,
						InitialSize,
						TransformFrames,
						InitialValues);

					AddFrameValueToAnimMap(
						Object,
						FrameNumber,
						Component,
						EDatasmithTransformType::Rotation,
						FMath::DegreesToRadians(RotationEuler[Component]),
						melange::ID_BASEOBJECT_REL_ROTATION,
						InitialSize,
						TransformFrames,
						InitialValues);
				}
			}
		}
		// TODO: CraneCameras can also have an AlignToSpline tag, so that the crane camera
		// base moves along the spline. We don't support that for now
		else if (TagType == Taligntospline)
		{
			melange::SplineObject* SplineObj = static_cast<melange::SplineObject*>(MelangeGetLink(Tag, melange::ALIGNTOSPLINETAG_LINK));
			if (!SplineObj)
			{
				continue;
			}

			TArray<FRichCurve>* FoundSpline = SplineCurves.Find(SplineObj);
			if (!FoundSpline)
			{
				UE_LOG(LogDatasmithC4DImport, Error, TEXT("Did not find target spline object '%s' for %s's AlignToSpline animation!"), *MelangeObjectName(SplineObj), *MelangeObjectName(Object));
				continue;
			}

			for (melange::CTrack* Track = Tag->GetFirstCTrack(); Track; Track=Track->GetNext())
			{
				melange::DescID TrackDescID = Track->GetDescriptionID();

				int32 Depth = TrackDescID.GetDepth();
				if (Depth != 1)
				{
					continue;
				}

				melange::Int32 MelangeTransformType = TrackDescID[0].id;
				if (MelangeTransformType != melange::ALIGNTOSPLINETAG_POSITION)
				{
					continue;
				}

				melange::CCurve* Curve = Track->GetCurve();
				if (!Curve)
				{
					continue;
				}

				// We need to bake every keyframe, as we need to eval the richcurves for the spline position
				melange::BaseTime StartTime = Curve->GetStartTime();
				melange::BaseTime EndTime = Curve->GetEndTime();
				int32 FirstFrame = StartTime.GetFrame(MelangeFPS);
				int32 LastFrame = EndTime.GetFrame(MelangeFPS);
				for (int32 FrameNumber = FirstFrame; FrameNumber <= LastFrame; ++FrameNumber)
				{
					// Uses the timing curve to find the percentage of the spline path at which we must sample
					// (e.g. 0.0 -> start; 0.5 -> middle; 1.0 -> end)
					float Percent = (float)Curve->GetValue(melange::BaseTime(StartTime.Get() + melange::Float((FrameNumber-FirstFrame) * (1.0 / MelangeFPS))));

					// Target spline point in our local space
					melange::Vector Location = WorldToLocal * melange::Vector((*FoundSpline)[0].Eval(Percent),
																			  (*FoundSpline)[1].Eval(Percent),
																			  (*FoundSpline)[2].Eval(Percent));
					for (int32 Component = 0; Component < 3; ++Component)
					{
						float ComponentValue = (float)Location[Component];
						AddFrameValueToAnimMap(
							Object,
							FrameNumber,
							Component,
							EDatasmithTransformType::Translation,
							ComponentValue,
							melange::ID_BASEOBJECT_REL_POSITION,
							InitialSize,
							TransformFrames,
							InitialValues);
					}
				}
			}
		}
	}

	// Get the last point in time where we have a valid key
	melange::BaseTime MaxTime(-1.0);
	for(melange::CTrack* Track = Object->GetFirstCTrack(); Track; Track=Track->GetNext())
	{
		melange::DescID TrackDescID = Track->GetDescriptionID();
		if (TrackDescID.GetDepth() != 2)
		{
			continue;
		}

		if (TrackDescID[1].id != melange::VECTOR_X && TrackDescID[1].id != melange::VECTOR_Y && TrackDescID[1].id != melange::VECTOR_Z)
		{
			continue;
		}

		if (melange::CCurve* Curve = Track->GetCurve())
		{
			MaxTime = FMath::Max(MaxTime, Curve->GetEndTime());
		}
	}

	// Import animations on the object's attributes
	for(melange::CTrack* Track = Object->GetFirstCTrack(); Track; Track=Track->GetNext())
	{
		melange::DescID TrackDescID = Track->GetDescriptionID();
		if (TrackDescID.GetDepth() != 2)
		{
			continue;
		}

		int32 TransformVectorIndex;
		switch (TrackDescID[1].id)
		{
		case melange::VECTOR_X: TransformVectorIndex = 0; break;
		case melange::VECTOR_Y: TransformVectorIndex = 1; break;
		case melange::VECTOR_Z: TransformVectorIndex = 2; break;
		default: continue;
		}

		EDatasmithTransformType TransformType;
		melange::Int32 MelangeTransformType = TrackDescID[0].id;
		switch (MelangeTransformType)
		{
		case melange::ID_BASEOBJECT_REL_POSITION: TransformType = EDatasmithTransformType::Translation; break;
		case melange::ID_BASEOBJECT_REL_ROTATION: TransformType = EDatasmithTransformType::Rotation; break;
		case 1100: /*size*/
		case melange::ID_BASEOBJECT_REL_SCALE:	  TransformType = EDatasmithTransformType::Scale; break;
		default: continue;
		}

		melange::CCurve* Curve = Track->GetCurve();
		if (!Curve)
		{
			continue;
		}

		for (melange::Int32 KeyIndex = 0; KeyIndex < Curve->GetKeyCount(); KeyIndex++)
		{
			melange::CKey* CurrentKey = Curve->GetKey(KeyIndex);
			melange::CINTERPOLATION Interpolation = CurrentKey->GetInterpolation();

			int32 FrameNumber = CurrentKey->GetTime().GetFrame(MelangeFPS);
			melange::Float FrameValue = CurrentKey->GetValue();
			AddFrameValueToAnimMap(
				Object,
				FrameNumber,
				TransformVectorIndex,
				TransformType,
				FrameValue,
				MelangeTransformType,
				InitialSize,
				TransformFrames,
				InitialValues);

			if (Interpolation != melange::CINTERPOLATION_LINEAR && KeyIndex < Curve->GetKeyCount() - 1)
			{
				//"Bake" the animation by generating a key for each frame between this Key and the next one
				melange::CKey* NextKey = Curve->GetKey(KeyIndex+1);
				melange::BaseTime CurrentKeyTime = CurrentKey->GetTime();
				melange::BaseTime NextKeyTime = NextKey->GetTime();
				int32 NextKeyFrameNumber = NextKeyTime.GetFrame(MelangeFPS);
				int32 FrameCount = NextKeyFrameNumber - FrameNumber;
				melange::Float ElapsedTime = NextKeyTime.Get() - CurrentKeyTime.Get();
				for (int32 FrameIndex = 1; FrameIndex < FrameCount; FrameIndex++)
				{
					FrameNumber++;
					FrameValue = Curve->GetValue(melange::BaseTime(CurrentKeyTime.Get() + ((ElapsedTime / FrameCount) * FrameIndex)));
					AddFrameValueToAnimMap(
						Object,
						FrameNumber,
						TransformVectorIndex,
						TransformType,
						FrameValue,
						MelangeTransformType,
						InitialSize,
						TransformFrames,
						InitialValues);
				}
			}
		}

		// Make sure the transform frame values remain at their last valid value up until the end of the
		// animation. We use FVectors to store all three components at once, if we don't do this we will
		// incorrectly think that components whose animation curves end early have gone back to zero
		melange::Float LastValue = Curve->GetValue(Curve->GetEndTime());
		melange::Int32 FirstFrameToFill = Curve->GetEndTime().GetFrame(MelangeFPS) + 1;
		melange::Int32 LastFrameToFill = MaxTime.GetFrame(MelangeFPS);
		for (melange::Int32 Frame = FirstFrameToFill; Frame <= LastFrameToFill; ++Frame)
		{
			AddFrameValueToAnimMap(
				Object,
				Frame,
				TransformVectorIndex,
				TransformType,
				LastValue,
				MelangeTransformType,
				InitialSize,
				TransformFrames,
				InitialValues);
		}
	}

	// No tags or object attribute animations
	if (TransformFrames.Num() == 0)
	{
		return;
	}

	// Prevent actor from being optimized away
	NamesOfAnimatedActors.Add(ActorElement->GetName());

	// Add a visibility track to simulate the particle spawning and despawning, if this is a particle actor.
	// It seems like the particles have keys where they are visible: Before the first key the particles haven't
	// spawned yet, and after the last key the particles disappear.
	if (ParticleActors.Contains(Object))
	{
		int32 FirstFrameAdded = MAX_int32;
		int32 LastFrameAdded = -1;
		for (const TPair<int32, TMap<EDatasmithTransformType, FVector>>& Pair : TransformFrames)
		{
			LastFrameAdded = FMath::Max(LastFrameAdded, Pair.Key);
			FirstFrameAdded = FMath::Min(FirstFrameAdded, Pair.Key);
		}

		TSharedRef<IDatasmithVisibilityAnimationElement> VisibilityAnimation = FDatasmithSceneFactory::CreateVisibilityAnimation(ActorElement->GetName());

		// Before our first frame we should be invisible
		if (FirstFrameAdded != 0)
		{
			VisibilityAnimation->AddFrame(FDatasmithVisibilityFrameInfo(0, false));
		}

		// We're always visible during our animation
		VisibilityAnimation->AddFrame(FDatasmithVisibilityFrameInfo(FirstFrameAdded, true));
		VisibilityAnimation->AddFrame(FDatasmithVisibilityFrameInfo(LastFrameAdded, true));

		// After our last frame we should be visible, but don't add a new key if that is also the last frame of the document
		melange::GeData Data;
		if (C4dDocument->GetParameter(melange::DOCUMENT_MAXTIME, Data) && Data.GetType() == melange::DA_TIME)
		{
			const melange::BaseTime& Time = Data.GetTime();
			int32 LastDocumentFrame = Time.GetFrame(MelangeFPS);

			if (LastFrameAdded < LastDocumentFrame)
			{
				VisibilityAnimation->AddFrame(FDatasmithVisibilityFrameInfo(LastFrameAdded+1, false));
			}
		}

		LevelSequence->AddAnimation(VisibilityAnimation);
	}

	TSharedRef< IDatasmithTransformAnimationElement > Animation = FDatasmithSceneFactory::CreateTransformAnimation(ActorElement->GetName());
	for (int32 TransformTypeIndex = 0; TransformTypeIndex < 3; TransformTypeIndex++)
	{
		EDatasmithTransformType TransFormType;
		switch (TransformTypeIndex)
		{
		case 0: TransFormType = EDatasmithTransformType::Translation; break;
		case 1: TransFormType = EDatasmithTransformType::Rotation; break;
		default: TransFormType = EDatasmithTransformType::Scale;
		}

		FVector* LastValue = InitialValues.Find(TransFormType);
		for (auto& FrameValues : TransformFrames)
		{
			FVector* TransformValue = FrameValues.Value.Find(TransFormType);
			if (!TransformValue)
			{
				TransformValue = LastValue;
			}
			else
			{
				LastValue = TransformValue;
			}
			FVector ConvertedValue = *TransformValue;
			if (TransFormType == EDatasmithTransformType::Scale)
			{
				ConvertedValue = FVector(TransformValue->X, TransformValue->Z, TransformValue->Y);
			}
			else if (TransFormType == EDatasmithTransformType::Translation)
			{
				ConvertedValue = ConvertMelangeDirection(*TransformValue);
			}
			else if (TransFormType == EDatasmithTransformType::Rotation)
			{
				// Copy as we might be reusing a LastValue
				FVector TransformValueCopy = *TransformValue;

				// If the object is in the HPB rotation order, melange will store its euler rotation
				// as "H, P, B", basically storing the rotations as "YXZ". Lets switch it back to XYZ
				if (RotationOrder == melange::ROTATIONORDER_HPB)
				{
					Swap(TransformValueCopy.X, TransformValueCopy.Y);
				}

				// TransformValue represents, in radians, the rotations around the C4D axes
				// XRot, YRot, ZRot are rotations around UE4 axes, in the UE4 CS, with the sign given by Quaternion rotations (NOT Rotators)
				FQuat XRot = FQuat(FVector(1, 0, 0), -TransformValueCopy.X);
				FQuat YRot = FQuat(FVector(0, 1, 0),  TransformValueCopy.Z);
				FQuat ZRot = FQuat(FVector(0, 0, 1), -TransformValueCopy.Y);

				// Swap YRot and ZRot in the composition order, as an XYZ order in the C4D CS really means a XZY order in the UE4 CS
				// This effectively converts the rotation order from the C4D CS to the UE4 CS, the sign of the rotations being handled when
				// creating the FQuats
				Swap(YRot, ZRot);

				FQuat FinalQuat;
				switch (RotationOrder)
				{
				case melange::ROTATIONORDER_XZYGLOBAL:
					FinalQuat = YRot * ZRot * XRot;
					break;
				case melange::ROTATIONORDER_XYZGLOBAL:
					FinalQuat = ZRot * YRot * XRot;
					break;
				case melange::ROTATIONORDER_YZXGLOBAL:
					FinalQuat = XRot * ZRot * YRot;
					break;
				case melange::ROTATIONORDER_ZYXGLOBAL:
					FinalQuat = XRot * YRot * ZRot;
					break;
				case melange::ROTATIONORDER_YXZGLOBAL:
					FinalQuat = ZRot * XRot * YRot;
					break;
				case melange::ROTATIONORDER_ZXYGLOBAL:
				case melange::ROTATIONORDER_HPB:
				default:
					FinalQuat = YRot * XRot * ZRot;
					break;
				}

				// In C4D cameras and lights shoot towards +Z, but in UE4 they shoot towards +X, so fix that with a yaw
				if (Object->GetType() == Olight || Object->GetType() == Ocamera)
				{
					FinalQuat = FinalQuat * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(-90.0f));
				}

				ConvertedValue = FinalQuat.Euler();
			}
			Animation->AddFrame(TransFormType, FDatasmithTransformFrameInfo(FrameValues.Key, ConvertedValue));
		}
	}

	LevelSequence->AddAnimation(Animation);
}

void FDatasmithC4DImporter::ImportActorHierarchyAnimations(TSharedPtr<IDatasmithActorElement> ActorElement)
{
	for (int32 ChildIndex = 0; ChildIndex < ActorElement->GetChildrenCount(); ++ChildIndex)
	{
		TSharedPtr<IDatasmithActorElement> ChildActorElement = ActorElement->GetChild(ChildIndex);

		ImportAnimations(ChildActorElement);
		ImportActorHierarchyAnimations(ChildActorElement);
	}
}

TSharedPtr<IDatasmithActorElement> FDatasmithC4DImporter::ImportObjectAndChildren(melange::BaseObject* ActorObject, melange::BaseObject* DataObject, TSharedPtr<IDatasmithActorElement> ParentActor, const melange::Matrix& WorldTransformMatrix, TMap<FString, melange::PolygonObject*>* ClonerBaseChildrenHash, const FString& InstancePath, TArray<melange::BaseObject*>* InstanceObjects, TArray<melange::TextureTag*> TextureTags, const FString& DatasmithLabel)
{
	TSharedPtr<IDatasmithActorElement> ActorElement;
	melange::Int32 ObjectType = DataObject->GetType();
	melange::BaseObject* ActorCache = GetBestMelangeCache(ActorObject);
	melange::BaseObject* DataCache = GetBestMelangeCache(DataObject);
	if (!DataCache)
	{
		DataCache = ActorCache;
	}
	else if (!ActorCache)
	{
		ActorCache = DataCache;
	}
	FString DatasmithName = MelangeObjectID(ActorObject);
	if (!InstancePath.IsEmpty())
	{
		DatasmithName = MD5FromString(InstancePath) + "_" + DatasmithName;
	}

	//Get all texture tags
	melange::BaseTag* Tag = ActorObject->GetFirstTag();
	while (Tag)
	{
		melange::Int32 TagType = Tag->GetType();
		if (TagType == Ttexture)
		{
			TextureTags.Add(static_cast<melange::TextureTag*>(Tag));
		}
		Tag = Tag->GetNext();
	}

	melange::Matrix NewWorldTransformMatrix = WorldTransformMatrix * ActorObject->GetMl();

	// Fetch actor layer
	FString TargetLayerName;
	bool bActorVisible = true;
	if (melange::LayerObject* LayerObject = static_cast<melange::LayerObject*>(MelangeGetLink(ActorObject, melange::ID_LAYER_LINK)))
	{
		// Do not create actors from invisible layers
		// We may end up creating null actors if the actor is in an invisible layer, and even
		// continuing to import the hierarchy below. This because in C4D if the child is not
		// in the invisible layer, it can actually be visible, and we need to maintain correct transforms
		// and so on.
		// Exceptions are generators: If a cloner is in an invisible layer, the child nodes are always invisible,
		// and also if the cloner is in a visible layer, the child nodes are always visible
		bActorVisible = MelangeGetBool(LayerObject, melange::ID_LAYER_VIEW);

		TargetLayerName = MelangeObjectName(LayerObject);
	}

	if (bActorVisible)
	{
		try
		{
			if (InstanceObjects)
			{
				DatasmithC4DImportCheck(InstanceObjects->Num() > 0);
				melange::BaseObject* RealDataObject = (*InstanceObjects)[0];
				InstanceObjects->RemoveAt(0);
				DatasmithC4DImportCheck(RealDataObject->GetType() == ObjectType);
				DataObject = RealDataObject;
			}

			if (ObjectType == Oinstance)
			{
				melange::BaseObject* InstanceLink = static_cast<melange::BaseObject*>(MelangeGetLink(DataObject, melange::INSTANCEOBJECT_LINK));
				DatasmithC4DImportCheck(InstanceLink != nullptr);
				TArray<melange::BaseObject*> CurentInstanceObjects = GetMelangeInstanceObjects(InstanceLink);
				melange::Int32 LinkObjectType = InstanceLink->GetType();
				if (ActorCache)
				{
					// Parse our own duplicated hierarchy (which is a replica of the original object's hierarchy),
					// carrying our own texture tags. If we jump through InstanceLink, we'll be parsing the original
					// hierarchy, so any animations or polygons we parse will be bound to the original actors (not
					// our replica actors)
					return ImportObjectAndChildren(ActorObject, ActorCache, ParentActor, WorldTransformMatrix, ClonerBaseChildrenHash, MelangeObjectID(DataObject) + InstancePath, &CurentInstanceObjects, TextureTags, DatasmithLabel);
				}
				else
				{
					return ImportObjectAndChildren(ActorObject, InstanceLink, ParentActor, WorldTransformMatrix, ClonerBaseChildrenHash, MelangeObjectID(DataObject) + InstancePath, &CurentInstanceObjects, TextureTags, DatasmithLabel);
				}
			}

			// For particle emitters, we need to mark all the child actors, as those need to have their visibility
			// manually animated to simulate mesh particles spawning and despawning
			if (ObjectType == Oparticle)
			{
				MarkActorsAsParticles(ActorObject, ActorCache);
			}

			if (ObjectType == Ocloner || ObjectType == Oarray)
			{
				//Cloner(Ocloner)
				//	| -CACHE: Null(Onull)
				//	| | -Cube 2(Ocube)
				//	| | | -CACHE: Cube 2(Opolygon)
				//	| | -Cube 1(Ocube)
				//	| | | -CACHE: Cube 1(Opolygon)
				//	| | -Cube 0(Ocube)
				//	| | | -CACHE: Cube 0(Opolygon)
				//	| -Cube(Ocube)

				melange::GeData Data;
				if (ObjectType == Ocloner && MelangeGetInt32(ActorObject, melange::MGCLONER_VOLUMEINSTANCES_MODE) != 0)
				{
					// Render/Multi-instance cloner should be ignored
					UE_LOG(LogDatasmithC4DImport, Warning, TEXT("Render-instance or multi-instance Cloners are not supported. Actor '%s' will be ignored"), *MelangeObjectName(ActorObject));
				}
				else if (ObjectType == Oarray && MelangeGetInt32(ActorObject, melange::ARRAYOBJECT_RENDERINSTANCES) != 0)
				{
					// Render-instance arrays should be ignored
					UE_LOG(LogDatasmithC4DImport, Warning, TEXT("Render-instance Arrays are not supported. Actor '%s' will be ignored"), *MelangeObjectName(ActorObject));
				}
				else
				{
					DatasmithC4DImportCheck(DataCache != nullptr);
					DatasmithC4DImportCheck(DataCache->GetType() == Onull);
					ActorElement = ImportNullActor(ActorObject, DatasmithName, DatasmithLabel);
					AddChildActor(ActorObject, ParentActor, NewWorldTransformMatrix, ActorElement);
					TMap<FString, melange::PolygonObject*> ThisClonerBaseChildrenHash;
					ImportHierarchy(ActorCache->GetDown(), DataCache->GetDown(), ActorElement, NewWorldTransformMatrix, &ThisClonerBaseChildrenHash, InstancePath, nullptr, TextureTags);
					return ActorElement;
				}

			}
			else if (ObjectType == Ofracture || ObjectType == ID_MOTIONFRACTUREVORONOI || ObjectType == Osymmetry || ObjectType == Osds /*Sub Division Surface*/ || ObjectType == Oboole)
			{
				DatasmithC4DImportCheck(DataCache != nullptr);
				ActorElement = ImportNullActor(ActorObject, DatasmithName + "0"/*to be different than the cache root*/, DatasmithLabel);
				AddChildActor(ActorObject, ParentActor, NewWorldTransformMatrix, ActorElement);
				ImportObjectAndChildren(ActorCache, DataCache, ActorElement, NewWorldTransformMatrix, ClonerBaseChildrenHash, InstancePath, nullptr, TextureTags, DatasmithLabel);
				return ActorElement;
			}
			else if (ObjectType == Ospline)
			{
				if (melange::SplineObject* Spline = static_cast<melange::SplineObject*>(ActorObject))
				{
					ImportSpline(Spline);
				}
			}
			else if (ActorCache)
			{
				ActorElement = ImportObjectAndChildren(ActorCache, DataCache, nullptr, ActorCache->GetMg(), ClonerBaseChildrenHash, InstancePath, nullptr, TextureTags, DatasmithLabel);
			}
			else if (ObjectType == Opolygon)
			{
				melange::PolygonObject* PolygonObject = static_cast<melange::PolygonObject*>(DataObject);
				if (Options->bImportEmptyMesh || PolygonObject->GetPolygonCount() > 0)
				{
					ActorElement = ImportPolygon(static_cast<melange::PolygonObject*>(DataObject), ClonerBaseChildrenHash, DatasmithName, DatasmithLabel, TextureTags);
				}
			}
			else if (ObjectType == Ocamera)
			{
				ActorElement = ImportCamera(DataObject, DatasmithName, DatasmithLabel);
			}
			else if (ObjectType == Olight)
			{
				ActorElement = ImportLight(DataObject, DatasmithName, DatasmithLabel);
			}
		}
		catch (const DatasmithC4DImportException& Exception)
		{
			UE_LOG(LogDatasmithC4DImport, Error, TEXT("Could not import the object \"%s\": %s"), *MelangeObjectName(ActorObject), *Exception.GetMessage());
		}
	}

	try
	{
		if (!ActorElement.IsValid())
		{
			ActorElement = ImportNullActor(ActorObject, DatasmithName, DatasmithLabel);
		}

		if (ParentActor)
		{
			AddChildActor(ActorObject, ParentActor, NewWorldTransformMatrix, ActorElement);
		}

		// Invisible layers will not be imported, so don't use their names
		if (bActorVisible)
		{
			ActorElement->SetLayer(*TargetLayerName);
		}
	}
	catch (const DatasmithC4DImportException& Exception)
	{
		UE_LOG(LogDatasmithC4DImport, Error, TEXT("Could not create the actor for the object \"%s\": %s"), *MelangeObjectName(ActorObject), *Exception.GetMessage());
	}

	ImportHierarchy(ActorObject->GetDown(), DataObject->GetDown(), ActorElement, NewWorldTransformMatrix, ClonerBaseChildrenHash, InstancePath, InstanceObjects, TextureTags);

	return ActorElement;
}

void FDatasmithC4DImporter::ImportHierarchy(melange::BaseObject* ActorObject, melange::BaseObject* DataObject, TSharedPtr<IDatasmithActorElement> ParentActor, const melange::Matrix& WorldTransformMatrix, TMap<FString, melange::PolygonObject*>* ClonerBaseChildrenHash, const FString& InstancePath, TArray<melange::BaseObject*>* InstanceObjects, const TArray<melange::TextureTag*>& TextureTags)
{
	while (ActorObject || DataObject)
	{
		if (!DataObject)
		{
			DataObject = ActorObject;
		}
		else if (!ActorObject)
		{
			ActorObject = DataObject;
		}

		bool SkipObject = false;
		for (melange::BaseTag* Tag = ActorObject->GetFirstTag(); Tag; Tag = Tag->GetNext())
		{
			if (Tag->GetType() == Tannotation)
			{
				FString AnnotationLabel = MelangeGetString(Tag, 10014);
				if (AnnotationLabel.Compare("EXCLUDE", ESearchCase::IgnoreCase) == 0)
				{
					SkipObject = true;
					break;
				}
			}
		}

		if (!SkipObject)
		{
			FString DatasmithLabel = FDatasmithUtils::SanitizeObjectName(MelangeObjectName(ActorObject));
			ImportObjectAndChildren(ActorObject, DataObject, ParentActor, WorldTransformMatrix, ClonerBaseChildrenHash, InstancePath, InstanceObjects, TextureTags, DatasmithLabel);
		}

		ActorObject = ActorObject->GetNext();
		DataObject = DataObject->GetNext();
	}
}

TSharedPtr<IDatasmithMeshElement> FDatasmithC4DImporter::ImportMesh(melange::PolygonObject* PolyObject, const FString& DatasmithMeshName, const FString& DatasmithLabel, const TArray<melange::TextureTag*>& TextureTags)
{
	melange::Int32 PointCount = PolyObject->GetPointCount();
	melange::Int32 PolygonCount = PolyObject->GetPolygonCount();

	const melange::Vector* Points = PolyObject->GetPointR();
	const melange::CPolygon* Polygons = PolyObject->GetPolygonR();

	// Get vertex normals
	melange::Vector32* Normals = nullptr;
	if (PolyObject->GetTag(Tphong))
	{
		Normals = PolyObject->CreatePhongNormals();
	}

	// Collect all UV channels and material slot information for this PolygonObject
	TArray<melange::ConstUVWHandle> UVWTagsData;
	TMap<FString, melange::BaseSelect*> SelectionTags;
	for (melange::BaseTag* Tag = PolyObject->GetFirstTag(); Tag; Tag = Tag->GetNext())
	{
		melange::Int32 TagType = Tag->GetType();
		if (TagType == Tuvw)
		{
			melange::UVWTag* UVWTag = static_cast<melange::UVWTag*>(Tag);
			DatasmithC4DImportCheck(UVWTag->GetDataCount() == PolygonCount);
			UVWTagsData.Add(UVWTag->GetDataAddressR());
		}
		else if (TagType == Tpolygonselection)
		{
			FString SelectionName = MelangeGetString(Tag, melange::POLYGONSELECTIONTAG_NAME);
			if (!SelectionName.IsEmpty())
			{
				SelectionTags.Add(SelectionName, static_cast<melange::SelectionTag*>(Tag)->GetBaseSelect());
			}
		}
	}

	// Create MeshDescription
	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);
	MeshDescription.Empty();

	FStaticMeshAttributes StaticMeshAttributes(MeshDescription);
	TVertexAttributesRef<FVector> VertexPositions = StaticMeshAttributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = StaticMeshAttributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = StaticMeshAttributes.GetVertexInstanceUVs();
	TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames();

	// Reserve space for attributes. These might not be enough as some of these polygons might be quads or n-gons, but its better than nothing
	MeshDescription.ReserveNewVertices(PointCount);
	MeshDescription.ReserveNewVertexInstances(PolygonCount);
	MeshDescription.ReserveNewEdges(PolygonCount);
	MeshDescription.ReserveNewPolygons(PolygonCount);
	MeshDescription.ReserveNewPolygonGroups(SelectionTags.Num() + 1);

	// At least one UV set must exist.
	int32 UVChannelCount = UVWTagsData.Num();
	VertexInstanceUVs.SetNumIndices(FMath::Max(1, UVChannelCount));

	// Vertices
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		FVertexID NewVertexID = MeshDescription.CreateVertex();
		// We count on this check when creating polygons
		check(NewVertexID.GetValue() == PointIndex);

		VertexPositions[NewVertexID] = ConvertMelangePosition(Points[PointIndex]);
	}

	// Auxiliary stuff to help with polygon material assignment and material slots
	int32 MaterialCounter = 0;
	TMap<melange::TextureTag*, int32> TextureTagToMaterialSlot;
	TMap<int32, FPolygonGroupID> MaterialSlotToPolygonGroup;
	auto GetOrCreatePolygonGroupId = [&](int32 MaterialIndex) -> FPolygonGroupID
	{
		FPolygonGroupID& PolyGroupId = MaterialSlotToPolygonGroup.FindOrAdd(MaterialIndex);
		if (PolyGroupId == FPolygonGroupID::Invalid)
		{
			PolyGroupId = MeshDescription.CreatePolygonGroup();
			FName ImportedSlotName = DatasmithMeshHelper::DefaultSlotName(MaterialIndex);
			PolygonGroupImportedMaterialSlotNames[PolyGroupId] = ImportedSlotName;
		}
		return PolyGroupId;
	};

	// Vertex indices in a quad or a triangle
	TArray<int32> QuadIndexOffsets = { 0, 1, 3, 1, 2, 3 };
	TArray<int32> TriangleIndexOffsets = { 0, 1, 2 };
	TArray<int32>* IndexOffsets;

	// We have to pass 3 instance IDs at a time to CreatePolygon, so we must copy
	TArray<FVertexInstanceID> IDsCopy;
	IDsCopy.SetNumUninitialized(3);
	TArray<FVector> QuadNormals;
	QuadNormals.SetNumZeroed(4);
	TArray<FVector2D> QuadUVs;
	QuadUVs.SetNumZeroed(4);

	// Just used to check for regenerate triangles
	TArray<FVector> TriangleVertexPositions;
	TriangleVertexPositions.SetNumZeroed(3);

	// Create polygons
	for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
	{
		const melange::CPolygon& Polygon = Polygons[PolygonIndex];

		// Check if we're a triangle or a quad
		if (Polygon.c == Polygon.d)
		{
			IndexOffsets = &TriangleIndexOffsets;
		}
		else
		{
			IndexOffsets = &QuadIndexOffsets;
		}

		// Vertex instances
		TArray<FVertexInstanceID> VertexInstances;
		for (int32 VertexIndexOffset : *IndexOffsets)
		{
			FVertexID VertexID = FVertexID(Polygon[VertexIndexOffset]);
			FVertexInstanceID InstanceID = MeshDescription.CreateVertexInstance(VertexID);

			VertexInstances.Add(InstanceID);
		}

		// Fetch melange polygon normals (always 4, even if triangle)
		if (Normals)
		{
			for (int32 VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
			{
				QuadNormals[VertexIndex] = ConvertMelangeDirection(Normals[PolygonIndex * 4 + VertexIndex]);
			}
			// Set normals
			for (int32 VertexCount = 0; VertexCount < VertexInstances.Num(); ++VertexCount)
			{
				FVertexInstanceID& VertInstanceID = VertexInstances[VertexCount];
				int32 VertexIDInQuad = (*IndexOffsets)[VertexCount];

				VertexInstanceNormals.Set(VertInstanceID, QuadNormals[VertexIDInQuad]);
			}
		}

		// UVs
		for (int32 ChannelIndex = 0; ChannelIndex < UVChannelCount; ++ChannelIndex)
		{
			melange::ConstUVWHandle UVWTagData = UVWTagsData[ChannelIndex];
			melange::UVWStruct UVWStruct;
			melange::UVWTag::Get(UVWTagData, PolygonIndex, UVWStruct);
			melange::Vector* UVs = &UVWStruct.a;

			// Fetch melange UVs
			for (int32 VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
			{
				melange::Vector& PointUVs = UVs[VertexIndex];
				FVector2D& UnrealUVs = QuadUVs[VertexIndex];

				if (PointUVs.z != 0.0f && PointUVs.z != 1.0f)
				{
					UnrealUVs.X = static_cast<float>(PointUVs.x / PointUVs.z);
					UnrealUVs.Y = static_cast<float>(PointUVs.y / PointUVs.z);
				}
				else
				{
					UnrealUVs.X = static_cast<float>(PointUVs.x);
					UnrealUVs.Y = static_cast<float>(PointUVs.y);
				}

				if (UnrealUVs.ContainsNaN())
				{
					UnrealUVs.Set(0, 0);
				}
			}
			// Set UVs
			for (int32 VertexCount = 0; VertexCount < VertexInstances.Num(); ++VertexCount)
			{
				FVertexInstanceID& VertInstanceID = VertexInstances[VertexCount];
				int32 VertexIDInQuad = (*IndexOffsets)[VertexCount];

				VertexInstanceUVs.Set(VertInstanceID, ChannelIndex, QuadUVs[VertexIDInQuad]);
			}
		}

		// TextureTag
		// Iterate backwards because the last valid texture tag is the one that is actually applied
		melange::TextureTag* PolygonTextureTag = nullptr;
		for (int32 TextureTagIndex = TextureTags.Num() - 1; TextureTagIndex >= 0; --TextureTagIndex)
		{
			melange::TextureTag* TextureTag = TextureTags[TextureTagIndex];

			melange::BaseSelect** SelectionInMap = nullptr;
			FString TextureSelectionTag = MelangeGetString(TextureTag, melange::TEXTURETAG_RESTRICTION);
			if (!TextureSelectionTag.IsEmpty())
			{
				SelectionInMap = SelectionTags.Find(TextureSelectionTag);
			}

			if (TextureSelectionTag.IsEmpty() || (SelectionInMap && (*SelectionInMap)->IsSelected(PolygonIndex)))
			{
				PolygonTextureTag = TextureTag;

				// We just need the "last valid" one
				break;
			}
		}

		// MaterialIndex from TextureTag
		int32 MaterialIndex = -1;
		if (int32* FoundMaterialIndex = TextureTagToMaterialSlot.Find(PolygonTextureTag))
		{
			MaterialIndex = *FoundMaterialIndex;
		}
		else
		{
			MaterialIndex = MaterialCounter++;
			TextureTagToMaterialSlot.Add(PolygonTextureTag, MaterialIndex);
		}

		// Create a triangle for each 3 vertex instance IDs we have
		check(VertexInstances.Num() % 3 == 0);
		for (int32 TriangleIndex = 0; TriangleIndex < VertexInstances.Num() / 3; ++TriangleIndex)
		{
			FMemory::Memcpy(IDsCopy.GetData(), VertexInstances.GetData() + TriangleIndex * 3, sizeof(FVertexInstanceID) * 3);

			// Invert winding order for triangles
			IDsCopy.Swap(0, 2);

			// Check if triangle is degenerate
			for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
			{
				FVertexID VertID = MeshDescription.GetVertexInstanceVertex(IDsCopy[VertexIndex]);
				TriangleVertexPositions[VertexIndex] = VertexPositions[VertID];
			}
			FVector RawNormal = ((TriangleVertexPositions[1] - TriangleVertexPositions[2]) ^ (TriangleVertexPositions[0] - TriangleVertexPositions[2]));
			if (RawNormal.SizeSquared() < SMALL_NUMBER)
			{
				continue; // this will leave holes...
			}

			const FPolygonGroupID PolygonGroupID = GetOrCreatePolygonGroupId(MaterialIndex);
			const FPolygonID NewPolygonID = MeshDescription.CreatePolygon(PolygonGroupID, IDsCopy);

			// Fill in the polygon's Triangles - this won't actually do any polygon triangulation as we always give it triangles
			MeshDescription.ComputePolygonTriangulation(NewPolygonID);
		}
	}

	int32 NumPolygons = MeshDescription.Polygons().Num();
	TArray<uint32> ZeroedFaceSmoothingMask;
	ZeroedFaceSmoothingMask.SetNumZeroed(NumPolygons);
	FMeshDescriptionOperations::ConvertSmoothGroupToHardEdges(ZeroedFaceSmoothingMask, MeshDescription);

	if (Normals)
	{
		melange::DeleteMem(Normals);
	}

	TSharedRef<IDatasmithMeshElement> MeshElement = FDatasmithSceneFactory::CreateMesh(*DatasmithMeshName);
	MeshElement->SetLabel(*DatasmithLabel);

	// Create customized materials for all the used texture tags. This because each tag
	// actually represents a material "instance", and might have different settings like texture tiling
	for (TPair<melange::TextureTag*, int32>& Pair : TextureTagToMaterialSlot)
	{
		melange::TextureTag* Tag = Pair.Key;
		int32 TargetSlot = Pair.Value;

		FString CustomizedMaterialName;
		melange::BaseList2D* TextureMaterial = Tag ? MelangeGetLink(Tag, melange::TEXTURETAG_MATERIAL) : nullptr;
		if (TextureMaterial)
		{
			// This can also return an existing material without necessarily spawning a new instance
			CustomizedMaterialName = CustomizeMaterial(GetMelangeBaseList2dID(TextureMaterial), DatasmithMeshName, Tag);
		}

		MeshElement->SetMaterial(*CustomizedMaterialName, TargetSlot);
	}

	MeshElementToMeshDescription.Add(&MeshElement.Get(), MoveTemp(MeshDescription));
	PolygonObjectToMeshElement.Add(PolyObject, MeshElement);

	DatasmithScene->AddMesh(MeshElement);
	return MeshElement;
}

void FDatasmithC4DImporter::GetGeometriesForMeshElementAndRelease(const TSharedRef<IDatasmithMeshElement> MeshElement, TArray<FMeshDescription>& OutMeshDescriptions)
{
	if (FMeshDescription* MeshDesc = MeshElementToMeshDescription.Find(&MeshElement.Get()))
	{
		OutMeshDescriptions.Add(MoveTemp(*MeshDesc));
	}
}

bool FDatasmithC4DImporter::OpenFile(const FString& InFilename)
{
	SCOPE_CYCLE_COUNTER(STAT_C4DImporter_LoadFile)

	using namespace melange;

	if (!FPaths::FileExists(InFilename))
	{
		return false;
	}

	C4dDocument = NewObj(BaseDocument);
	if (!C4dDocument)
	{
		return false;
	}

	HyperFile *C4Dfile = NewObj(HyperFile);
	if (!C4Dfile)
	{
		DeleteObj(C4dDocument);
		return false;
	}

	if (C4Dfile->Open(DOC_IDENT, TCHAR_TO_ANSI(*InFilename), FILEOPEN_READ))
	{
		bool bSuccess = C4dDocument->ReadObject(C4Dfile, true);

		int64 LastPos = static_cast<int64>(C4Dfile->GetPosition());
		int64 Length = static_cast<int64>(C4Dfile->GetLength());
		int64 Version = static_cast<int64>(C4Dfile->GetFileVersion());
		FILEERROR Error = C4Dfile->GetError();

		if (bSuccess)
		{
			UE_LOG(LogDatasmithC4DImport, Log, TEXT("Melange SDK successfully read the file '%s' (read %ld out of %ld bytes, version %ld)"), *InFilename, LastPos, Length, Version);
		}
		else
		{
			UE_LOG(LogDatasmithC4DImport, Warning, TEXT("Melange SDK did not read the entire file '%s' (read %ld out of %ld bytes, version %ld, error code: %d). Imported scene may contain errors or missing data."), *InFilename, LastPos, Length, Version, Error);
		}
	}
	else
	{
		UE_LOG(LogDatasmithC4DImport, Error, TEXT("Cannot open file '%s'"), *InFilename);
		DeleteObj(C4Dfile);
		DeleteObj(C4dDocument);
		return false;
	}

	C4dDocumentFilename = InFilename;

	C4Dfile->Close();
	DeleteObj(C4Dfile);

	return true;
}

melange::BaseObject* FDatasmithC4DImporter::FindMelangeObject(const FString& SearchObjectID, melange::BaseObject* Object)
{
	while (Object)
	{
		if (MelangeObjectID(Object) == SearchObjectID)
		{
			return Object;
		}

		melange::BaseObject* FoundObject = FindMelangeObject(SearchObjectID, Object->GetDown());
		if (FoundObject)
		{
			return FoundObject;
		}

		Object = Object->GetNext();
	}

	return nullptr;
}

melange::BaseObject* FDatasmithC4DImporter::GoToMelangeHierarchyPosition(melange::BaseObject* Object, const FString& HierarchyPosition)
{
	if (Object)
	{
		int32 SeparatorIndex = 0;
		bool SeparatorFound = HierarchyPosition.FindChar('_', SeparatorIndex);
		int32 IndexFromRoot = FCString::Atoi(SeparatorFound ? *HierarchyPosition.Left(SeparatorIndex) : *HierarchyPosition);
		while (Object && IndexFromRoot > 0)
		{
			Object = Object->GetNext();
			IndexFromRoot--;
		}

		if (SeparatorFound && HierarchyPosition.Len() > SeparatorIndex + 1)
		{
			FString NextHierarchyPosition = HierarchyPosition.Right(HierarchyPosition.Len() - SeparatorIndex - 1);
			if (NextHierarchyPosition.Left(2) == "C_")
			{
				Object = GoToMelangeHierarchyPosition(GetBestMelangeCache(Object), NextHierarchyPosition.Right(NextHierarchyPosition.Len() - 2));
			}
			else
			{
				Object = GoToMelangeHierarchyPosition(Object->GetDown(), NextHierarchyPosition);
			}
		}
	}
	return Object;
}

bool FDatasmithC4DImporter::ProcessScene()
{
	// Cinema 4D Document settings
	MelangeFPS = static_cast<melange::Float>(MelangeGetInt32(C4dDocument, melange::DOCUMENT_FPS));
	if(MelangeFPS == 0.0f)
	{
		UE_LOG(LogDatasmithC4DImport, Error, TEXT("DOCUMENT_FPS not found"));
		return false;
	}
	MelangeColorProfile = MelangeGetInt32(C4dDocument, melange::DOCUMENT_COLORPROFILE);
	melange::RenderData* RenderData = C4dDocument->GetActiveRenderData();
	if (!RenderData)
	{
		UE_LOG(LogDatasmithC4DImport, Error, TEXT("Active Render Data not found"));
		return false;
	}

	// Materials
	ImportedTextures.Empty();
	ImportMaterialHierarchy(C4dDocument->GetFirstMaterial());
	ImportedTextures.Empty();

	// Actors
	ActorMetadata.Empty();
	// Need a RootActor for RemoveEmptyActors and to make AddChildActor agnostic to actor hiearchy level
	TSharedPtr<IDatasmithActorElement> RootActor = FDatasmithSceneFactory::CreateActor(TEXT("RootActor"));
	DatasmithScene->AddActor(RootActor);
	TArray<melange::TextureTag*> TextureTags;
	ImportHierarchy(C4dDocument->GetFirstObject(), C4dDocument->GetFirstObject(), RootActor, melange::Matrix(), nullptr, "", nullptr, TextureTags);

	// Animations
	LevelSequence = FDatasmithSceneFactory::CreateLevelSequence(DatasmithScene->GetName());
	LevelSequence->SetFrameRate(static_cast<float>(MelangeFPS));
	DatasmithScene->AddLevelSequence(LevelSequence.ToSharedRef());
	ImportActorHierarchyAnimations(RootActor);

	// Processing
	KeepParentsOfAnimatedNodes(RootActor, NamesOfAnimatedActors);
	RemoveEmptyActors();
	DatasmithScene->RemoveActor(RootActor, EDatasmithActorRemovalRule::KeepChildrenAndKeepRelativeTransform);

	if (Options->bExportToUDatasmith)
	{
		SceneExporterRef = TSharedRef<FDatasmithSceneExporter>(new FDatasmithSceneExporter);
		SceneExporterRef->PreExport();
		FString SceneName = FDatasmithUtils::SanitizeFileName(FDatasmithUtils::SanitizeObjectName(FPaths::GetBaseFilename(C4dDocumentFilename)));
		SceneExporterRef->SetName(*SceneName);
		SceneExporterRef->SetOutputPath(*FPaths::GetPath(C4dDocumentFilename));
		SceneExporterRef->Export(DatasmithScene);
	}

	return true;
}

void FDatasmithC4DImporter::UnloadScene()
{
	DeleteObj(C4dDocument);
}

// Traverse the LayerObject hierarchy adding visible layer names to VisibleLayers
void RecursivelyParseLayers(melange::LayerObject* CurrentLayer, TSet<FName>& VisibleLayers)
{
	if (CurrentLayer == nullptr)
	{
		return;
	}

	FString Name = MelangeObjectName(CurrentLayer);

	melange::GeData Data;
	if (MelangeGetBool(CurrentLayer, melange::ID_LAYER_VIEW))
	{
		VisibleLayers.Add(FName(*Name));
	}

	RecursivelyParseLayers(CurrentLayer->GetDown(), VisibleLayers);
	RecursivelyParseLayers(CurrentLayer->GetNext(), VisibleLayers);
}

#undef LOCTEXT_NAMESPACE

#endif //_MELANGE_SDK_
