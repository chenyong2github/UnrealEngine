// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AlembicHairTranslator.h"

#include "HairDescription.h"
#include "GroomImportOptions.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#include "Alembic/AbcGeom/All.h"
#include "Alembic/AbcCoreFactory/IFactory.h"
#include "Alembic/Abc/IArchive.h"
#include "Alembic/Abc/IObject.h"
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAlembicHairImporter, Log, All);

namespace AlembicHairFormat
{
	static const float RootRadius = 0.0001f; // m
	static const float TipRadius = 0.00005f; // m

	static constexpr const float UNIT_TO_CM = 1;
}

namespace AlembicHairTranslatorUtils
{
	bool IsAttributeValid(const FString& AttributeName)
	{
		// Ignore the attributes that aren't prefixed with groom_
		return AttributeName.StartsWith(TEXT("groom_"));
	}

	template <typename AbcParamType, typename AbcArraySampleType, typename AttributeType>
	void SetGroomAttributes(FHairDescription& HairDescription, const Alembic::AbcGeom::ICompoundProperty& Parameters, const std::string& PropName)
	{
		FName AttributeName(ANSI_TO_TCHAR(PropName.c_str()));

		AbcParamType Param(Parameters, PropName);
		AbcArraySampleType ParamValues = Param.getExpandedValue().getVals();
		if (ParamValues->size() == 1)
		{
			AttributeType ParamValue = (*ParamValues)[0];
			SetGroomAttribute(HairDescription, FGroomID(0), AttributeName, ParamValue);
		}
	}

	template <>
	void SetGroomAttributes<Alembic::AbcGeom::IStringGeomParam, Alembic::Abc::StringArraySamplePtr, FName>(FHairDescription& HairDescription, const Alembic::AbcGeom::ICompoundProperty& Parameters, const std::string& PropName)
	{
		FName AttributeName(ANSI_TO_TCHAR(PropName.c_str()));

		Alembic::AbcGeom::IStringGeomParam Param(Parameters, PropName);
		Alembic::Abc::StringArraySamplePtr ParamValues = Param.getExpandedValue().getVals();
		if (ParamValues->size() == 1)
		{
			FName ParamValue = FName((*ParamValues)[0].c_str());
			SetGroomAttribute(HairDescription, FGroomID(0), AttributeName, ParamValue);
		}
	}

	template <typename AbcParamType, typename AbcArraySampleType, typename AttributeType>
	void SetGroomAttributes(FHairDescription& HairDescription, const Alembic::AbcGeom::ICompoundProperty& Parameters, const std::string& PropName, uint8 Extent)
	{
		FName AttributeName(ANSI_TO_TCHAR(PropName.c_str()));

		AbcParamType Param(Parameters, PropName);
		AbcArraySampleType ParamValues = Param.getExpandedValue().getVals();
		if (ParamValues->size() == 1)
		{
			AttributeType ParamValue;
			for (int32 Index = 0; Index < Extent; ++Index)
			{
				ParamValue[Index] = (*ParamValues)[0][Index];
			}
			SetGroomAttribute(HairDescription, FGroomID(0), AttributeName, ParamValue);
		}
	}

	void SetGroomAttributes(FHairDescription& HairDescription, const Alembic::AbcGeom::ICompoundProperty& Parameters)
	{
		for (int Index = 0; Index < Parameters.getNumProperties(); ++Index)
		{
			Alembic::Abc::PropertyHeader PropertyHeader = Parameters.getPropertyHeader(Index);
			std::string PropName = PropertyHeader.getName();

			if (!IsAttributeValid(ANSI_TO_TCHAR(PropName.c_str())))
			{
				continue;
			}

			Alembic::Abc::PropertyType PropType = PropertyHeader.getPropertyType();

			if (PropType != Alembic::Abc::kCompoundProperty)
			{
				Alembic::Abc::DataType DataType = PropertyHeader.getDataType();
				uint8 Extent = DataType.getExtent();

				switch (DataType.getPod())
				{
				case Alembic::Util::kInt16POD:
				{
					SetGroomAttributes<Alembic::AbcGeom::IInt16GeomParam, Alembic::Abc::Int16ArraySamplePtr, int>(HairDescription, Parameters, PropName);
				}
				break;
				case Alembic::Util::kInt32POD:
				{
					SetGroomAttributes<Alembic::AbcGeom::IInt32GeomParam, Alembic::Abc::Int32ArraySamplePtr, int>(HairDescription, Parameters, PropName);
				}
				break;
				case Alembic::Util::kStringPOD:
				{
					SetGroomAttributes<Alembic::AbcGeom::IStringGeomParam, Alembic::Abc::StringArraySamplePtr, FName>(HairDescription, Parameters, PropName);
				}
				break;
				case Alembic::Util::kFloat32POD:
				{
					switch (Extent)
					{
					case 1:
						SetGroomAttributes<Alembic::AbcGeom::IFloatGeomParam, Alembic::Abc::FloatArraySamplePtr, float>(HairDescription, Parameters, PropName);
						break;
					case 2:
						SetGroomAttributes<Alembic::AbcGeom::IV2fGeomParam, Alembic::Abc::V2fArraySamplePtr, FVector2D>(HairDescription, Parameters, PropName, Extent);
						break;
					case 3:
						SetGroomAttributes<Alembic::AbcGeom::IV3fGeomParam, Alembic::Abc::V3fArraySamplePtr, FVector>(HairDescription, Parameters, PropName, Extent);
						break;
					}
				}
				break;
				case Alembic::Util::kFloat64POD:
				{
					switch (Extent)
					{
					case 1:
						SetGroomAttributes<Alembic::AbcGeom::IDoubleGeomParam, Alembic::Abc::DoubleArraySamplePtr, float>(HairDescription, Parameters, PropName);
						break;
					case 2:
						SetGroomAttributes<Alembic::AbcGeom::IV2dGeomParam, Alembic::Abc::V2dArraySamplePtr, FVector2D>(HairDescription, Parameters, PropName, Extent);
						break;
					case 3:
						SetGroomAttributes<Alembic::AbcGeom::IV3dGeomParam, Alembic::Abc::V3dArraySamplePtr, FVector>(HairDescription, Parameters, PropName, Extent);
						break;
					}
				}
				break;
				}
			}
		}
	}

	template <typename AbcParamType, typename AbcArraySampleType, typename AttributeType>
	void ConvertAlembicAttribute(FHairDescription& HairDescription, FStrandID StrandID, int32 StartVertexID, int32 NumVertices, const Alembic::AbcGeom::ICompoundProperty& Parameters, const std::string& PropName)
	{
		FName AttributeName(ANSI_TO_TCHAR(PropName.c_str()));

		// The number of values in Param determines the scope on which to set the hair attribute
		AbcParamType Param(Parameters, PropName);
		AbcArraySampleType ParamValues = Param.getExpandedValue().getVals();
		if (ParamValues->size() == 1)
		{
			AttributeType ParamValue = (*ParamValues)[0];
			SetHairStrandAttribute(HairDescription, StrandID, AttributeName, ParamValue);

		}
		else if (ParamValues->size() == NumVertices)
		{
			TVertexAttributesRef<AttributeType> VertexAttributeRef = HairDescription.VertexAttributes().GetAttributesRef<AttributeType>(AttributeName);
			if (!VertexAttributeRef.IsValid())
			{
				HairDescription.VertexAttributes().RegisterAttribute<AttributeType>(AttributeName);
				VertexAttributeRef = HairDescription.VertexAttributes().GetAttributesRef<AttributeType>(AttributeName);
			}

			for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
			{
				VertexAttributeRef[FVertexID(StartVertexID + VertexIndex)] = (*ParamValues)[VertexIndex];
			}
		}
	}

	template <typename AbcParamType, typename AbcArraySampleType, typename AttributeType>
	void ConvertAlembicAttribute(FHairDescription& HairDescription, FStrandID StrandID, int32 StartVertexID, int32 NumVertices, const Alembic::AbcGeom::ICompoundProperty& Parameters, const std::string& PropName, uint8 Extent)
	{
		FName AttributeName(ANSI_TO_TCHAR(PropName.c_str()));

		// The number of values in Param determines the scope on which to set the hair attribute
		AbcParamType Param(Parameters, PropName);
		AbcArraySampleType ParamValues = Param.getExpandedValue().getVals();
		if (ParamValues->size() == 1)
		{
			AttributeType ParamValue;
			for (int32 Index = 0; Index < Extent; ++Index)
			{
				ParamValue[Index] = (*ParamValues)[0][Index];
			}
			SetHairStrandAttribute(HairDescription, StrandID, AttributeName, ParamValue);
		}
		else if (ParamValues->size() == NumVertices)
		{
			TVertexAttributesRef<AttributeType> VertexAttributeRef = HairDescription.VertexAttributes().GetAttributesRef<AttributeType>(AttributeName);
			if (!VertexAttributeRef.IsValid())
			{
				HairDescription.VertexAttributes().RegisterAttribute<AttributeType>(AttributeName, 1, AttributeType::ZeroVector);
				VertexAttributeRef = HairDescription.VertexAttributes().GetAttributesRef<AttributeType>(AttributeName);
			}

			for (int32 VertexIndex = 0; VertexIndex < NumVertices; ++VertexIndex)
			{
				AttributeType ParamValue;
				for (int32 Index = 0; Index < Extent; ++Index)
				{
					ParamValue[Index] = (*ParamValues)[VertexIndex][Index];
				}

				VertexAttributeRef[FVertexID(StartVertexID + VertexIndex)] = ParamValue;
			}
		}
	}

	/** Convert the given Alembic parameters to hair attributes in the proper scope */
	void ConvertAlembicAttributes(FHairDescription& HairDescription, FStrandID StrandID, int32 StartVertexID, int32 NumVertices, const Alembic::AbcGeom::ICompoundProperty& Parameters)
	{
		for (int Index = 0; Index < Parameters.getNumProperties(); ++Index)
		{
			Alembic::Abc::PropertyHeader PropertyHeader = Parameters.getPropertyHeader(Index);
			std::string PropName = PropertyHeader.getName();

			if (!IsAttributeValid(ANSI_TO_TCHAR(PropName.c_str())))
			{
				continue;
			}

			Alembic::Abc::PropertyType PropType = PropertyHeader.getPropertyType();

			if (PropType != Alembic::Abc::kCompoundProperty)
			{
				Alembic::Abc::DataType DataType = PropertyHeader.getDataType();
				uint8 Extent = DataType.getExtent();

				switch (DataType.getPod())
				{
				case Alembic::Util::kBooleanPOD:
				{
					ConvertAlembicAttribute<Alembic::AbcGeom::IBoolGeomParam, Alembic::Abc::BoolArraySamplePtr, bool>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName);
				}
				break;
				case Alembic::Util::kInt8POD:
				{
					ConvertAlembicAttribute<Alembic::AbcGeom::ICharGeomParam, Alembic::Abc::CharArraySamplePtr, int>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName);
				}
				break;
				case Alembic::Util::kInt16POD:
				{
					ConvertAlembicAttribute<Alembic::AbcGeom::IInt16GeomParam, Alembic::Abc::Int16ArraySamplePtr, int>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName);
				}
				break;
				case Alembic::Util::kInt32POD:
				{
					ConvertAlembicAttribute<Alembic::AbcGeom::IInt32GeomParam, Alembic::Abc::Int32ArraySamplePtr, int>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName);
				}
				break;
				case Alembic::Util::kFloat32POD:
				{
					switch (Extent)
					{
					case 1:
						ConvertAlembicAttribute<Alembic::AbcGeom::IFloatGeomParam, Alembic::Abc::FloatArraySamplePtr, float>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName);
						break;
					case 2:
						ConvertAlembicAttribute<Alembic::AbcGeom::IV2fGeomParam, Alembic::Abc::V2fArraySamplePtr, FVector2D>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName, DataType.getExtent());
						break;
					case 3:
						ConvertAlembicAttribute<Alembic::AbcGeom::IV3fGeomParam, Alembic::Abc::V3fArraySamplePtr, FVector>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName, DataType.getExtent());
						break;
					}
				}
				break;
				case Alembic::Util::kFloat64POD:
				{
					switch (Extent)
					{
					case 1:
						ConvertAlembicAttribute<Alembic::AbcGeom::IDoubleGeomParam, Alembic::Abc::DoubleArraySamplePtr, float>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName);
						break;
					case 2:
						ConvertAlembicAttribute<Alembic::AbcGeom::IV2dGeomParam, Alembic::Abc::V2dArraySamplePtr, FVector2D>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName, DataType.getExtent());
						break;
					case 3:
						ConvertAlembicAttribute<Alembic::AbcGeom::IV3dGeomParam, Alembic::Abc::V3dArraySamplePtr, FVector>(HairDescription, StrandID, StartVertexID, NumVertices, Parameters, PropName, DataType.getExtent());
						break;
					}
				}
				break;
				}
			}
		}
	}
}

FMatrix ConvertAlembicMatrix(const Alembic::Abc::M44d& AbcMatrix)
{
	FMatrix Matrix;
	for (uint32 i = 0; i < 16; ++i)
	{
		Matrix.M[i >> 2][i % 4] = (float)AbcMatrix.getValue()[i];
	}

	return Matrix;
}

static void ParseObject(const Alembic::Abc::IObject& InObject, FHairDescription& HairDescription, const FMatrix& ParentMatrix, const FMatrix& ConversionMatrix, float Scale, bool bCheckGroomAttributes)
{
	// Get MetaData info from current Alembic Object
	const Alembic::Abc::MetaData ObjectMetaData = InObject.getMetaData();
	const uint32 NumChildren = InObject.getNumChildren();

	FMatrix LocalMatrix = ParentMatrix;

	enum class EAttributeFrequency : uint8
	{
		None,
		Groom,
		Hair,
		CV
	};

	bool bHandled = false;
	if (Alembic::AbcGeom::ICurves::matches(ObjectMetaData))
	{
		Alembic::AbcGeom::ICurves Curves = Alembic::AbcGeom::ICurves(InObject, Alembic::Abc::kWrapExisting);
		Alembic::AbcGeom::ICurves::schema_type::Sample Sample = Curves.getSchema().getValue();

		Alembic::Abc::FloatArraySamplePtr Widths = Curves.getSchema().getWidthsParam() ? Curves.getSchema().getWidthsParam().getExpandedValue().getVals() : nullptr;
		Alembic::Abc::P3fArraySamplePtr Positions = Sample.getPositions();
		Alembic::Abc::Int32ArraySamplePtr NumVertices = Sample.getCurvesNumVertices();

		const int32 NumWidths = Widths ? Widths->size() : 0;
		const uint32 NumPoints = Positions ? Positions->size() : 0;
		const uint32 NumCurves = NumVertices->size(); // equivalent to Sample.getNumCurves()

		EAttributeFrequency WidthFrequency = EAttributeFrequency::None;
		{
			if (NumWidths == NumPoints)
			{
				WidthFrequency = EAttributeFrequency::CV;
			}
			else if (NumWidths == NumCurves)
			{
				WidthFrequency = EAttributeFrequency::Hair;
			}
		}

		FMatrix ConvertedMatrix = ParentMatrix * ConversionMatrix;
		uint32 GlobalIndex = 0;
		for (uint32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
		{
			const uint32 CurveNumVertices = (*NumVertices)[CurveIndex];

			FStrandID StrandID = HairDescription.AddStrand();

			SetHairStrandAttribute(HairDescription, StrandID, HairAttribute::Strand::VertexCount, (int) CurveNumVertices);

			int32 StartVertexID = HairDescription.GetNumVertices();
			for (uint32 PointIndex = 0; PointIndex < CurveNumVertices; ++PointIndex, ++GlobalIndex)
			{
				FVertexID VertexID = HairDescription.AddVertex();

				Alembic::Abc::P3fArraySample::value_type Position = (*Positions)[GlobalIndex];

				FVector ConvertedPosition = ConvertedMatrix.TransformPosition(FVector(Position.x, Position.y, Position.z));
				SetHairVertexAttribute(HairDescription, VertexID, HairAttribute::Vertex::Position, ConvertedPosition);

				float Width = 0;
				switch (WidthFrequency)
				{
				case EAttributeFrequency::None:
				{
					const float CoordU = PointIndex / static_cast<float>(CurveNumVertices - 1);
					Width = FMath::Lerp(AlembicHairFormat::RootRadius, AlembicHairFormat::TipRadius, CoordU);
				}
				break;
				case EAttributeFrequency::CV:
					Width = (*Widths)[GlobalIndex];
					break;
				}

				// Per-vertex widths
				if ((WidthFrequency == EAttributeFrequency::CV || WidthFrequency == EAttributeFrequency::None))
				{
					SetHairVertexAttribute(HairDescription, VertexID, HairAttribute::Vertex::Width, Width * Scale);
				}
			}

			Alembic::AbcGeom::ICompoundProperty ArbParams = Curves.getSchema().getArbGeomParams();
			if (ArbParams)
			{
				AlembicHairTranslatorUtils::ConvertAlembicAttributes(HairDescription, StrandID, StartVertexID, CurveNumVertices, ArbParams);
			}

			if (WidthFrequency == EAttributeFrequency::Hair)
			{
				// Fallback if no per-strand or per-vertex groom_width attribute was found
				TStrandAttributesRef<float> StrandWidths = HairDescription.StrandAttributes().GetAttributesRef<float>(HairAttribute::Strand::Width);
				TVertexAttributesRef<float> VertexWidths = HairDescription.VertexAttributes().GetAttributesRef<float>(HairAttribute::Strand::Width);

				if (!StrandWidths.IsValid() && !VertexWidths.IsValid())
				{
					const float Width = (*Widths)[CurveIndex];
					SetHairStrandAttribute(HairDescription, StrandID, HairAttribute::Strand::Width, Width * Scale);
				}
				if (StrandWidths.IsValid() && StrandWidths[StrandID] == 0.f)
				{
					const float Width = (*Widths)[CurveIndex];
					SetHairStrandAttribute(HairDescription, StrandID, HairAttribute::Strand::Width, Width * Scale);
				}
			}
		}
	}
	else if (Alembic::AbcGeom::IXform::matches(ObjectMetaData))
	{
		Alembic::AbcGeom::IXform Xform = Alembic::AbcGeom::IXform(InObject, Alembic::Abc::kWrapExisting);
		Alembic::AbcGeom::XformSample MatrixSample; 
		Xform.getSchema().get(MatrixSample);

		// The groom attributes should only be on the first IXform under the top node, no need to check for them once they are found
		if (bCheckGroomAttributes)
		{
			Alembic::AbcGeom::ICompoundProperty ArbParams = Xform.getSchema().getArbGeomParams();
			if (ArbParams)
			{
				if (ArbParams.getNumProperties() > 0)
				{
					AlembicHairTranslatorUtils::SetGroomAttributes(HairDescription, ArbParams);
					bCheckGroomAttributes = false;
				}
			}
		}

		LocalMatrix =  ParentMatrix * ConvertAlembicMatrix(MatrixSample.getMatrix());
	}

	if (NumChildren > 0)
	{
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			ParseObject(InObject.getChild(ChildIndex), HairDescription, LocalMatrix, ConversionMatrix, Scale, bCheckGroomAttributes);
		}
	}
}

bool FAlembicHairTranslator::Translate(const FString& FileName, FHairDescription& HairDescription, const FGroomConversionSettings& ConversionSettings)
{
	/** Factory used to generate objects*/
	Alembic::AbcCoreFactory::IFactory Factory;
	Alembic::AbcCoreFactory::IFactory::CoreType CompressionType = Alembic::AbcCoreFactory::IFactory::kUnknown;
	/** Archive-typed ABC file */
	Alembic::Abc::IArchive Archive;
	/** Alembic typed root (top) object*/
	Alembic::Abc::IObject TopObject;

	Factory.setPolicy(Alembic::Abc::ErrorHandler::kThrowPolicy);
	Factory.setOgawaNumStreams(12);

	// Extract Archive and compression type from file
	Archive = Factory.getArchive(TCHAR_TO_UTF8(*FileName), CompressionType);
	if (!Archive.valid())
	{
		UE_LOG(LogAlembicHairImporter, Warning, TEXT("Failed to open %s: Not a valid Alembic file."), *FileName);
		return false;
	}

	// Get Top/root object
	TopObject = Alembic::Abc::IObject(Archive, Alembic::Abc::kTop);
	if (!TopObject.valid())
	{
		UE_LOG(LogAlembicHairImporter, Warning, TEXT("Failed to import %s: Root not is not valid."), *FileName);
		return false;
	}

	FMatrix ConversionMatrix = FScaleMatrix::Make(ConversionSettings.Scale) * FRotationMatrix::Make(FQuat::MakeFromEuler(ConversionSettings.Rotation));
	FMatrix ParentMatrix = FMatrix::Identity;
	ParseObject(TopObject, HairDescription, ParentMatrix, ConversionMatrix, ConversionSettings.Scale.X, true);

	return HairDescription.IsValid();
}

static void ValidateObject(const Alembic::Abc::IObject& InObject, bool& bHasGeometry, int32& NumCurves)
{
	// Validate that the Alembic has curves only
	// Any PolyMesh will cause the Alembic to be rejected by this translator

	Alembic::AbcCoreAbstract::ObjectHeader Header = InObject.getHeader();
	const Alembic::Abc::MetaData ObjectMetaData = InObject.getMetaData();
	const uint32 NumChildren = InObject.getNumChildren();

	if (Alembic::AbcGeom::ICurves::matches(ObjectMetaData))
	{
		++NumCurves;
	}
	else if (Alembic::AbcGeom::IPolyMesh::matches(ObjectMetaData))
	{
		bHasGeometry = true;
	}

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren && !bHasGeometry; ++ChildIndex)
	{
		ValidateObject(InObject.getChild(ChildIndex), bHasGeometry, NumCurves);
	}
}

bool FAlembicHairTranslator::CanTranslate(const FString& FilePath)
{
	if (!IsFileExtensionSupported(FPaths::GetExtension(FilePath)))
	{
		return false;
	}

	/** Factory used to generate objects*/
	Alembic::AbcCoreFactory::IFactory Factory;
	Alembic::AbcCoreFactory::IFactory::CoreType CompressionType = Alembic::AbcCoreFactory::IFactory::kUnknown;
	/** Archive-typed ABC file */
	Alembic::Abc::IArchive Archive;
	/** Alembic typed root (top) object*/
	Alembic::Abc::IObject TopObject;

	Factory.setPolicy(Alembic::Abc::ErrorHandler::kThrowPolicy);
	Factory.setOgawaNumStreams(12);

	// Extract Archive and compression type from file
	Archive = Factory.getArchive(TCHAR_TO_UTF8(*FilePath), CompressionType);
	if (!Archive.valid())
	{
		return false;
	}

	// Get Top/root object
	TopObject = Alembic::Abc::IObject(Archive, Alembic::Abc::kTop);
	if (!TopObject.valid())
	{
		return false;
	}

	bool bHasGeometry = false;
	int32 NumCurves = 0;

	ValidateObject(TopObject, bHasGeometry, NumCurves);

	return !bHasGeometry && NumCurves > 0;
}

bool FAlembicHairTranslator::IsFileExtensionSupported(const FString& FileExtension) const
{
	return GetSupportedFormat().StartsWith(FileExtension);
}

FString FAlembicHairTranslator::GetSupportedFormat() const
{
	return TEXT("abc;Alembic hair strands file");
}
