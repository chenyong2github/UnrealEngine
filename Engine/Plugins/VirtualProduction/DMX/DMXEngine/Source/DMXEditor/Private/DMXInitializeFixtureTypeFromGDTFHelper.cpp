// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXInitializeFixtureTypeFromGDTFHelper.h"

#include "DMXEditorLog.h"
#include "DMXProtocolSettings.h"
#include "DMXZipper.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXGDTFAssetImportData.h"
#include "Library/DMXImportGDTF.h"

#include "XmlFile.h"
#include "XmlNode.h"
#include "Algo/Count.h"
#include "Algo/Find.h"


#define LOCTEXT_NAMESPACE "DMXInitializeFixtureTypeFromGDTFHelper"

namespace UE::DMX::DMXInitializeFixtureTypeFromGDTFHelper::Private
{
	/**
	 * Depending on the GDTF Spec, some XML Attribute Names might start with or without DMX.
	 *
	 * @param ParentNode		The Parent Node to search in
	 * @param NodeName			The Attribute Name with DMX tag. This is the correct form, e.g. 'DMXMode', not 'Mode', 'DMXChannels', not 'Channels'.
	 * @return					Returns a pointer to the child node, or nullptr if the child cannot be found
	 */
	const FXmlNode* FindChildNodeEvenIfDMXSubstringIsMissing(const FXmlNode& ParentNode, const FString& AttributeNameWithDMXTag)
	{
		if (const FXmlNode* ChildNodePtrWithSubstring = ParentNode.FindChildNode(AttributeNameWithDMXTag))
		{
			return ChildNodePtrWithSubstring;
		}
		else if (const FXmlNode* ChildNodePtrWithoutSubstring = ParentNode.FindChildNode(AttributeNameWithDMXTag.RightChop(3)))
		{
			return ChildNodePtrWithoutSubstring;
		}

		return nullptr;
	}

	/**
	 * Depending on the GDTF Spec, some XML Attribute Names might start with or without DMX.
	 *
	 * @param ParentNode		The Parent Node to search in
	 * @param NodeName			The Attribute Name with DMX tag. This is the correct form, e.g. 'DMXMode', not 'Mode', 'DMXChannels', not 'Channels'.
	 * @return					Returns the Attribute as String
	 */
	FString FindAttributeEvenIfDMXSubstringIsMissing(const FXmlNode& ParentNode, const FString& AttributeNameWithDMXTag)
	{
		FString Attribute = ParentNode.GetAttribute(AttributeNameWithDMXTag);
		if (Attribute.IsEmpty())
		{
			Attribute = ParentNode.GetAttribute(AttributeNameWithDMXTag.RightChop(3));
		}

		return Attribute;
	}
};

bool FDMXInitializeFixtureTypeFromGDTFHelper::GenerateModesFromGDTF(UDMXEntityFixtureType& InOutFixtureType, const UDMXImportGDTF& InGDTF)
{
	FDMXInitializeFixtureTypeFromGDTFHelper Instance;
	return Instance.GenerateModesFromGDTFInternal(InOutFixtureType, InGDTF);
}

bool FDMXInitializeFixtureTypeFromGDTFHelper::GenerateModesFromGDTFInternal(UDMXEntityFixtureType& InOutFixtureType, const UDMXImportGDTF& InGDTF) const
{
	UDMXGDTFAssetImportData* GDTFAssetImportData = InGDTF.GetGDTFAssetImportData();
	if (!ensureMsgf(GDTFAssetImportData, TEXT("Found GDTF Asset that has no GDTF asset import data subobject.")))
	{
		return false;
	}

	const TSharedRef<FDMXZipper> Zip = MakeShared<FDMXZipper>();
	if (!Zip->LoadFromData(GDTFAssetImportData->GetRawSourceData()))
	{
		return false;
	}

	const FDMXZipper::FDMXScopedUnzipToTempFile ScopedUnzippedDescriptionXml(Zip, TEXT("Description.xml"));
	if (ScopedUnzippedDescriptionXml.TempFilePathAndName.IsEmpty())
	{
		return false;
	}

	const TSharedRef<FXmlFile> XmlFile = MakeShared<FXmlFile>();
	if (!XmlFile->LoadFile(ScopedUnzippedDescriptionXml.TempFilePathAndName))
	{
		return false;
	}

	// Largely a copy of GDTF Factory, but avoiding the asset
	const FXmlNode* RootNode = XmlFile->GetRootNode();
	if (!RootNode)
	{
		return false;
	}

	constexpr TCHAR FixtureTypeTag[] = TEXT("FixtureType");
	const FXmlNode* FixtureTypeNode = RootNode->FindChildNode(FixtureTypeTag);
	if (!FixtureTypeNode)
	{
		return false;
	}

	// Iterate modes and build the DMXFixtureMode array from here
	const FXmlNode* const ModesNode = UE::DMX::DMXInitializeFixtureTypeFromGDTFHelper::Private::FindChildNodeEvenIfDMXSubstringIsMissing(*FixtureTypeNode, TEXT("DMXModes"));
	if (!ModesNode)
	{
		return false;
	}

	for (const FXmlNode* DMXModeNode : ModesNode->GetChildrenNodes())
	{
		FDMXFixtureMode Mode;
		if (GenerateMode(*FixtureTypeNode, *DMXModeNode, Mode))
		{
			InOutFixtureType.Modes.Add(Mode);
		}
	}

	for (int32 ModeIndex = 0; ModeIndex < InOutFixtureType.Modes.Num(); ModeIndex++)
	{
		InOutFixtureType.UpdateChannelSpan(ModeIndex);
	}
	InOutFixtureType.GetOnFixtureTypeChanged().Broadcast(&InOutFixtureType);

	CleanupAttributes(InOutFixtureType);

	return true;
}

bool FDMXInitializeFixtureTypeFromGDTFHelper::GenerateMode(const FXmlNode& InFixtureTypeNode, const FXmlNode& InDMXModeNode, FDMXFixtureMode& OutMode) const
{
	if (!ensureMsgf(InFixtureTypeNode.GetTag() == TEXT("fixturetype"), TEXT("Trying to read Fixture Type node, but node is tagged '%s'."), *InFixtureTypeNode.GetTag()))
	{
		return false;
	}
	if (!ensureMsgf(InDMXModeNode.GetTag() == TEXT("dmxmode") || InDMXModeNode.GetTag() == TEXT("mode"), TEXT("Trying to read Channel node, but node is tagged '%s'."), *InDMXModeNode.GetTag()))
	{
		return false;
	}

	// Get geometry nodes specific to this mode (can be empty)
	TArray<const FXmlNode*> GeometryNodes;
	constexpr TCHAR GeometriesTag[] = TEXT("Geometries");
	if (const FXmlNode* GeometriesNode = InFixtureTypeNode.FindChildNode(GeometriesTag))
	{
		constexpr TCHAR GeometryTag[] = TEXT("Geometry");
		const FString GeometryName = InDMXModeNode.GetAttribute(GeometryTag);

		// Root Geometry of this Mode
		const FXmlNode* const* GeometryNodePtr = Algo::FindByPredicate(GeometriesNode->GetChildrenNodes(), [&GeometryName, GeometryTag](const FXmlNode* GeometryNode)
			{
				constexpr TCHAR NameTag[] = TEXT("Name");
				if (GeometryNode->GetAttribute(NameTag) == GeometryName)
				{
					return true;
				}

				return false;
			});

		if (GeometryNodePtr)
		{
			GeometryNodes = GetChildrenRecursive(**GeometryNodePtr);
		}
	}

	// Depending on the GDTF spec in use modes may be stored in the "Modes" or "DMXModes" node.
	const FXmlNode* const ChannelsNode = UE::DMX::DMXInitializeFixtureTypeFromGDTFHelper::Private::FindChildNodeEvenIfDMXSubstringIsMissing(InDMXModeNode, TEXT("DMXChannels"));
	if (!ChannelsNode)
	{
		return false;
	}

	FDMXFixtureMode Mode;
	Mode.FixtureMatrixConfig.CellAttributes.Reset();

	constexpr TCHAR NameTag[] = TEXT("Name");
	Mode.ModeName = InDMXModeNode.GetAttribute(NameTag); // Not using the class's default attribute

	int32 NextFunctionStartingChannel = 1;
	for (const FXmlNode* DMXChannelNode : ChannelsNode->GetChildrenNodes())
	{
		FDMXFixtureFunction Function;
		if (!GetChannelPropertiesWithoutStartingChannel(*DMXChannelNode, Function))
		{
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("Channel: %i"), NextFunctionStartingChannel);

		constexpr TCHAR GeometryTag[] = TEXT("Geometry");
		const FString GeometryName = DMXChannelNode->GetAttribute(GeometryTag);

		// Count the number of geometries this channel references
		const int32 NumReferencesInGeometry = Algo::CountIf(GeometryNodes, [&GeometryName, GeometryTag](const FXmlNode* GeometryNode)
			{
				if (GeometryNode->GetAttribute(GeometryTag) == GeometryName)
				{
					return true;
				}

				return false;
			});

		if (NumReferencesInGeometry < 2)
		{
			// A single or no referenced Geometry - Create a normal function.
			Function.Channel = FMath::Max(NextFunctionStartingChannel, Mode.FixtureMatrixConfig.GetLastChannel() + 1);
			Mode.Functions.Add(Function);
		}
		else
		{
			// Try to add a matrix function instead of a common function
			if (Mode.bFixtureMatrixEnabled &&
				Mode.Functions.Num() > 0 &&
				Mode.FixtureMatrixConfig.GetLastChannel() > Mode.Functions.Last().GetLastChannel())
			{
				UE_LOG(LogDMXEditor, Warning, TEXT("Mode '%s' contains many matrices, but this version of Unreal Engine only supports one matrix. Skipping import of mode."), *Mode.ModeName)
					Mode.Functions.Reset();
				Mode.bFixtureMatrixEnabled = false;
				Mode.FixtureMatrixConfig.CellAttributes.Reset();
				Mode.ModeName = FString::Printf(TEXT("n/a '%s' [not supported in this Engine Version]"), *Mode.ModeName);
				break;
			}

			FDMXFixtureCellAttribute MatrixAttribute;
			MatrixAttribute.Attribute = Function.Attribute;
			MatrixAttribute.bUseLSBMode = Function.bUseLSBMode;
			MatrixAttribute.DataType = Function.DataType;
			MatrixAttribute.DefaultValue = Function.DefaultValue;

			Mode.FixtureMatrixConfig.CellAttributes.Add(MatrixAttribute);
			Mode.FixtureMatrixConfig.YCells = NumReferencesInGeometry;
			if (!Mode.bFixtureMatrixEnabled)
			{
				Mode.FixtureMatrixConfig.FirstCellChannel = NextFunctionStartingChannel;
				Mode.bFixtureMatrixEnabled = true;
			}
		}

		NextFunctionStartingChannel += Function.GetNumChannels();
	}

	OutMode = Mode;
	return true;
}

bool FDMXInitializeFixtureTypeFromGDTFHelper::GetChannelPropertiesWithoutStartingChannel(const FXmlNode& DMXChannelNode, FDMXFixtureFunction& OutFunction) const
{
	if (!ensureMsgf(DMXChannelNode.GetTag() == TEXT("dmxchannel") || DMXChannelNode.GetTag() == TEXT("channel"), TEXT("Trying to read Channel node, but node is tagged '%s'."), *DMXChannelNode.GetTag()))
	{
		return false;
	}

	// As per legacy consider only the first function, although there may be many.
	constexpr TCHAR LogicalChannelTag[] = TEXT("LogicalChannel");
	const FXmlNode* LogicalChannelNode = DMXChannelNode.FindChildNode(LogicalChannelTag);
	if (!LogicalChannelNode)
	{
		return false;
	}

	constexpr TCHAR ChannelFunctionTag[] = TEXT("ChannelFunction");
	const FXmlNode* ChannelFunctionNode = LogicalChannelNode->FindChildNode(ChannelFunctionTag);
	if (!ChannelFunctionNode)
	{
		return false;
	}

	// Name
	constexpr TCHAR NameTag[] = TEXT("Name");
	const FString Name = ChannelFunctionNode->GetAttribute(NameTag);

	OutFunction.FunctionName = Name.IsEmpty() ? TEXT("<empty>") : Name;
	OutFunction.Attribute.Name = Name.IsEmpty() ? NAME_None : FName(*Name);
	
	// Default value
	constexpr TCHAR DefaultTag[] = TEXT("Default");
	int32 DefaultValue;
	if (LexTryParseString(DefaultValue, *ChannelFunctionNode->GetAttribute(DefaultTag)))
	{
		OutFunction.DefaultValue = DefaultValue;
	}

	// Word size and endianness
	constexpr TCHAR OffsetTag[] = TEXT("Offset");
	TArray<int32> OffsetArray;
	TArray<FString> OffsetStrArray;
	DMXChannelNode.GetAttribute(OffsetTag).ParseIntoArray(OffsetStrArray, TEXT(","));

	for (int32 OffsetIndex = 0; OffsetIndex < OffsetStrArray.Num(); ++OffsetIndex)
	{
		int32 OffsetValue;
		LexTryParseString(OffsetValue, *OffsetStrArray[OffsetIndex]);
		OffsetArray.Add(OffsetValue);
	}

	if (OffsetArray.Num() > 0)
	{
		// Compute number of used addresses
		int32 AddressMin = DMX_MAX_ADDRESS;
		int32 AddressMax = 0;
		for (const int32& Address : OffsetArray)
		{
			AddressMin = FMath::Min(AddressMin, Address);
			AddressMax = FMath::Max(AddressMax, Address);
		}
		const int32 NumUsedAddresses = FMath::Clamp(AddressMax - AddressMin + 1, 1, DMX_MAX_FUNCTION_SIZE);

		OutFunction.DataType = static_cast<EDMXFixtureSignalFormat>(NumUsedAddresses - 1);

		// Offsets represent the channels in MSB order. If they are in reverse order, it means this Function uses LSB format.
		if (OffsetArray.Num() > 1)
		{
			OutFunction.bUseLSBMode = OffsetArray[0] > OffsetArray[1];
		}
		else
		{
			OutFunction.bUseLSBMode = false;
		}
	}
	else
	{
		OutFunction.DataType = EDMXFixtureSignalFormat::E8Bit;
	}

	return true;
}

void FDMXInitializeFixtureTypeFromGDTFHelper::CleanupAttributes(UDMXEntityFixtureType& InOutFixtureType) const
{
	// Get Protocol Setting's default attributes
	const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
	TMap<FName, TArray<FString> > AttributeNameToKeywordsMap;
	for (const FDMXAttribute& Attribute : ProtocolSettings->Attributes)
	{
		TArray<FString> Keywords = Attribute.GetKeywords();

		AttributeNameToKeywordsMap.Emplace(Attribute.Name, Keywords);
	}

	TArray<FName> AssignedAttributeNames;
	for (FDMXFixtureMode& Mode : InOutFixtureType.Modes)
	{
		for (FDMXFixtureFunction& Function : Mode.Functions)
		{
			const TTuple<FName, TArray<FString>>* AttributeNameToKeywordPairPtr = Algo::FindByPredicate(AttributeNameToKeywordsMap, [Function](const TTuple<FName, TArray<FString>> AttributeNameToKeywordPair)
				{
					if (Function.Attribute.Name == AttributeNameToKeywordPair.Key ||
						AttributeNameToKeywordPair.Value.Contains(Function.Attribute.Name.ToString()))
					{
						return true;
					}

					return false;
				});

			if (AttributeNameToKeywordPairPtr && !AssignedAttributeNames.Contains(AttributeNameToKeywordPairPtr->Key))
			{
				Function.Attribute.Name = AttributeNameToKeywordPairPtr->Key;
				AssignedAttributeNames.Add(AttributeNameToKeywordPairPtr->Key);
			}
			else
			{
				if (AssignedAttributeNames.Contains(Function.Attribute.Name))
				{
					UE_LOG(LogDMXEditor, Warning, TEXT("Attribute '%s' occurs twice in Fixture Type '%s'. Reseting second occurence to None"), *Function.Attribute.Name.ToString(), *InOutFixtureType.GetName());
				}
				else
				{
					// Attribute not specified in project settings, but unique
					AssignedAttributeNames.Add(Function.Attribute.Name);
				}
			}
		}
	}
}

TArray<const FXmlNode*> FDMXInitializeFixtureTypeFromGDTFHelper::GetChildrenRecursive(const FXmlNode& ParentNode) const
{
	TArray<const FXmlNode*> Result;

	for (const FXmlNode* Child : ParentNode.GetChildrenNodes())
	{
		Result.Add(Child);
		Result.Append(GetChildrenRecursive(*Child));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
