// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVR/DMXMVRFixture.h"

#include "DMXProtocolCommon.h"
#include "DMXRuntimeLog.h"

#include "XmlFile.h"
#include "XmlNode.h"


namespace UE::DMXRuntime::DMXMVRFixture::Private
{
	/** Parses the Name from the MVR Fixture Node, returns true on success. */
	bool ParseName(const FXmlNode* const InFixtureNode, FString& OutName)
	{
		check(InFixtureNode);

		static const FString InFixtureNodeAttribute_Name = TEXT("Name");
		OutName = InFixtureNode->GetAttribute(InFixtureNodeAttribute_Name);
		if (!OutName.IsEmpty())
		{
			return true;
		}

		return false;
	}

	/** Parses the UUID from the MVR Fixture Node, returns true on success. */
	bool ParseUUID(const FXmlNode* const InFixtureNode, FGuid& OutUUID)
	{
		check(InFixtureNode);

		static const FString InFixtureNodeAttribute_UUID = TEXT("UUID");
		const FString UUIDString = InFixtureNode->GetAttribute(InFixtureNodeAttribute_UUID);
		if (FGuid::Parse(UUIDString, OutUUID))
		{
			return true;
		}
		return false;
	}

	/** Parses the Matrix from the MVR Fixture Node, optional, always succeeds. */
	void ParseMatrix(const FXmlNode* const InFixtureNode, TOptional<FTransform>& OutTransform)
	{
		check(InFixtureNode);
		OutTransform.Reset();

		static const FString ChildNodeName_Matrix = TEXT("Matrix");
		const FXmlNode* const MatrixNode = InFixtureNode->FindChildNode(ChildNodeName_Matrix);

		if (!MatrixNode)
		{
			return;
		}

		auto GetVectorStringsFromMatrixStringLambda([](const FString& MatrixString) -> TArray<FString>
			{
				TArray<FString> Result;
				int32 SubstringIndex = 0;
				for (int32 SubStringNumber = 0; SubStringNumber < 4; SubStringNumber++)
				{
					SubstringIndex = MatrixString.Find(TEXT("{"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SubstringIndex) + 1;
					const int32 EndIndex = MatrixString.Find(TEXT("}"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SubstringIndex);

					const FString SubString = MatrixString.Mid(SubstringIndex, EndIndex - SubstringIndex);
					Result.Add(SubString);
				}

				return Result;
			});

		auto GetVectorFromVectorStringLambda([](const FString& VectorString) -> FVector
			{
				const int32 XIndex = 0;
				const int32 YIndex = VectorString.Find(TEXT(","), ESearchCase::IgnoreCase, ESearchDir::FromStart, 0) + 1;
				const int32 ZIndex = VectorString.Find(TEXT(","), ESearchCase::IgnoreCase, ESearchDir::FromStart, YIndex) + 1;

				const FString XString = "X=" + VectorString.Mid(XIndex, YIndex - XIndex);
				const FString YString = "Y=" + VectorString.Mid(YIndex, ZIndex - YIndex);
				const FString ZString = "Z=" + VectorString.Mid(ZIndex, VectorString.Len() - ZIndex);

				FVector Vector;
				if (Vector.InitFromString(XString + YString + ZString))
				{
					return Vector;
				}

				return FVector::ZeroVector;
			});

		const TArray<FString> SubStrings = GetVectorStringsFromMatrixStringLambda(MatrixNode->GetContent());
		if (SubStrings.Num() < 4)
		{
			return;
		}

		const FVector U = GetVectorFromVectorStringLambda(SubStrings[0]) / 10.f;
		const FVector V = GetVectorFromVectorStringLambda(SubStrings[1]) / 10.f;
		const FVector W = GetVectorFromVectorStringLambda(SubStrings[2]) / 10.f;
		const FVector O = GetVectorFromVectorStringLambda(SubStrings[3]) / 10.f;

		FMatrix Matrix;
		Matrix.M[0][0] = U.X;
		Matrix.M[0][1] = V.X;
		Matrix.M[0][2] = W.X;
		Matrix.M[0][3] = O.X;

		Matrix.M[1][0] = U.Y;
		Matrix.M[1][1] = V.Y;
		Matrix.M[1][2] = W.Y;
		Matrix.M[1][3] = -O.Y; // From MVR's right hand to UE's left hand coordinate system

		Matrix.M[2][0] = U.Z;
		Matrix.M[2][1] = V.Z;
		Matrix.M[2][2] = W.Z;
		Matrix.M[2][3] = O.Z;

		Matrix.M[3][0] = 0.0;
		Matrix.M[3][1] = 0.0;
		Matrix.M[3][2] = 0.0;
		Matrix.M[3][3] = 1.0;

		OutTransform = FTransform(Matrix.GetTransposed());

		// Scale from milimeters to centimeters
		OutTransform->SetScale3D(OutTransform->GetScale3D() * 10.f);

		// GDTFs are facing down on the Z-Axis, but UE Actors are facing up 
		const FQuat InvertUpRotationQuaternion = FQuat(OutTransform->GetRotation().GetAxisY(), PI);
		OutTransform->SetRotation(OutTransform->GetRotation() * InvertUpRotationQuaternion);
	}
	
	/** Parses the GDTFSpec from the MVR Fixture Node, returns true on success. */
	bool ParseGDTFSpec(const FXmlNode* const InFixtureNode, FString& OutGDTFSpec)
	{
		check(InFixtureNode);

		static const FString ChildNodeName_GDTFSpec = TEXT("GDTFSpec");
		const FXmlNode* const GDTFSpecNode = InFixtureNode->FindChildNode(ChildNodeName_GDTFSpec);
		if (GDTFSpecNode)
		{
			OutGDTFSpec = GDTFSpecNode->GetContent();
			return true;
		}

		return false;
	}

	/** Parses the GDTFMode from the MVR Fixture Node, returns true on success. */
	bool ParseGDTFMode(const FXmlNode* const InFixtureNode, FString& OutGDTFMode)
	{
		check(InFixtureNode);

		static const FString ChildNodeName_GDTFMode = TEXT("GDTFMode");
		const FXmlNode* const GDTFModeNode = InFixtureNode->FindChildNode(ChildNodeName_GDTFMode);
		if (GDTFModeNode)
		{
			OutGDTFMode = GDTFModeNode->GetContent();
			return true;
		}
		return true;
	}

	/** Parses Focus from the MVR Fixture Node, optional, always succeeds. */
	void ParseFocus(const FXmlNode* const InFixtureNode, TOptional<FGuid>& OutFocus)
	{
		check(InFixtureNode);
		OutFocus.Reset();

		static const FString ChildNodeName_Focus = TEXT("Focus");
		const FXmlNode* const FocusNode = InFixtureNode->FindChildNode(ChildNodeName_Focus);
		if (FocusNode)
		{
			const FString FocusString = InFixtureNode->GetContent();
			FGuid FocusGuid;
			if (FGuid::Parse(FocusString, FocusGuid))
			{
				OutFocus = FocusGuid;
			}
		};
	}

	/** Parses Cast Shadows from the MVR Fixture Node, optional, always succeeds. */
	void ParseCastShadow(const FXmlNode* const InFixtureNode, TOptional<bool>& OutCastShadow)
	{
		check(InFixtureNode);
		OutCastShadow.Reset();

		static const FString ChidNodeName_CastShadow = TEXT("CastShadow");
		const FXmlNode* const CastShadowNode = InFixtureNode->FindChildNode(ChidNodeName_CastShadow);
		if (CastShadowNode)
		{
			OutCastShadow = !(CastShadowNode->GetContent() == TEXT("0")) || !CastShadowNode->GetContent().Equals(TEXT("true"), ESearchCase::IgnoreCase);
		}
	}

	/** Parses Position of from MVR Fixture Node, optional, always succeeds. */
	void ParsePosition(const FXmlNode* const InFixtureNode, TOptional<FGuid>& OutPosition)
	{
		check(InFixtureNode);
		OutPosition.Reset();

		static const FString ChildNodenName_Position = TEXT("Position");
		const FXmlNode* const PositionNode = InFixtureNode->FindChildNode(ChildNodenName_Position);
		if (PositionNode)
		{
			FGuid PositionGuid;
			if (FGuid::Parse(PositionNode->GetContent(), PositionGuid))
			{
				OutPosition = PositionGuid;
			}
		}
	}

	/** Parses the Fixture Id from the MVR Fixture Node, returns true on success. */
	bool ParseFixtureId(const FXmlNode* const InFixtureNode, FString& OutFixtureId)
	{
		check(InFixtureNode);
		OutFixtureId.Reset();

		static const FString ChildNodenName_FixtureId = TEXT("FixtureId");
		const FXmlNode* const FixtureIdNode = InFixtureNode->FindChildNode(ChildNodenName_FixtureId);
		if (FixtureIdNode)
		{
			OutFixtureId = FixtureIdNode->GetContent();
			return true;
		}

		return false;
	}

	/** Parses the Unit Number from the MVR Fixture Node, returns true on success. */
	bool ParseUnitNumber(const FXmlNode* const InFixtureNode, int32& OutUnitNumber)
	{
		check(InFixtureNode);

		static const FString ChildNodenName_UnitNumber = TEXT("FixtureId");
		const FXmlNode* const UnitNumberNode = InFixtureNode->FindChildNode(ChildNodenName_UnitNumber);
		if (UnitNumberNode)
		{
			int32 UnitNumber;
			if (LexTryParseString(UnitNumber, *UnitNumberNode->GetContent()))
			{
				OutUnitNumber = UnitNumber;
				return true;
			}
		}
		return false;
	}

	/** Parses the Addresses from the MVR Fixture Node, defaulted, always succeeds */
	bool ParseAddresses(const FXmlNode* const InFixtureNode, FDMXMVRFixtureAddresses& OutAddresses)
	{
		check(InFixtureNode);

		static const FString ChildNodenName_Addresses = TEXT("Addresses");
		const FXmlNode* const AddressesNode = InFixtureNode->FindChildNode(ChildNodenName_Addresses);

		static const FString ChildNodenName_Address = TEXT("Address");
		const FXmlNode* const AddressNode = AddressesNode ? AddressesNode->FindChildNode(ChildNodenName_Address) : nullptr;

		if (AddressNode)
		{
			int32 Universe = -1;
			int32 Address = -1;

			auto ParseAddressStringLambda = [](const FXmlNode* const InAddressNode, int32& OutUniverse, int32& OutAddress) -> const bool
			{
				const FString InAddressString = InAddressNode->GetContent();

				int32 SeparatorIndex = INDEX_NONE;
				if (InAddressString.FindChar('.', SeparatorIndex))
				{
					if (SeparatorIndex > 0 && SeparatorIndex < InAddressString.Len() - 2)
					{
						const FString UniverseString = InAddressString.Left(SeparatorIndex);
						const FString AddressString = InAddressString.RightChop(SeparatorIndex + 1);

						const bool bValidUniverse = LexTryParseString(OutUniverse, *UniverseString) && OutUniverse >= 0 && OutUniverse <= DMX_MAX_UNIVERSE;
						const bool bValidAddress = LexTryParseString(OutAddress, *UniverseString) && OutAddress >= 0 && OutAddress <= DMX_MAX_ADDRESS;

						return bValidUniverse && bValidAddress;
					}
				}
				else if (LexTryParseString(OutAddress, *InAddressString))
				{
					static const int32 UniverseSize = 512;

					OutUniverse = OutAddress / UniverseSize;
					OutAddress = OutAddress % UniverseSize;

					return 
						OutUniverse >= 0 && 
						OutUniverse <= DMX_MAX_UNIVERSE &&
						OutAddress >= 0 && 
						OutAddress <= DMX_MAX_ADDRESS;
				}

				return false;
			}(AddressNode, Universe, Address);
			if (ParseAddressStringLambda)
			{
				OutAddresses.Universe = Universe;
				OutAddresses.Address = Address;

				return true;
			}
		}
		return false;
	}

	/** Parses the CID Color from the MVR Fixture Node, optional, always succeeds. */
	void ParseCIEColor(const FXmlNode* const InFixtureNode, TOptional<FDMXMVRColorCIE>& OutCIEColor)
	{
		check(InFixtureNode);

		static const FString ChildNodenName_CIEColor = TEXT("CIEColor");
		const FXmlNode* const CIEColorNode = InFixtureNode->FindChildNode(ChildNodenName_CIEColor);
		if (CIEColorNode)
		{
			const FString CIEColorString = CIEColorNode->GetContent();
			if (CIEColorString.IsEmpty())
			{
				return;
			}

			TArray<FString> CIEColorArray;
			CIEColorString.ParseIntoArray(CIEColorArray, TEXT(","));

			if (CIEColorArray.Num() != 3	)
			{
				return;
			}

			FDMXMVRColorCIE Result;
			bool bSuccess = LexTryParseString<float>(Result.X, *CIEColorArray[0]);
			bSuccess = bSuccess && LexTryParseString<float>(Result.Y, *CIEColorArray[1]);
			bSuccess = bSuccess && LexTryParseString<uint8>(Result.YY, *CIEColorArray[2]);

			if (bSuccess)
			{
				OutCIEColor = Result;
			}
		}
	}

	/** Parses the Fixture Type Id from MVR Fixture Node, optional, always succeeds. */
	void ParseFixtureTypeId(const FXmlNode* const InFixtureNode, TOptional<int32>& OutFixtureTypeId)
	{
		check(InFixtureNode);
		OutFixtureTypeId.Reset();

		static const FString ChildNodenName_FixtureTypeId = TEXT("FixtureTypeId");
		const FXmlNode* const FixtureTypeIdNode = InFixtureNode->FindChildNode(ChildNodenName_FixtureTypeId);
		if (FixtureTypeIdNode)
		{
			int32 FixtureTypeId;
			if (LexTryParseString(FixtureTypeId, *FixtureTypeIdNode->GetContent()))
			{
				OutFixtureTypeId = FixtureTypeId;
			}
		}
	}

	/** Parses the Custom Id from MVR Fixture Node, optional, always succeeds. */
	void ParseCustomId(const FXmlNode* const InFixtureNode, TOptional<int32>& OutCustomId)
	{
		check(InFixtureNode);
		OutCustomId.Reset();

		static const FString ChildNodenName_Position = TEXT("Position");
		const FXmlNode* const PositionNode = InFixtureNode->FindChildNode(ChildNodenName_Position);
		if (PositionNode)
		{
			int32 FixtureTypeId;
			if (LexTryParseString(FixtureTypeId, *PositionNode->GetContent()))
			{
				OutCustomId = FixtureTypeId;
			}
		}
	}

	/** Parses the Mapping from MVR Fixture Node, optional, always succeeds. */
	void ParseMapping(const FXmlNode* const InFixtureNode, TOptional<FDMXMVRFixtureMapping>& OutMapping)
	{
		check(InFixtureNode);
		OutMapping.Reset();

		static const FString ChildNodenName_Mapping = TEXT("Position");
		const FXmlNode* const MappingNode = InFixtureNode->FindChildNode(ChildNodenName_Mapping);
		if (MappingNode)
		{
			int32 FixtureTypeId;
			if (LexTryParseString(FixtureTypeId, *MappingNode->GetContent()))
			{
				static const FString ChildNodenName_LinkDef = TEXT("LinkDef");
				const FXmlNode* const LinkDefNOde = MappingNode->FindChildNode(ChildNodenName_LinkDef);
				FDMXMVRFixtureMapping Mapping;
				if (LinkDefNOde && !FGuid::Parse(LinkDefNOde->GetContent(), Mapping.LinkDef))
				{
					return;
				}

				static const FString ChildNodenName_UX = TEXT("UX");
				const FXmlNode* const UXNode = InFixtureNode->FindChildNode(ChildNodenName_UX);
				int32 UX;
				if (UXNode && !LexTryParseString(UX, *UXNode->GetContent()))
				{
					Mapping.UX = UX;
					return;
				}

				static const FString ChildNodenName_UY = TEXT("UY");
				const FXmlNode* const UYNode = InFixtureNode->FindChildNode(ChildNodenName_UY);
				int32 UY;
				if (UYNode && !LexTryParseString(UY, *UYNode->GetContent()))
				{
					return;
				}
				Mapping.UY = UY;

				static const FString ChildNodenName_OX = TEXT("OX");
				const FXmlNode* const OXNode = InFixtureNode->FindChildNode(ChildNodenName_OX);
				int32 OX;
				if (OXNode && !LexTryParseString(OX, *OXNode->GetContent()))
				{
					return;
				}
				Mapping.OX = OX;

				static const FString ChildNodenName_OY = TEXT("OY");
				const FXmlNode* const OYNode = InFixtureNode->FindChildNode(ChildNodenName_OY);
				int32 OY;
				if (OYNode && !LexTryParseString(OY, *OYNode->GetContent()))
				{
					return;
				}
				Mapping.OY = OY;

				static const FString ChildNodenName_RZ = TEXT("RZ");
				const FXmlNode* const RZNode = InFixtureNode->FindChildNode(ChildNodenName_RZ);
				int32 RZ;
				if (RZNode && !LexTryParseString(RZ, *RZNode->GetContent()))
				{
					return;
				}
				Mapping.RZ = RZ;

				OutMapping = Mapping;
			}
		}
	}

	/** Parses the Gobo from MVR Fixture Node, optional, always succeeds. */
	void ParseGobo(const FXmlNode* const InFixtureNode, TOptional<FDMXMVRFixtureGobo>& OutGobo)
	{
		check(InFixtureNode);
		OutGobo.Reset();

		static const FString ChildNodenName_Gobo = TEXT("Position");
		const FXmlNode* const GoboNode = InFixtureNode->FindChildNode(ChildNodenName_Gobo);
		if (GoboNode)
		{
			const FString& GoboResource = GoboNode->GetContent();
			OutGobo->Value = GoboResource;

			static const FString ChildNodeName_Gobo = TEXT("Rotation");
			const FXmlNode* const GoboRotationNode = GoboNode->FindChildNode(ChildNodeName_Gobo);
			float GoboRotation;
			if (GoboRotationNode && LexTryParseString(GoboRotation, *GoboRotationNode->GetContent()))
			{
				OutGobo->Rotation = GoboRotation;
			}
		}
	}
}

FDMXMVRFixture::FDMXMVRFixture()
{}

FDMXMVRFixture::FDMXMVRFixture(const FXmlNode* const FixtureNode)
{
	// Ensure a valid node
	if (!ensureAlwaysMsgf(FixtureNode, TEXT("Trying to parse MVR Fixture Node, but node is null.")))
	{
		return;
	}

	static const FString NodeName_Fixture = TEXT("Fixture");
	if (!ensureAlwaysMsgf(FixtureNode->GetTag() == NodeName_Fixture, TEXT("Trying to parse MVR Fixture Node, but node name doesn't match.")))
	{
		return;
	}

	// Parse nodes. Those that don't fail are either defaulted or optional.
	using namespace UE::DMXRuntime::DMXMVRFixture::Private;
	if (!ParseName(FixtureNode, Name))
	{
		// Non-optional
		UE_LOG(LogDMXRuntime, Warning, TEXT("Cannot parse Fixture Node as MVR Fixture Name. Failed to parse MVR Node %s."), *FixtureNode->GetTag());
		return;
	}

	if(!ParseUUID(FixtureNode, UUID))
	{
		// Non-optional
		UE_LOG(LogDMXRuntime, Warning, TEXT("Cannot parse Fixture '%s' from MVR. Failed to parse MVR UUID."), *Name);
		return;
	}

	ParseMatrix(FixtureNode, Transform);
	if (Transform.IsSet() && !Transform.GetValue().IsValid())
	{
		// Mend invalid transforms
		UE_LOG(LogDMXRuntime, Warning, TEXT("Imported Fixture '%s' from MVR but its Transform is not valid, using Identity instead."), *Name);
		Transform = FTransform::Identity;
	}

	if (!ParseGDTFSpec(FixtureNode, GDTFSpec))
	{
		// Non-optional
		UE_LOG(LogDMXRuntime, Warning, TEXT("Cannot parse Fixture '%s' from MVR. No valid GDTF Spec specified."), *Name);
		return;
	}

	if (!ParseGDTFMode(FixtureNode, GDTFMode))
	{
		// Non-optional
		UE_LOG(LogDMXRuntime, Warning, TEXT("Cannot parse Fixture '%s' from MVR. No valid GDTF Mode specified."), *Name);
		return;
	}

	ParseFocus(FixtureNode, Focus);
	ParseCastShadow(FixtureNode, bCastShadows);
	ParsePosition(FixtureNode, Position);
	
	if (!ParseFixtureId(FixtureNode, FixtureId))
	{
		// Non-optional, mend with empty string
		UE_LOG(LogDMXRuntime, Warning, TEXT("Imported Fixture '%s' from MVR. No valid Fixture Id specified. Fixture Id set to Fixture Name."), *Name);
		FixtureId = TEXT("");
	}

	if (!ParseUnitNumber(FixtureNode, UnitNumber))
	{
		// Non-optional, but defaulted by standard
		UE_LOG(LogDMXRuntime, Warning, TEXT("Imported Fixture '%s' from MVR. No valid Unit Number specified. Unit Number set to ''0'."), *Name);
		UnitNumber = 0;
	}

	if (!ParseAddresses(FixtureNode, Addresses))
	{
		// Non-optional
		UE_LOG(LogDMXRuntime, Warning, TEXT("Cannot parse Fixture '%s' from MVR. No valid Addresses specified."), *Name);
		return;
	}

	ParseCIEColor(FixtureNode, CIEColor);
	ParseFixtureTypeId(FixtureNode, FixtureTypeId);
	ParseCustomId(FixtureNode, CustomId);
	ParseMapping(FixtureNode, Mapping);
	ParseGobo(FixtureNode, Gobo);
}

bool FDMXMVRFixture::IsValid() const
{
	if (!UUID.IsValid())
	{
		return false;
	}
	
	if (Name.IsEmpty())
	{
		return false;
	}

	if (GDTFSpec.IsEmpty())
	{
		return false;
	}

	if (GDTFMode.IsEmpty())
	{
		return false;
	}

	if (Addresses.Universe < 0 || Addresses.Universe > DMX_MAX_UNIVERSE)
	{
		return false;
	}

	if (Addresses.Address < 1 || Addresses.Address > DMX_MAX_ADDRESS)
	{
		return false;
	}

	return true;
}
