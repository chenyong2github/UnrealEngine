// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CoreTechParser.h"

#ifndef USE_CORETECH_MT_PARSER


#include "CoreTechHelper.h"
#include "DatasmithMeshHelper.h"
#include "DatasmithSceneFactory.h"
#include "DatasmithSceneSource.h"
#include "IDatasmithSceneElements.h"
#include "kernel_io/object_io/asm_io/component_io/part_io/part_io.h"
#include "kernel_io/object_io/asm_io/component_io/assembly_io/assembly_io.h"
#include "Math/UnrealMathUtility.h"
#include "MeshDescription.h"
#include "misc/Paths.h"
#include "Utility/DatasmithMathUtils.h"


using namespace CADLibrary;

FCoreTechParser::FCoreTechParser(TSharedRef<IDatasmithScene> InScene, const FDatasmithSceneSource& InSource, CT_DOUBLE Unit, CT_DOUBLE Tolerance)
	: CTSession(TEXT("FCoreTechParser"), Unit, Tolerance)
	, Source(InSource)
	, DatasmithScene(InScene)
	, DefaultMaterial(nullptr)
{
	SourceFullPath = FPaths::ConvertRelativePathToFull(Source.GetSourceFile());
	MainFileExt = Source.GetSourceFileExtension();
}

CheckedCTError FCoreTechParser::ReadNode(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> Parent)
{
	CT_OBJECT_TYPE Type;
	CT_OBJECT_IO::AskType(NodeId, Type);

	switch (Type)
	{
	case CT_INSTANCE_TYPE:
		return ReadInstance(NodeId, Parent);

	case CT_ASSEMBLY_TYPE:
	case CT_PART_TYPE:
	case CT_COMPONENT_TYPE:
		return ReadComponent(NodeId, Parent);

	case CT_BODY_TYPE:
		return ReadBody(NodeId, Parent);

	//Treat all CT_CURVE_TYPE :
	case CT_CURVE_TYPE:
	case CT_C_NURBS_TYPE:
	case CT_CONICAL_TYPE:
	case CT_ELLIPSE_TYPE:
	case CT_CIRCLE_TYPE:
	case CT_PARABOLA_TYPE:
	case CT_HYPERBOLA_TYPE:
	case CT_LINE_TYPE:
	case CT_C_COMPO_TYPE:
	case CT_POLYLINE_TYPE:
	case CT_EQUATION_CURVE_TYPE:
	case CT_CURVE_ON_SURFACE_TYPE:
	case CT_INTERSECTION_CURVE_TYPE:
	default:
		break;
	}
	return CT_IO_ERROR::IO_ERROR;
}

CheckedCTError FCoreTechParser::ReadComponent(CT_OBJECT_ID ComponentId, TSharedRef<FImportDestination> ComponentNode)
{
	ReadNodeAttributes(ComponentId, ComponentNode, false);
	ComponentNode->SetNodeParameterFromAttribute();
	CreateActor(ComponentNode);
	ComponentNode->AddMetaData(DatasmithScene, Source);

	CT_LIST_IO Children;
	CheckedCTError error = CT_COMPONENT_IO::AskChildren(ComponentId, Children);

	// Iterate over the instances and call some function on each
	Children.IteratorInitialize();
	CT_OBJECT_ID CurrentObjectId;
	int index = 0;
	while ((CurrentObjectId = Children.IteratorIter()) != 0)
	{
		ReadNode(CurrentObjectId, ComponentNode);
	}

	LinkActor(ComponentNode);

	return CT_IO_ERROR::IO_OK;
}

CheckedCTError FCoreTechParser::ReadObjectTransform(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> CurrentNode)
{
	double CTMatrix[16];
	CT_INSTANCE_IO::AskTransformation(NodeId, CTMatrix);

	FMatrix Matrix;
	float* MatrixFloats = (float*)Matrix.M;

	for (int k = 0; k < 16; k++) MatrixFloats[k] = CTMatrix[k];

	FTransform LocalTransform(Matrix);
	FTransform LocalUETransform = FDatasmithUtils::ConvertTransform(FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded, LocalTransform);
	FQuat Quat;
	FDatasmithTransformUtils::GetRotation(LocalUETransform, Quat);
	
	CurrentNode->SetTranslation(LocalUETransform.GetTranslation() * ImportParams.ScaleFactor);
	CurrentNode->SetScale(LocalUETransform.GetScale3D());
	CurrentNode->SetRotation(Quat);

	return CT_IO_ERROR::IO_OK;
}

CheckedCTError FCoreTechParser::ReadInstance(CT_OBJECT_ID InstanceNodeId, TSharedRef<FImportDestination> Parent)
{
	TSharedRef<FImportDestination> InstanceNode = MakeShared<FImportDestination>(Parent);

	// Ask the transformation of the instance
	FMatrix localMatrix;

	ReadObjectTransform(InstanceNodeId, InstanceNode);
	ReadNodeAttributes(InstanceNodeId, InstanceNode, true);

	// Ask the reference
	CT_OBJECT_ID ReferenceNodeId;
	CheckedCTError Result = CT_INSTANCE_IO::AskChild(InstanceNodeId, ReferenceNodeId);
	if (!Result)
		return Result;

	CT_OBJECT_TYPE type;
	CT_OBJECT_IO::AskType(ReferenceNodeId, type);
	if (type == CT_UNLOADED_PART_TYPE || type == CT_UNLOADED_COMPONENT_TYPE || type == CT_UNLOADED_ASSEMBLY_TYPE)
	{
		CT_STR componentFile, fileType;
		CT_COMPONENT_IO::AskExternalDefinition(ReferenceNodeId, componentFile, fileType);

		//FString* ConfigurationName = InstanceNode->GetAttributMap().Find(TEXT("Configuration Name"));
		//if (ConfigurationName != nullptr) {
		//	//TODO
		//}

		// TODO
		//if (unloadedComponents.find(fileName2) == unloadedComponents.end())
		//{
		//	unloadedComponents[fileName2] = new UnloadedComponent(fileName2);
		//}
		//unloadedComponents[fileName2]->addInstance(dest.getContainer(), nodeAttributeSet, instanceMatrix*dest.getMatrix());

		return CT_IO_ERROR::IO_OK;
	}

	Result = ReadNode(ReferenceNodeId, InstanceNode);

	return CT_IO_ERROR::IO_ERROR;
}

CheckedCTError FCoreTechParser::ReadBody(CT_OBJECT_ID BodyId, TSharedRef<FImportDestination> Parent)
{
	TSharedRef<FImportDestination> BodyNode = MakeShared<FImportDestination>(Parent, true);
	ReadNodeAttributes(BodyId, BodyNode, false);
	BodyNode->SetNodeParameterFromAttribute(true);

	TSharedPtr< IDatasmithMeshElement > MeshElement = FindOrAddMeshElement(BodyNode, BodyId);
	if (!MeshElement.IsValid())
	{
		return CT_IO_ERROR::IO_ERROR;
	}

	TSharedPtr< IDatasmithMeshActorElement > ActorElement = FDatasmithSceneFactory::CreateMeshActor(*BodyNode->GetUEUUID());
	if (!ActorElement.IsValid())
	{
		return CT_IO_ERROR::IO_ERROR;
	}
	BodyNode->SetActor(ActorElement);

	ActorElement->SetLabel(*BodyNode->GetLabel());
	ActorElement->SetStaticMeshPathName(MeshElement->GetName());

	LinkActor(BodyNode);

	return CT_IO_ERROR::IO_OK;
}

void FCoreTechParser::GetAttributeValue(CT_ATTRIB_TYPE AttributType, int IthField, FString& Value)
{
	CT_STR               FieldName;
	CT_ATTRIB_FIELD_TYPE FieldType;

	Value = "";

	if (CT_ATTRIB_DEFINITION_IO::AskFieldDefinition(AttributType, IthField, FieldType, FieldName) != IO_OK) return;

	switch (FieldType) {
	case CT_ATTRIB_FIELD_UNKNOWN:
	{
		break;
	}
	case CT_ATTRIB_FIELD_INTEGER:
	{
		int IValue;
		if (CT_CURRENT_ATTRIB_IO::AskIntField(IthField, IValue) != IO_OK) break;
		Value = FString::FromInt(IValue);
		break;
	}
	case CT_ATTRIB_FIELD_DOUBLE:
	{
		double DValue;
		if (CT_CURRENT_ATTRIB_IO::AskDblField(IthField, DValue) != IO_OK) break;
		Value = FString::Printf(TEXT("%lf"), DValue);
		break;
	}
	case CT_ATTRIB_FIELD_STRING:
	{
		CT_STR StrValue;
		if (CT_CURRENT_ATTRIB_IO::AskStrField(IthField, StrValue) != IO_OK) break;
		Value = StrValue.toUnicode();
		break;
	}
	case CT_ATTRIB_FIELD_POINTER:
	{
		break;
	}
	}
}

CheckedCTError FCoreTechParser::ReadNodeAttributes(CT_OBJECT_ID NodeId, TSharedRef<FImportDestination> CurrentNode, bool bIsInstance)
{
	TMap<FString, FString>& NodeAttributeSet = CurrentNode->GetAttributMap(bIsInstance);

	if (!bIsInstance && CT_COMPONENT_IO::IsA(NodeId, CT_COMPONENT_TYPE))
	{
		CT_STR FileName, FileType;
		CT_COMPONENT_IO::AskExternalDefinition(NodeId, FileName, FileType);
		if (!FileName.IsEmpty())
		{
			CurrentNode->SetExternalDefinition(FileName.toUnicode());
		}
	}

	CT_UINT32 NbAttributes;
	CT_OBJECT_IO::AskNbAttributes(NodeId, CT_ATTRIB_ALL, NbAttributes);
	CT_UINT32 ith_attrib = 0;

	CT_SHOW_ATTRIBUTE IsShow = CT_UNKNOWN;
	if (CT_OBJECT_IO::AskShowAttribute(NodeId, IsShow) == IO_OK)
	{
		switch (IsShow)
		{
		case CT_SHOW:
			NodeAttributeSet.Add(TEXT("ShowAttribute"), TEXT("show"));
			break;
		case CT_NOSHOW:
			NodeAttributeSet.Add(TEXT("ShowAttribute"), TEXT("noShow"));
			break;
		case CT_UNKNOWN:
			NodeAttributeSet.Add(TEXT("ShowAttribute"), TEXT("unknown"));
			break;
		}
	}

	while (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_ALL, ith_attrib++) == IO_OK)
	{
		// Get the current attribute type
		CT_ATTRIB_TYPE       AttributeType;
		CT_STR               TypeName;

		CT_STR               FieldName;
		CT_STR               FieldStrValue;
		CT_INT32             FieldIntValue;
		CT_DOUBLE            FieldDoubleValue0, FieldDoubleValue1, FieldDoubleValue2;
		FString              FieldValue;


		if (CT_CURRENT_ATTRIB_IO::AskAttributeType(AttributeType) != IO_OK) continue;;
		switch (AttributeType) {

		case CT_ATTRIB_SPLT:
			//if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SPLT_VALUE, field_strValue) != IO_OK) break;
			//nodeAttributeSet[TEXT("SPLT")] = field_strValue.toUnicode();
			break;

		case CT_ATTRIB_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("CTName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("Name"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_FILENAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_FILENAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("FileName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_UUID:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_UUID_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("UUID"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_INPUT_FORMAT_AND_EMETTOR:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INPUT_FORMAT_AND_EMETTOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("Input_Format_and_Emitter"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_CONFIGURATION_NAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_NAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("ConfigurationName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_LAYERID:
			GetAttributeValue(AttributeType, ITH_LAYERID_VALUE, FieldValue);
			NodeAttributeSet.Add(TEXT("LayerId"), FieldValue);
			GetAttributeValue(AttributeType, ITH_LAYERID_NAME, FieldValue);
			NodeAttributeSet.Add(TEXT("LayerName"), FieldValue);
			GetAttributeValue(AttributeType, ITH_LAYERID_FLAG, FieldValue);
			NodeAttributeSet.Add(TEXT("LayerFlag"), FieldValue);
			break;

		case CT_ATTRIB_COLORID:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_COLORID_VALUE, FieldIntValue) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("ColorId"), FString::FromInt(FieldIntValue));
			{
				CT_COLOR CtColor;
				if (CT_MATERIAL_IO::AskIndexedColor(FieldIntValue, CtColor) != IO_OK) break;

				unsigned char Alpha = 255;
				if (CT_OBJECT_IO::SearchAttribute(NodeId, CT_ATTRIB_TRANSPARENCY) == IO_OK)
				{
					if (CT_CURRENT_ATTRIB_IO::AskDblField(0, FieldDoubleValue0) == IO_OK)
					{
						Alpha = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
					}
				}
				FString colorHexa = FString::Printf(TEXT("%02x%02x%02x%02x"), CtColor[0], CtColor[1], CtColor[2], Alpha);
				NodeAttributeSet.Add(TEXT("ColorValue"), colorHexa);
			}
			break;

		case CT_ATTRIB_MATERIALID:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_MATERIALID_VALUE, FieldIntValue) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("MaterialId"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_TRANSPARENCY:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_TRANSPARENCY_VALUE, FieldDoubleValue0) != IO_OK) break;
			FieldIntValue = FMath::Max((1. - FieldDoubleValue0), FieldDoubleValue0) * 255.;
			NodeAttributeSet.Add(TEXT("Transparency"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_COMMENT:
			//ITH_COMMENT_POSX, ITH_COMMENT_POSY, ITH_COMMENT_POSZ, ITH_COMMENT_TEXT
			break;

		case CT_ATTRIB_REFCOUNT:
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_REFCOUNT_VALUE, FieldIntValue) != IO_OK) break;
			//NodeAttributeSet.Add(TEXT("RefCount"), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_TESS_PARAMS:
			//ITH_TESS_PARAMS_MAXSAG, ITH_TESS_PARAMS_MAXANGLE, ITH_TESS_PARAMS_MAXLENGTH
			break;

		case CT_ATTRIB_COMPARE_RESULT:
			//ITH_COMPARE_TYPE, ITH_COMPARE_RESULT_MAXDIST, ITH_COMPARE_RESULT_MINDIST, ITH_COMPARE_THICKNESS, ITH_COMPARE_BACKLASH, ITH_COMPARE_CLEARANCE, ITH_COMPARE_MAXTHICKNESS, ITH_COMPARE_RESULT_NEAREST, ITH_COMPARE_RESULT_ORIGIN1_X, ITH_COMPARE_RESULT_ORIGIN1_Y, ITH_COMPARE_RESULT_ORIGIN1_Z, ITH_COMPARE_RESULT_DIRECTION1_X, ITH_COMPARE_RESULT_DIRECTION1_Y, ITH_COMPARE_RESULT_DIRECTION1_Z, ITH_COMPARE_RESULT_RADIUS1_1, ITH_COMPARE_RESULT_RADIUS2_1, ITH_COMPARE_RESULT_HALFHANGLE1, ITH_COMPARE_RESULT_ORIGIN2_X, ITH_COMPARE_RESULT_ORIGIN2_Y, ITH_COMPARE_RESULT_ORIGIN2_Z, ITH_COMPARE_RESULT_DIRECTION2_X, ITH_COMPARE_RESULT_DIRECTION2_Y, ITH_COMPARE_RESULT_DIRECTION2_Z, ITH_COMPARE_RESULT_RADIUS1_2, ITH_COMPARE_RESULT_RADIUS2_2, ITH_COMPARE_RESULT_HALFHANGLE2, ITH_COMPARE_RESULT_COLOR1, ITH_COMPARE_RESULT_COLOR2, ITH_COMPARE_DIST_VECTOR1_X, ITH_COMPARE_DIST_VECTOR1_Y, ITH_COMPARE_DIST_VECTOR1_Z, ITH_COMPARE_DIST_VECTOR2_X, ITH_COMPARE_DIST_VECTOR2_Y, ITH_COMPARE_DIST_VECTOR2_Z
			break;
		case CT_ATTRIB_DENSITY:
			//ITH_VOLUME_DENSITY_VALUE
			break;

		case CT_ATTRIB_MASS_PROPERTIES:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_AREA, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("Area"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_VOLUME, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("Volume"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_MASS, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("Mass"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_MASS_PROPERTIES_LENGTH, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("Length"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			//ITH_MASS_PROPERTIES_COGX, ITH_MASS_PROPERTIES_COGY, ITH_MASS_PROPERTIES_COGZ
			//ITH_MASS_PROPERTIES_M1, ITH_MASS_PROPERTIES_M2, ITH_MASS_PROPERTIES_M3
			//ITH_MASS_PROPERTIES_IXXG,ITH_MASS_PROPERTIES_IYYG, ITH_MASS_PROPERTIES_IZZG, ITH_MASS_PROPERTIES_IXYG, ITH_MASS_PROPERTIES_IYZG, ITH_MASS_PROPERTIES_IZXG
			//ITH_MASS_PROPERTIES_AXIS1X, ITH_MASS_PROPERTIES_AXIS1Y, ITH_MASS_PROPERTIES_AXIS1Z, ITH_MASS_PROPERTIES_AXIS2X, ITH_MASS_PROPERTIES_AXIS2Y, ITH_MASS_PROPERTIES_AXIS2Z, ITH_MASS_PROPERTIES_AXIS3X, ITH_MASS_PROPERTIES_AXIS3Y, ITH_MASS_PROPERTIES_AXIS3Z
			//ITH_MASS_PROPERTIES_XMIN, ITH_MASS_PROPERTIES_YMIN, ITH_MASS_PROPERTIES_ZMIN, ITH_MASS_PROPERTIES_XMAX, ITH_MASS_PROPERTIES_YMAX, ITH_MASS_PROPERTIES_ZMAX
			break;

		case CT_ATTRIB_THICKNESS:
			//ITH_THICKNESS_VALUE
			break;

		case CT_ATTRIB_INTEGER_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_METADATA_VALUE, FieldIntValue) != IO_OK) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_METADATA_VALUE, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_METADATA:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_METADATA_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_UNITS:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_MASS, FieldDoubleValue0) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_LENGTH, FieldDoubleValue1) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ORIGINAL_UNITS_DURATION, FieldDoubleValue2) != IO_OK) break;
			NodeAttributeSet.Add(TEXT("OriginalUnitsMass"), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			NodeAttributeSet.Add(TEXT("OriginalUnitsLength"), FString::Printf(TEXT("%lf"), FieldDoubleValue1));
			NodeAttributeSet.Add(TEXT("OriginalUnitsDuration"), FString::Printf(TEXT("%lf"), FieldDoubleValue2));
			break;

		case CT_ATTRIB_ORIGINAL_TOLERANCE:
			//ITH_ORIGINAL_TOLERANCE_LENGTH, ITH_ORIGINAL_TOLERANCE_MAXCOORD, ITH_ORIGINAL_TOLERANCE_ANGLE
			break;
		case CT_ATTRIB_IGES_PARAMETERS:
			//ITH_IGES_DELIMITOR, ITH_IGES_ENDDELIMITOR, ITH_IGES_EMETTORID, ITH_IGES_CREATION_FILENAME, ITH_IGES_EMETTOR_NAME, ITH_IGES_EMETTOR_VERSION, ITH_IGES_NBOFBITS, ITH_IGES_MAXPOW_SINGLE_PREC, ITH_IGES_NB_DIGITS_SINGLE_PRE, ITH_IGES_MAXPOW_DOUBLE_PREC, ITH_IGES_NB_DIGITS_DOUBLE_PRE, ITH_IGES_RECEPTOR_ID, ITH_IGES_SCALE, ITH_IGES_UNIT, ITH_IGES_UNIT_NAME, ITH_IGES_MAXLINEGRADATIONS, ITH_IGES_MAXLINEWIDTH, ITH_IGES_CREATIONFILEDATE, ITH_IGES_MINIRESOLUTION, ITH_IGES_MAXCOORD, ITH_IGES_AUTHORNAME, ITH_IGES_ORGANIZATION, ITH_IGES_IGESVERSION, ITH_IGES_DRAFTINGSTANDARD, ITH_IGES_MODELDATE, ITH_IGES_APPLICATIONPROTOCOL
			break;
		case CT_ATTRIB_READ_V4_MARKER:
			//ITH_READ_V4_MARKER_VALUE
			break;

		case CT_ATTRIB_PRODUCT:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_REVISION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("ProductRevision"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DEFINITION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("ProductDefinition"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_NOMENCLATURE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("ProductNomenclature"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_SOURCE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("ProductSource"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_PRODUCT_DESCRIPTION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("ProductDescription"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_SIMPLIFY:
			//ITH_SIMPLIFY_CURRENT_ID, ITH_SIMPLIFY_CONNECTED_ID
			break;
		case CT_ATTRIB_MIDFACE:
			//ITH_MIDFACE_TYPE, ITH_MIDFACE_THICKNESS1, ITH_MIDFACE_THICKNESS2
			break;
		case CT_ATTRIB_DEBUG_STRING:
			//ITH_DEBUG_STRING_VALUE
			break;
		case CT_ATTRIB_DEFEATURING:
			break;
		case CT_ATTRIB_BREPLINKID:
			//ITH_BREPLINKID_BRANCHID, ITH_BREPLINKID_FACEID
			break;
		case CT_ATTRIB_MARKUPS_REF:
			//ITH_MARKUPS_REF
			break;
		case CT_ATTRIB_COLLISION:
			//ITH_COLLISION_ID, ITH_COLLISION_MATID
			break;
		case CT_ATTRIB_EXTERNAL_ID:
			//ITH_EXTERNAL_ID_VALUE
			break;
		case CT_ATTRIB_MODIFIER:
			//ITH_MODIFIER_TYPE, ITH_MODIFIER_INTVALUE, ITH_MODIFIER_DBLVALUE, ITH_MODIFIER_STRVALUE
			break;
		case CT_ATTRIB_ORIGINAL_SURF_OLD:
			break;
		case CT_ATTRIB_RESULT_BREPLINKID:
			break;
		case CT_ATTRIB_AREA:
			//ITH_AREA_VALUE
			break;
		case CT_ATTRIB_ACIS_SG_PIDNAME:
			//ITH_ACIS_SG_PIDNAME_BASE_NAME, ITH_ACIS_SG_PIDNAME_TIME_VAL, ITH_ACIS_SG_PIDNAME_INDEX, ITH_ACIS_SG_PIDNAME_COPY_NUM
			break;
		case CT_ATTRIB_CURVE_ORIGINAL_BOUNDARY_PARAMS:
			//ITH_CURVE_ORIGINAL_BOUNDARY_PARAMS_START, ITH_CURVE_ORIGINAL_BOUNDARY_PARAMS_END, ITH_CURVE_ORIGINAL_BOUNDARY_PARAMS_SCALE
			break;

		case CT_ATTRIB_INTEGER_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_PARAMETER_VALUE, FieldIntValue) != IO_OK) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_PARAMETER_VALUE, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_PARAMETER:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_PARAMETER_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(FieldName.toUnicode(), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_PARAMETER_ARRAY:
			//ITH_PARAMETER_ARRAY_NAME
			//ITH_PARAMETER_ARRAY_NUMBER
			//ITH_PARAMETER_ARRAY_VALUES
			break;

		case CT_ATTRIB_SAVE_OPTION:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionAuthor"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_ORGANIZATION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionOrganization"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_FILE_DESCRIPTION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionFileDescription"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_AUTHORISATION, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionAuthorisation"), FieldStrValue.toUnicode());
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_SAVE_OPTION_PREPROCESSOR, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			//NodeAttributeSet.Add(TEXT("SaveOptionPreprocessor"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ORIGINAL_ID:
			GetAttributeValue(AttributeType, ITH_ORIGINAL_ID_VALUE, FieldValue);
			NodeAttributeSet.Add(TEXT("OriginalId"), FieldValue);
			break;

		case CT_ATTRIB_ORIGINAL_ID_STRING:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_ORIGINAL_ID_VALUE_STRING, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("OriginalIdStr"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_COLOR_RGB_DOUBLE:
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_R_DOUBLE, FieldDoubleValue0) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_G_DOUBLE, FieldDoubleValue1) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_ATTRIB_COLOR_B_DOUBLE, FieldDoubleValue2) != IO_OK) break;
			FieldValue = FString::Printf(TEXT("%lf"), FieldDoubleValue0) + TEXT(", ") + FString::Printf(TEXT("%lf"), FieldDoubleValue1) + TEXT(", ") + FString::Printf(TEXT("%lf"), FieldDoubleValue2);
			//NodeAttributeSet.Add(TEXT("ColorRGBDouble"), FieldValue);
			break;

		case CT_ATTRIB_REVERSE_COLORID:
			break;
		case CT_ATTRIB_INITIAL_FILTER:
			break;
		case CT_ATTRIB_ORIGINAL_SURF:
			break;
		case CT_ATTRIB_LINKMANAGER_BRANCH_FACE:
			//ITH_LINKMANAGER_BRANCH_FACE_PART_ID, ITH_LINKMANAGER_BRANCH_FACE_SOLID_ID, ITH_LINKMANAGER_BRANCH_FACE_BRANCH_ID, ITH_LINKMANAGER_BRANCH_FACE_FACE_ID
			break;
		case CT_ATTRIB_LINKMANAGER_PMI:
			//ITH_LINKMANAGER_PMI_ID, ITH_LINKMANAGER_PMI_SENSE, ITH_LINKMANAGER_PMI_INSTANCE
			break;
		case CT_ATTRIB_NULL:
			//ITH_NULL_TITLE
			break;
		case CT_ATTRIB_MEASURE_VALIDATION_ATTRIBUTE:
			//ITH_MEASURE_VALIDATION_NAME
			//ITH_MEASURE_VALIDATION_UNIT
			//ITH_MEASURE_VALIDATION_VALUE
			break;

		case CT_ATTRIB_INTEGER_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_INTEGER_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskIntField(ITH_INTEGER_VALIDATION_VALUE, FieldIntValue) != IO_OK) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FString::FromInt(FieldIntValue));
			break;

		case CT_ATTRIB_DOUBLE_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_DOUBLE_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskDblField(ITH_DOUBLE_VALIDATION_VALUE, FieldDoubleValue0) != IO_OK) break;
			NodeAttributeSet.Add(FieldName.toUnicode(), FString::Printf(TEXT("%lf"), FieldDoubleValue0));
			break;

		case CT_ATTRIB_STRING_VALIDATION_ATTRIBUTE:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_NAME, FieldName) != IO_OK) break;
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_STRING_VALIDATION_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(FieldName.toUnicode(), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_BOUNDING_BOX:
			//ITH_BOUNDING_BOX_XMIN, ITH_BOUNDING_BOX_YMIN, ITH_BOUNDING_BOX_ZMIN, ITH_BOUNDING_BOX_XMAX, ITH_BOUNDING_BOX_YMAX, ITH_BOUNDING_BOX_ZMAX
			break;
		case CT_ATTRIB_DATABASE:
			//ITH_DATABASE_NAME, ITH_DATABASE_TABLE
			break;
		case CT_ATTRIB_CURVE_FONT:
			//ITH_ATTRIB_CURVE_FONT_VALUE
			break;
		case CT_ATTRIB_CURVE_WEIGHT:
			//ITH_ATTRIB_CURVE_WEIGHT_VALUE
			break;
		case CT_ATTRIB_COMPARE_TOPO:
			break;
		case CT_ATTRIB_MONIKER_GUID_TABLE:
			//ITH_MONIKER_GUID_TABLE_NAME, ITH_MONIKER_GUID_TABLE_GUID, ITH_MONIKER_GUID_TABLE_INDEX, ITH_MONIKER_GUID_TABLE_APPLICATION_NAME
			break;
		case CT_ATTRIB_MONIKER_DATA:
			//ITH_MONIKER_DATA_NAME, ITH_MONIKER_DATA_TABLE_INDEX, ITH_MONIKER_DATA_ENTITY_ID, ITH_MONIKER_DATA_LABEL, ITH_MONIKER_DATA_BODY_GUID
			break;
		case CT_ATTRIB_MONIKER_BODY_ID:
			//ITH_MONIKER_BODY_ID_NAME, ITH_MONIKER_BODY_ID_ENTITY_ID, ITH_MONIKER_BODY_ID_APPLICATION_NAME
			break;
		case CT_ATTRIB_NO_INSTANCE:
			break;

		case CT_ATTRIB_GROUPNAME:
			if (CT_CURRENT_ATTRIB_IO::AskStrField(ITH_GROUPNAME_VALUE, FieldStrValue) != IO_OK) break;
			if (FieldStrValue.IsEmpty()) { break; }
			NodeAttributeSet.Add(TEXT("GroupName"), FieldStrValue.toUnicode());
			break;

		case CT_ATTRIB_ANALYZE_ID:
			//ITH_ANALYZE_OBJECT_ID
			break;
		case CT_ATTRIB_ANALYZER_DISPLAY_MODE:
			//ITH_ANALYZER_DISPLAY_MODE_VALUE
			break;
		case CT_ATTRIB_ANIMATION_ID:
			//ITH_ANIMATION_ID_VALUE
			break;
		case CT_ATTRIB_PROJECTED_SURFACE_ID:
			//ITH_PROJECTED_SURFACE_ID_VALUE
			break;
		case CT_ATTRIB_ANALYZE_LINK:
			//ITH_ANALYZE_LINK_ID
			break;
		case CT_ATTRIB_TOPO_EVENT_ID:
			//ITH_TOPO_EVENT_ID_VALUE
			break;
		case CT_ATTRIB_ADDITIVE_MANUFACTURING:
			//ITH_AM_MODE, ITH_AM_ROUGHNESS_MIN, ITH_AM_SECTION_DIST, ITH_AM_FUNCTIONAL_TYPE, ITH_AM_LATTICE_TYPE
			break;
		case CT_ATTRIB_MOLDING_RESULT:
			//ITH_MOLDING_MODE, ITH_MOLDING_MEASURE_MIN, ITH_MOLDING_MEASURE_MAX
			break;
		case CT_ATTRIB_AMF_ID:
			//ITH_AMF_ID_VALUE, ITH_AMF_TYPE
			break;
		case CT_ATTRIB_PARAMETER_LINK:
			//ITH_PARAMETER_LINK_ID, ITH_PARAMETER_LINK_PARENT_ID, ITH_PARAMETER_LINK_SENSE
			break;
		default:
			break;
		}
	}
	return CT_IO_ERROR::IO_OK;
}

void FImportDestination::SetNodeParameterFromAttribute(bool bIsBody)
{
	FString* IName = InstanceNodeAttributeSetMap.Find(TEXT("CTName"));
	FString* IOriginalName = InstanceNodeAttributeSetMap.Find(TEXT("Name"));
	FString* IUUID = InstanceNodeAttributeSetMap.Find(TEXT("UUID"));

	FString* RName = ReferenceNodeAttributeSetMap.Find(TEXT("CTName"));
	FString* ROriginalName = ReferenceNodeAttributeSetMap.Find(TEXT("Name"));
	FString* RUUID = ReferenceNodeAttributeSetMap.Find(TEXT("UUID"));

	// Reference Name
	if (ROriginalName)
	{
		ReferenceName = *ROriginalName;
	}
	else if (RName)
	{
		ReferenceName = *RName;
	}
	else
	{
		ReferenceName = "NoName";
	}

	//ReferenceInstanceName = ReferenceName + TEXT("(") + (IOriginalName ? *IOriginalName : ReferenceName) + TEXT(")");
	ReferenceInstanceName = IOriginalName ? *IOriginalName : IName ? *IName : ReferenceName;

	//// UUID
	if (bIsBody)
	{
		BuildMeshActorUUID();
	}

	{
		UEUUID = 0;
		if (Parent.IsValid())
		{
			UEUUID = Parent->GetUUID();
		}

		if (IUUID)
		{
			UEUUID = HashCombine(UEUUID, GetTypeHash(*IUUID));
		}
		else if (IName)
		{
			UEUUID = HashCombine(UEUUID, GetTypeHash(*IName));
		}

		if (RUUID)
		{
			UEUUID = HashCombine(UEUUID, GetTypeHash(*RUUID));
		}
		else if (RName)
		{
			UEUUID = HashCombine(UEUUID, GetTypeHash(*RName));
		}

		UEUUIDStr = FString::Printf(TEXT("0x%08x"), UEUUID);
	}
}

bool FImportDestination::IsValidActor()
{
	if (ActorElement.IsValid())
	{
		if (ActorElement->GetChildrenCount() > 0)
		{
			return true;
		}
		else if (ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
		{
			const TSharedPtr< IDatasmithMeshActorElement >& MeshActorElement = StaticCastSharedPtr< IDatasmithMeshActorElement >(ActorElement);
			return FCString::Strlen(MeshActorElement->GetStaticMeshPathName()) > 0;
		}
	}
	return false;
}

void FImportDestination::AddActorTransform()
{
	if (ActorElement.IsValid())
	{
		ActorElement->SetTranslation(Translation);
		ActorElement->SetScale(Scale);
		ActorElement->SetRotation(Rotation);
	}
}

bool FImportDestination::IsRootNodeOfAFile()
{
	return !ExternalDefinition.IsEmpty();
}

void FImportDestination::AddMetaData(TSharedRef<IDatasmithScene> DatasmithScene, const FDatasmithSceneSource& Source)
{
	// Initialize list of attributes not to pass as meta-data
	auto GetUnwantedAttributes = []() -> TSet<FString>
	{
		TSet<FString> UnwantedAttributes;

		// CoreTech
		UnwantedAttributes.Add(TEXT("CTName"));
		UnwantedAttributes.Add(TEXT("LayerId"));
		UnwantedAttributes.Add(TEXT("LayerName"));
		UnwantedAttributes.Add(TEXT("LayerFlag"));
		UnwantedAttributes.Add(TEXT("OriginalUnitsMass"));
		UnwantedAttributes.Add(TEXT("OriginalUnitsLength"));
		UnwantedAttributes.Add(TEXT("OriginalUnitsDuration"));
		UnwantedAttributes.Add(TEXT("OriginalId"));
		UnwantedAttributes.Add(TEXT("OriginalIdStr"));
		UnwantedAttributes.Add(TEXT("ShowAttribute"));
		UnwantedAttributes.Add(TEXT("Identification"));

		return UnwantedAttributes;
	};

	static const TSet<FString> UnwantedAttributes = GetUnwantedAttributes();

	TSharedRef< IDatasmithMetaDataElement > MetaDataRefElement = FDatasmithSceneFactory::CreateMetaData(ActorElement->GetName());
	MetaDataRefElement->SetAssociatedElement(ActorElement);

	for (auto& Attribute : ReferenceNodeAttributeSetMap)
	{
		if (UnwantedAttributes.Contains(*Attribute.Key))
		{
			continue;
		}

		// If file information are attached to object, make sure to set a workable and full path
		if (Attribute.Key == TEXT("OriginalFileName"))
		{
			FString OFilePath = Attribute.Value;
			if (FPaths::FileExists(OFilePath))
			{
				OFilePath = *FPaths::ConvertRelativePathToFull(OFilePath);
			}
			else
			{
				FString FileDir = FPaths::GetPath(Source.GetSourceFile());
				FString FilePath = FPaths::Combine(FileDir, OFilePath);

				if (FPaths::FileExists(FilePath))
				{
					OFilePath = *FPaths::ConvertRelativePathToFull(OFilePath);
				}
				else // No workable file path to store. Skip
				{
					continue;
				}
			}

			// Beautifying the attributes name
			Attribute.Key = TEXT("FilePath");
			Attribute.Value = OFilePath;
		}

		TSharedRef< IDatasmithKeyValueProperty > KeyValueProperty = FDatasmithSceneFactory::CreateKeyValueProperty(*(FString("Reference_") + Attribute.Key));

		KeyValueProperty->SetValue(*Attribute.Value);
		KeyValueProperty->SetPropertyType(EDatasmithKeyValuePropertyType::String);

		MetaDataRefElement->AddProperty(KeyValueProperty);
	}

	for (auto& Attribute : InstanceNodeAttributeSetMap)
	{
		if (UnwantedAttributes.Contains(*Attribute.Key))
		{
			continue;
		}

		TSharedRef< IDatasmithKeyValueProperty > KeyValueProperty = FDatasmithSceneFactory::CreateKeyValueProperty(*(FString("Instance_") + Attribute.Key));

		KeyValueProperty->SetValue(*Attribute.Value);
		KeyValueProperty->SetPropertyType(EDatasmithKeyValuePropertyType::String);

		MetaDataRefElement->AddProperty(KeyValueProperty);
	}
	DatasmithScene->AddMetaData(MetaDataRefElement);

}

void FCoreTechParser::SetTessellationOptions(const FDatasmithTessellationOptions& Options)
{
	TessellationOptionsHash = Options.GetHash();
	SetImportParameters(Options.ChordTolerance, Options.MaxEdgeLength, Options.NormalTolerance, (EStitchingTechnique) Options.StitchingTechnique);
}

CT_FLAGS FCoreTechParser::SetCoreTechImportOption()
{
	// Set import option
	CT_FLAGS Flags = CT_LOAD_FLAGS_USE_DEFAULT;

	if (MainFileExt == TEXT("jt"))
	{
		Flags |= CT_LOAD_FLAGS_READ_META_DATA;
	}

	if (MainFileExt == TEXT("catpart") || MainFileExt == TEXT("catproduct") || MainFileExt == TEXT("cgr"))
	{
		Flags |= CT_LOAD_FLAGS_V5_READ_GEOM_SET;
	}

	// All the BRep topology is not available in IGES import
	// Ask Kernel IO to complete or create missing topology
	if (MainFileExt == TEXT("igs") || MainFileExt == TEXT("iges"))
	{
		Flags |= CT_LOAD_FLAG_SEARCH_NEW_TOPOLOGY | CT_LOAD_FLAG_COMPLETE_TOPOLOGY;
	}


	Flags |= CT_LOAD_FLAGS_V5_READ_GEOM_SET;
	return Flags;
}

void FImportDestination::BuildMeshActorUUID()
{
	uint32 MeshActorHash = GetTypeHash(GetReferenceName());
	TSharedPtr<FImportDestination> ParentActor = GetParent();
	do {

		FString ExternalDef = ParentActor->GetExternalDefinition();
		if (!ExternalDef.IsEmpty())
		{
			MeshActorHash = HashCombine(MeshActorHash, GetTypeHash(ExternalDef));
			break;
		}

		FString RefInstanceName = ParentActor->GetLabel();
		MeshActorHash = HashCombine(MeshActorHash, GetTypeHash(RefInstanceName));

		ParentActor = ParentActor->GetParent();
	} while (ParentActor);

	MeshUEUUID = MeshActorHash;
	UEUUIDStr = FString::Printf(TEXT("0x%08x"), MeshActorHash);
}

TSharedPtr< IDatasmithUEPbrMaterialElement > FCoreTechParser::GetDefaultMaterial()
{
	if (!DefaultMaterial.IsValid())
	{
		DefaultMaterial = CreateDefaultUEPbrMaterial();
		DatasmithScene->AddMaterial(DefaultMaterial);
	}

	return DefaultMaterial;
}

TSharedPtr< IDatasmithMaterialIDElement > FCoreTechParser::FindOrAddMaterial(uint32 MaterialID)
{
	TSharedPtr< IDatasmithUEPbrMaterialElement > MaterialElement;

	TSharedPtr< IDatasmithUEPbrMaterialElement >* MaterialPtr = MaterialMap.Find(MaterialID);
	if (MaterialPtr != nullptr)
	{
		MaterialElement = *MaterialPtr;
	}
	else if(MaterialID > 0)
	{
		if (MaterialID > LAST_CT_MATERIAL_ID)
		{
			MaterialElement = CreateUEPbrMaterialFromColor(MaterialID);
		}
		else
		{
			MaterialElement = CreateUEPbrMaterialFromMaterial(MaterialID, DatasmithScene);
		}

		if (MaterialElement.IsValid())
		{
			DatasmithScene->AddMaterial(MaterialElement);
			MaterialMap.Add(MaterialID, MaterialElement);
		}
	}

	if (!MaterialElement.IsValid())
	{
		MaterialElement = GetDefaultMaterial();
	}

	TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialElement->GetName());

	return MaterialIDElement;
}

void FCoreTechParser::UnloadScene()
{
	CTKIO_UnloadModel();
}

TOptional<FMeshDescription> FCoreTechParser::GetMeshDescription(TSharedRef<IDatasmithMeshElement> OutMeshElement, FMeshParameters& OutMeshParameters)
{
	CT_OBJECT_ID * BodyId = MeshElementToCTBodyMap.Find(OutMeshElement);
	CT_OBJECT_TYPE Type;
	CT_OBJECT_IO::AskType(*BodyId, Type);

	if (!CT_COMPONENT_IO::IsA(*BodyId, CT_BODY_TYPE))
	{
		return TOptional< FMeshDescription >();
	}

	// Ref. visitBRep
	CheckedCTError Result;

	FMeshDescription MeshDescription;
	DatasmithMeshHelper::PrepareAttributeForStaticMesh(MeshDescription);

	size_t VertexCount = 0;
	size_t IndexCount = 0;

	TArray<CT_OBJECT_ID> BodySet;
	BodySet.Add(*BodyId);

	bool bTessellated = CADLibrary::ConvertCTBodySetToMeshDescription(ImportParams.ScaleFactor, OutMeshParameters, BodySet, MeshDescription);


	//const char*Name = DagNode->name();
	FString Filename = OutMeshElement->GetName();
	Filename += TEXT(".ct");

	FString FilePath = FPaths::Combine(OutputPath, Filename);

	CT_LIST_IO ObjectList;
	ObjectList.PushBack(*BodyId);
	Result = CTKIO_SaveFile(ObjectList, *FilePath, L"Ct");
	if (Result)
	{
		OutMeshElement->SetFile(*FilePath);
	}

	CheckedCTError ConversionResult;
	if (!bTessellated)
	{
		ConversionResult.RaiseOtherError("Error during mesh conversion");
	}

	return MoveTemp(MeshDescription);
}

CheckedCTError FCoreTechParser::Read()
{
	if (!IsSessionValid()) return CT_IO_ERROR::IO_ERROR;;

	// Initialization of CoreTechkernel
	CheckedCTError Result = IO_OK;

	CT_OBJECT_ID MainId = 0;

	CT_FLAGS CTImportOption = SetCoreTechImportOption();

	try
	{
		Result = CTKIO_UnloadModel();
		Result = CTKIO_LoadFile(*SourceFullPath, MainId, CTImportOption);
		if (Result == IO_ERROR_EMPTY_ASSEMBLY)
		{
			Result = CTKIO_UnloadModel();
			if (Result != IO_OK)
			{
				//printCTError(result, __LINE__, __FILE__);
				return CT_IO_ERROR::IO_ERROR;;
			}
			Result = CTKIO_LoadFile(*SourceFullPath, MainId, CTImportOption | CT_LOAD_FLAGS_LOAD_EXTERNAL_REF);
		}
	}
	catch (...)
	{
		Result = IO_ERROR_READING_FILE;
	}

	if (Result != IO_OK && Result != IO_OK_MISSING_LICENSES)
	{
		return CT_IO_ERROR::IO_ERROR;;
	}

	TopoFixes();

	TSharedRef<FImportDestination> RootNode = MakeShared<FImportDestination>();

	Result = ReadNode(MainId, RootNode);

	return Result;


}

TSharedPtr< IDatasmithMeshElement > FCoreTechParser::FindOrAddMeshElement(TSharedRef<FImportDestination> BodyNode, CT_OBJECT_ID BodyId)
{
	uint32 ShellUUID = BodyNode->GetMeshUUID();

	// Look if geometry has not been already processed, return it if found
	TSharedPtr< IDatasmithMeshElement >* MeshElementPtr = BodyUUIDToMeshElementMap.Find(ShellUUID);
	if (MeshElementPtr != nullptr)
	{
		return *MeshElementPtr;
	}

	TSharedPtr< IDatasmithMeshElement > MeshElement = FDatasmithSceneFactory::CreateMesh(*BodyNode->GetUEUUID());
	MeshElement->SetLabel(*BodyNode->GetReferenceName());
	MeshElement->SetLightmapSourceUV(-1);

	// TODO
	// Get bounding box saved by GPure
	//double BoundingBox[8][4];
	//ShellNode.boundingBox(BoundingBox);
	//float BoundingBox[6];
	//FString Buffer = GetStringAttribute(GeomID, TEXT("UE_MESH_BBOX"));
	//if (FString::ToHexBlob(Buffer, (uint8*)BoundingBox, sizeof(BoundingBox)))
	//{
	//	MeshElement->SetDimensions(BoundingBox[3] - BoundingBox[0], BoundingBox[4] - BoundingBox[1], BoundingBox[5] - BoundingBox[2], 0.0f);
	//}

	FCTMaterialPartition MaterialPartition;
	TArray<CT_OBJECT_ID> BodySet;
	BodySet.Add(BodyId);
	GetBodiesMaterials(BodySet, MaterialPartition);

	for (auto MaterialId2Hash : MaterialPartition.GetMaterialIdToHashSet())
	{
		TSharedPtr< IDatasmithMaterialIDElement > PartMaterialIDElement;
		PartMaterialIDElement = FindOrAddMaterial(MaterialId2Hash.Key);

		const TCHAR* MaterialIDElementName = PartMaterialIDElement->GetName();

		TSharedPtr< IDatasmithMaterialIDElement > MaterialIDElement = FDatasmithSceneFactory::CreateMaterialId(MaterialIDElementName);

		MeshElement->SetMaterial(MaterialIDElementName, MaterialId2Hash.Value);
	}

	DatasmithScene->AddMesh(MeshElement);

	BodyUUIDToMeshElementMap.Add(ShellUUID, MeshElement);
	MeshElementToCTBodyMap.Add(MeshElement, BodyId);

	return MeshElement;
}

CheckedCTError FCoreTechParser::CreateMeshActor(TSharedRef<FImportDestination> ActorNode)
{
	TSharedPtr< IDatasmithActorElement > Actor = FDatasmithSceneFactory::CreateActor(*ActorNode->GetUEUUID());
	ActorNode->SetActor(Actor);
	if (Actor.IsValid())
	{
		Actor->SetLabel(*ActorNode->GetLabel());
	}

	return CT_IO_ERROR::IO_OK;
}

void FCoreTechParser::LinkActor(TSharedRef<FImportDestination> ActorNode)
{
	// add the resulting actor to the scene
	if (ActorNode->IsValidActor())
	{
		// Apply local transform to actor element
		ActorNode->AddActorTransform();

		if (ActorNode->GetParent().IsValid())
		{
			ActorNode->GetParent()->GetActor()->AddChild(ActorNode->GetActor());
		}
		else
		{
			DatasmithScene->AddActor(ActorNode->GetActor());
		}
	}
}

CheckedCTError FCoreTechParser::CreateActor(TSharedRef<FImportDestination> ActorNode)
{
	TSharedPtr< IDatasmithActorElement > Actor = FDatasmithSceneFactory::CreateActor(*ActorNode->GetUEUUID());
	ActorNode->SetActor(Actor);
	if (Actor.IsValid())
	{
		Actor->SetLabel(*ActorNode->GetLabel());
	}

	return CT_IO_ERROR::IO_OK;
}

#endif