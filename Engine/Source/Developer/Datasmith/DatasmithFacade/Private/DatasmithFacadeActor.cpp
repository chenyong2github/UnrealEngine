// Copyright Epic Games, Inc. All Rights Reserved.

#include "DatasmithFacadeActor.h"

// OpenEXR third party library.
#include "ImathMatrixAlgo.h"


FDatasmithFacadeActor::FDatasmithFacadeActor(
	const TCHAR* InElementName,
	const TCHAR* InElementLabel
) :
	FDatasmithFacadeElement(InElementName, InElementLabel),
	bIsComponent(false),
	bOptimizeActor(true)
{
}

void FDatasmithFacadeActor::KeepActor()
{
	bOptimizeActor = false;
}

void FDatasmithFacadeActor::SetWorldTransform(
	const float* InWorldMatrix
)
{
	WorldTransform = ConvertTransform(InWorldMatrix);
}

void FDatasmithFacadeActor::SetLayer(
	const TCHAR* InLayerName
)
{
	if (!FString(InLayerName).IsEmpty())
	{
		LayerName = InLayerName;
	}
}

void FDatasmithFacadeActor::AddTag(
	const TCHAR* InTag
)
{
	FString Tag = InTag;

	if (!Tag.IsEmpty())
	{
		TagArray.Add(Tag);
	}
}

void FDatasmithFacadeActor::AddMetadataBoolean(
	const TCHAR* InPropertyName,
	bool         bInPropertyValue
)
{
	// Create a new Datasmith metadata boolean property.
	TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Bool);
	MetadataPropertyPtr->SetValue(bInPropertyValue ? TEXT("True") : TEXT("False"));

	// Add the new property to the array of Datasmith metadata properties.
	MetadataPropertyArray.Add(MetadataPropertyPtr);
}

void FDatasmithFacadeActor::AddMetadataColor(
	const TCHAR*  InPropertyName,
	unsigned char InR,
	unsigned char InG,
	unsigned char InB,
	unsigned char InA
)
{
	// Convert the sRGBA color to a Datasmith linear color.
	FColor       Color(InR, InG, InB, InA);
	FLinearColor LinearColor(Color);

	// Create a new Datasmith metadata color property.
	TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Color);
	MetadataPropertyPtr->SetValue(*LinearColor.ToString());

	// Add the new property to the array of Datasmith metadata properties.
	MetadataPropertyArray.Add(MetadataPropertyPtr);
}

void FDatasmithFacadeActor::AddMetadataFloat(
	const TCHAR* InPropertyName,
	float        InPropertyValue
)
{
	// Create a new Datasmith metadata float property.
	TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Float);
	MetadataPropertyPtr->SetValue(*FString::Printf(TEXT("%f"), InPropertyValue));

	// Add the new property to the array of Datasmith metadata properties.
	MetadataPropertyArray.Add(MetadataPropertyPtr);
}

void FDatasmithFacadeActor::AddMetadataString(
	const TCHAR* InPropertyName,
	const TCHAR* InPropertyValue
)
{
	// Create a new Datasmith metadata string property.
	TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::String);
	MetadataPropertyPtr->SetValue(InPropertyValue);

	// Add the new property to the array of Datasmith metadata properties.
	MetadataPropertyArray.Add(MetadataPropertyPtr);
}

void FDatasmithFacadeActor::AddMetadataTexture(
	const TCHAR* InPropertyName,
	const TCHAR* InTextureFilePath
)
{
	// Create a new Datasmith metadata texture property.
	TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Texture);
	MetadataPropertyPtr->SetValue(InTextureFilePath);

	// Add the new property to the array of Datasmith metadata properties.
	MetadataPropertyArray.Add(MetadataPropertyPtr);
}

void FDatasmithFacadeActor::AddMetadataVector(
	const TCHAR* InPropertyName,
	const TCHAR* InPropertyValue
)
{
	// Create a new Datasmith metadata vector property.
	TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr = FDatasmithSceneFactory::CreateKeyValueProperty(InPropertyName);
	MetadataPropertyPtr->SetPropertyType(EDatasmithKeyValuePropertyType::Vector);
	MetadataPropertyPtr->SetValue(InPropertyValue);

	// Add the new property to the array of Datasmith metadata properties.
	MetadataPropertyArray.Add(MetadataPropertyPtr);
}

void FDatasmithFacadeActor::SetIsComponent(
	bool bInIsComponent
)
{
	bIsComponent = bInIsComponent;
}

void FDatasmithFacadeActor::AddChild(
	FDatasmithFacadeActor* InChildActorPtr
)
{
	ChildActorArray.Add(TSharedPtr<FDatasmithFacadeActor>(InChildActorPtr));
}

void FDatasmithFacadeActor::SanitizeActorHierarchyNames()
{
	if (ChildActorArray.Num() > 0)
	{
		TMap<FString, int> NameCountMap;
		TMap<FString, int> NameUsageMap;

		// Count the number of times a name is reused by the Datasmith actor children.
		for (TSharedPtr<FDatasmithFacadeActor> ChildActorPtr : ChildActorArray)
		{
			FString const& ChildActorName = ChildActorPtr->ElementName;

			if (!NameCountMap.Contains(ChildActorName))
			{
				NameCountMap.Add(ChildActorName, 0);
			}

			NameCountMap[ChildActorName] ++;
		}

		// Rename with a name made unique each child of the Datasmith actor.
		for (TSharedPtr<FDatasmithFacadeActor> ChildActorPtr : ChildActorArray)
		{
			FString const& ChildActorName = ChildActorPtr->ElementName;

			if (!NameUsageMap.Contains(ChildActorName))
			{
				NameUsageMap.Add(ChildActorName, 0);
			}

			NameUsageMap[ChildActorName] ++;

			// Assign a new unique name to the Datasmith actor child.
			ChildActorPtr->ElementName = FString::Printf(TEXT("%ls.%ls_%d/%d"), *ElementName, *ChildActorName, NameUsageMap[ChildActorName], NameCountMap[ChildActorName]);

			// Make sure all the actor names are unique in the hierarchy of this Datasmith actor child.
			ChildActorPtr->SanitizeActorHierarchyNames();
		}
	}

	// Hash the Datasmith actor name to shorten it.
	HashName();
}

FTransform FDatasmithFacadeActor::ConvertTransform(
	const float* InSourceMatrix
) const
{
	// We use Imath::extractAndRemoveScalingAndShear() because FMatrix::ExtractScaling() is deemed unreliable.

	// Set up a scaling and rotation matrix.
	Imath::Matrix44<float> Matrix(InSourceMatrix[0], InSourceMatrix[1], InSourceMatrix[2],  0.0,
		                          InSourceMatrix[4], InSourceMatrix[5], InSourceMatrix[6],  0.0,
		                          InSourceMatrix[8], InSourceMatrix[9], InSourceMatrix[10], 0.0,
		                          0.0,               0.0,               0.0,                1.0);

	// Remove any scaling from the matrix and get the scale vector that was initially present.
	Imath::Vec3<float> Scale;
	Imath::Vec3<float> Shear;
	bool bExtracted = Imath::extractAndRemoveScalingAndShear<float>(Matrix, Scale, Shear, false);

	if (!bExtracted)
	{
		// TODO: Append a message to the build summary.
		FString Msg = FString::Printf(TEXT("WARNING: Actor %ls (%ls) has some zero scaling"), *ElementName, *ElementLabel);

		return FTransform::Identity;
	}

	FVector TransformScale3D;

	// Initialize a rotation quaternion with the rotation matrix.
	Imath::Quat<float> Quaternion = Imath::extractQuat<float>(Matrix);

	switch (WorldCoordinateSystemType)
	{
		case ECoordinateSystemType::LeftHandedYup:
		{
			// Set the Datasmith actor scale.
			TransformScale3D = FVector(Scale.x, Scale.z, Scale.y);

			// Convert the left-handed Y-up coordinate rotation into an Unreal left-handed Z-up coordinate rotation.
			// This is done by doing a 90 degree rotation about the X axis.
			float Y = Quaternion.v.y;
			float Z = Quaternion.v.z;
			Quaternion.v.y = -Z;
			Quaternion.v.z =  Y;
			Quaternion.normalize();

			break;
		}

		case ECoordinateSystemType::LeftHandedZup:
		{
			// Set the Datasmith actor scale.
			TransformScale3D = FVector(Scale.x, Scale.y, Scale.z);

			break;
		}

		case ECoordinateSystemType::RightHandedZup:
		{
			// Set the Datasmith actor scale.
			TransformScale3D = FVector(Scale.x, Scale.y, Scale.z);

			// Convert the right-handed Z-up coordinate rotation into an Unreal left-handed Z-up coordinate rotation.
			// This is done by inverting the X and Z components of the quaternion to mirror the quaternion on the XZ-plane.
			Quaternion.v.x = -Quaternion.v.x;
			Quaternion.v.z = -Quaternion.v.z;
			Quaternion.normalize();

			break;
		}
	}

	// Make sure Unreal will be able to handle the rotation quaternion.
	float              Angle = Quaternion.angle();
	Imath::Vec3<float> Axis  = Quaternion.axis();
	FQuat TransformRotation = FQuat(FVector(Axis.x, Axis.y, Axis.z), Angle);

	// Scale and convert the source translation into a Datasmith actor translation.
	FVector TransformTranslation = ConvertPosition(InSourceMatrix[12], InSourceMatrix[13], InSourceMatrix[14]);

	return FTransform(TransformRotation, TransformTranslation, TransformScale3D);
}

void FDatasmithFacadeActor::AddChild(
	TSharedPtr<FDatasmithFacadeActor> InChildActorPtr
)
{
	ChildActorArray.Add(InChildActorPtr);
}

TSharedPtr<FDatasmithFacadeElement> FDatasmithFacadeActor::Optimize(
	TSharedPtr<FDatasmithFacadeElement> InElementPtr,
	bool                                bInNoSingleChild
)
{
	for (int32 ChildNo = ChildActorArray.Num() - 1; ChildNo >= 0; ChildNo--)
	{
		// Optimize the Datasmith child actor.
		TSharedPtr<FDatasmithFacadeElement> ChildActorPtr = ChildActorArray[ChildNo]->Optimize(ChildActorArray[ChildNo], bInNoSingleChild);

		if (ChildActorPtr.IsValid())
		{
			ChildActorArray[ChildNo] = StaticCastSharedPtr<FDatasmithFacadeActor>(ChildActorPtr);
		}
		else
		{
			ChildActorArray.RemoveAt(ChildNo);
		}
	}

	if (bOptimizeActor)
	{
		if (ChildActorArray.Num() == 0)
		{
			// This Datasmith actor can be removed by optimization.
			return TSharedPtr<FDatasmithFacadeElement>();
		}

		if (bInNoSingleChild && ChildActorArray.Num() == 1)
		{
			// This intermediate Datasmith actor can be removed while keeping its single child actor.
			TSharedPtr<FDatasmithFacadeActor> SingleChildActorPtr = ChildActorArray[0];

			// Make sure the single child actor will not become a dangling component in the actor hierarchy.
			if (!bIsComponent && SingleChildActorPtr->bIsComponent)
			{
				SingleChildActorPtr->bIsComponent = false;
			}

			return SingleChildActorPtr;
		}
	}

	// Prevent the Datasmith actor from being removed by optimization.
	return InElementPtr;
}

void FDatasmithFacadeActor::BuildScene(
	TSharedRef<IDatasmithScene> IOSceneRef
)
{
	// Create and initialize a Datasmith actor hierarchy.
	TSharedPtr<IDatasmithActorElement> ActorHierarchyPtr = CreateActorHierarchy(IOSceneRef);

	// Add the Datasmith actor hierarchy to the Datasmith scene.
	IOSceneRef->AddActor(ActorHierarchyPtr);
}

TSharedPtr<IDatasmithActorElement> FDatasmithFacadeActor::CreateActorHierarchy(
	TSharedRef<IDatasmithScene> IOSceneRef
) const
{
	// Create a Datasmith actor element.
	TSharedPtr<IDatasmithActorElement> ActorPtr = FDatasmithSceneFactory::CreateActor(*ElementName);

	// Set the Datasmith actor properties.
	SetActorProperties(IOSceneRef, ActorPtr);

	// Add the hierarchy of children to the Datasmith actor.
	AddActorChildren(IOSceneRef, ActorPtr);

	return ActorPtr;
}

void FDatasmithFacadeActor::SetActorProperties(
	TSharedRef<IDatasmithScene>        IOSceneRef,
	TSharedPtr<IDatasmithActorElement> IOActorPtr
) const
{
	// Set the actor label used in the Unreal UI.
	IOActorPtr->SetLabel(*ElementLabel);

	// Set the Datasmith actor world transform.
	IOActorPtr->SetScale(WorldTransform.GetScale3D());
	IOActorPtr->SetRotation(WorldTransform.GetRotation());
	IOActorPtr->SetTranslation(WorldTransform.GetTranslation());

	// Set the Datasmith actor layer name.
	if (!LayerName.IsEmpty())
	{
		IOActorPtr->SetLayer(*LayerName);
	}

	// Add the Datasmith actor tags.
	for (FString const& Tag : TagArray)
	{
		IOActorPtr->AddTag(*Tag);
	}

	if (MetadataPropertyArray.Num() > 0)
	{
		// Create a Datasmith metadata element.
		TSharedPtr<IDatasmithMetaDataElement> MetadataPtr = FDatasmithSceneFactory::CreateMetaData(*(ElementName + TEXT("_DATA")));

		// Set the metadata label used in the Unreal UI.
		MetadataPtr->SetLabel(*ElementLabel);

		// Set the actor associated with the Datasmith metadata.
		MetadataPtr->SetAssociatedElement(IOActorPtr);

		// Add the metadata properties to the Datasmith metadata.
		for (TSharedPtr<IDatasmithKeyValueProperty> MetadataPropertyPtr : MetadataPropertyArray)
		{
			MetadataPtr->AddProperty(MetadataPropertyPtr);
		}

		// Add the Datasmith metadata to the Datasmith scene.
		IOSceneRef->AddMetaData(MetadataPtr);
	}

	// Set whether or not the Datasmith actor is a component when used in a hierarchy.
	IOActorPtr->SetIsAComponent(bIsComponent);
}

void FDatasmithFacadeActor::AddActorChildren(
	TSharedRef<IDatasmithScene>        IOSceneRef,
	TSharedPtr<IDatasmithActorElement> IOActorPtr
) const
{
	// Add the hierarchy of Datasmith actor children.
	for (TSharedPtr<FDatasmithFacadeActor> ChildActorPtr : ChildActorArray)
	{
		// Create and initialize a Datasmith child actor hierarchy.
		TSharedPtr<IDatasmithActorElement> ChildActorHierarchyPtr = ChildActorPtr->CreateActorHierarchy(IOSceneRef);

		if (IOActorPtr.IsValid())
		{
			// Add the child actor hierarchy to the Datasmith actor.
			IOActorPtr->AddChild(ChildActorHierarchyPtr);
		}
		else
		{
			// Add the child actor hierarchy to the Datasmith scene.
			IOSceneRef->AddActor(ChildActorHierarchyPtr);
		}
	}
}

const TArray<TSharedPtr<FDatasmithFacadeActor>> FDatasmithFacadeActor::GetActorChildren() const
{
	return ChildActorArray;
}
