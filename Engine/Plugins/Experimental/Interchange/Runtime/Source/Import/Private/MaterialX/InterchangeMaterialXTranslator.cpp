// Copyright Epic Games, Inc. All Rights Reserved. 

#include "MaterialX/InterchangeMaterialXTranslator.h"
#if WITH_EDITOR
#include "MaterialXFormat/Util.h"
#endif
#include "Engine/Scene.h"
#include "InterchangeImportLog.h"
#include "InterchangeLightNode.h"
#include "InterchangeManager.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeSceneNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeMaterialXDefinitions.h"
#include "InterchangeTexture2DNode.h"
#include "Nodes/InterchangeSourceNode.h"
#include "UObject/GCObjectScopeGuard.h"

#define LOCTEXT_NAMESPACE "InterchangeMaterialXTranslator"

namespace mx = MaterialX;

namespace UE::Interchange::MaterialX
{
	bool IsStandardSurfacePackageLoaded()
	{
		static const bool bStandardSurfacePackageLoaded = []() -> bool
		{
			const FString TextPath{ TEXT("MaterialFunction'/Interchange/Functions/MX_StandardSurface.MX_StandardSurface'") };
			const FString FunctionPath{ FPackageName::ExportTextPathToObjectPath(TextPath) };
			if(FPackageName::DoesPackageExist(FunctionPath))
			{
				if(FSoftObjectPath(FunctionPath).TryLoad())
				{
					return true;
				}
				else
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Couldn't load %s"), *FunctionPath);
				}
			}
			else
			{
				UE_LOG(LogInterchangeImport, Warning, TEXT("Couldn't find %s"), *FunctionPath);
			}

			return false;
		}();

		return bStandardSurfacePackageLoaded;
	}
}

UInterchangeMaterialXTranslator::UInterchangeMaterialXTranslator()
#if WITH_EDITOR
	:
InputNamesMaterialX2UE{
	{TEXT("in"),        TEXT("Input")},
	{TEXT("in1"),       TEXT("A")},
	{TEXT("in2"),       TEXT("B")},
	{TEXT("fg"),        TEXT("A")},
	{TEXT("bg"),        TEXT("B")},
	{TEXT("mix"),       TEXT("Factor")},
	{TEXT("low"),       TEXT("Min")},
	{TEXT("high"),      TEXT("Max")},
	{TEXT("texcoord"),  UE::Interchange::Materials::Standard::Nodes::TextureSample::Inputs::Coordinates.ToString()} },
NodeNamesMaterialX2UE{
	{MaterialX::Category::Add,      TEXT("Add")},
	{MaterialX::Category::Sub,      TEXT("Subtract")},
	{MaterialX::Category::Multiply, TEXT("Multiply")},
	{MaterialX::Category::Sin,      TEXT("Sine")},
	{MaterialX::Category::Cos,      TEXT("Cosine")},
	{MaterialX::Category::Clamp,    TEXT("Clamp")},
	{MaterialX::Category::Mix,      TEXT("Lerp")},
	{MaterialX::Category::Max,      TEXT("Max")},
	{MaterialX::Category::Min,      TEXT("Min")},
	{MaterialX::Category::Combine2, TEXT("AppendVector")},
	{MaterialX::Category::Combine3, TEXT("AppendVector")},
	{MaterialX::Category::Combine4, TEXT("AppendVector")}},
UEInputs{ TEXT("A"), TEXT("B"), TEXT("Input"), TEXT("Factor"), TEXT("Min"), TEXT("Max") }
#endif // WITH_EDITOR
{
}

EInterchangeTranslatorType UInterchangeMaterialXTranslator::GetTranslatorType() const
{
	return EInterchangeTranslatorType::Scenes;
}

bool UInterchangeMaterialXTranslator::DoesSupportAssetType(EInterchangeTranslatorAssetType AssetType) const
{
	return AssetType == EInterchangeTranslatorAssetType::Materials;
}

TArray<FString> UInterchangeMaterialXTranslator::GetSupportedFormats() const
{
	// Call to UInterchangeMaterialXTranslator::GetSupportedFormats is not supported out of game thread
	// A more global solution must be found for translators which require some initialization
	if(!IsInGameThread())
	{
		return TArray<FString>{};
	}

	return UE::Interchange::MaterialX::IsStandardSurfacePackageLoaded() ? TArray<FString>{ TEXT("mtlx;MaterialX File Format") } : TArray<FString>{};
}

bool UInterchangeMaterialXTranslator::Translate(UInterchangeBaseNodeContainer& BaseNodeContainer) const
{
	bool bIsDocumentValid = false;
	bool bIsReferencesValid = true;

#if WITH_EDITOR
	FString Filename = GetSourceData()->GetFilename();

	if(!FPaths::FileExists(Filename))
	{
		return false;
	}
	try
	{
		mx::FileSearchPath MaterialXLibFolder{ TCHAR_TO_UTF8(*FPaths::Combine(
			FPaths::EngineDir(),
			TEXT("Binaries"),
			TEXT("ThirdParty"),
			TEXT("MaterialX"),
			TEXT("libraries"))) };

		mx::DocumentPtr MaterialXLibrary = mx::createDocument();

		mx::StringSet LoadedLibs = mx::loadLibraries({ mx::Library::Std, mx::Library::Pbr, mx::Library::Bxdf, mx::Library::Lights }, MaterialXLibFolder, MaterialXLibrary);
		if(LoadedLibs.empty())
		{
			UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
			Message->Text = FText::Format(LOCTEXT("MaterialXLibrariesNotFound", "Couldn't load MaterialX libraries from {0}"),
										  FText::FromString(MaterialXLibFolder.asString().c_str()));
			return false;
		}

		mx::DocumentPtr Document = mx::createDocument();
		mx::readFromXmlFile(Document, TCHAR_TO_UTF8(*Filename));
		Document->importLibrary(MaterialXLibrary);

		std::string MaterialXMessage;
		bIsDocumentValid = Document->validate(&MaterialXMessage);
		if(!bIsDocumentValid)
		{
			UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
			Message->Text = FText::Format(LOCTEXT("MaterialXDocumentInvalid", "{0}"),
										  FText::FromString(MaterialXMessage.c_str()));
			return false;
		}

		for(mx::ElementPtr Elem : Document->traverseTree())
		{
			//make sure to read only the current file otherwise we'll process the entire library
			if(Elem->getActiveSourceUri() != Document->getActiveSourceUri())
			{
				continue;
			}

			mx::NodePtr Node = Elem->asA<mx::Node>();

			if(Node)
			{
				std::string NameOfNodeDocument = Document->getCategory();
				std::string NameOfNode = Node->getCategory();
				printf("%s, %s", NameOfNodeDocument.c_str(), NameOfNode.c_str());

				bool bIsMaterialShader = Node->getType() == mx::MATERIAL_TYPE_STRING;
				bool bIsLightShader = Node->getType() == mx::LIGHT_SHADER_TYPE_STRING;

				if(bIsMaterialShader || bIsLightShader)
				{
					if(!Node->getTypeDef())
					{
						UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
						Message->Text = FText::Format(LOCTEXT("TypeDefNotFound", "<{0}> has no matching TypeDef"),
													  FText::FromString(Node->getName().c_str()));

						bIsReferencesValid = false;
						break;
					}

					//The entry point for materials is only on a surfacematerial node
					if(bIsMaterialShader && Node->getCategory() == mx::SURFACE_MATERIAL_NODE_STRING)
					{
						bool bHasStandardSurface = false;

						for(mx::InputPtr Input : Node->getInputs())
						{
							//we only support standard_surface for now
							mx::NodePtr ConnectedNode = Input->getConnectedNode();

							if(ConnectedNode && ConnectedNode->getCategory() == mx::Category::StandardSurface)
							{
								ProcessStandardSurface(BaseNodeContainer, ConnectedNode, Document);
								bHasStandardSurface = true;
							}
						}

						if(!bHasStandardSurface)
						{
							UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
							Message->Text = FText::Format(LOCTEXT("StandardSurfaceNotFound", "<{0}> has no standard_surface inputs"),
														  FText::FromString(Node->getName().c_str()));
						}
					}
					else if(bIsLightShader)
					{
						bIsReferencesValid = true;
						ProcessLightShader(BaseNodeContainer, Node, Document);
					}
				}
			}
		}
	}
	catch(std::exception& Exception)
	{
		bIsDocumentValid = false;
		bIsReferencesValid = false;
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = FText::Format(LOCTEXT("MaterialXException", "{0}"),
									  FText::FromString(Exception.what()));
	}

#endif // WITH_EDITOR

	if(bIsDocumentValid && bIsReferencesValid)
	{
		UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(&BaseNodeContainer);
		SourceNode->SetCustomImportUnusedMaterial(true);
	}

	return bIsDocumentValid && bIsReferencesValid;
}

TOptional<UE::Interchange::FImportImage> UInterchangeMaterialXTranslator::GetTexturePayloadData(const UInterchangeSourceData* InSourceData, const FString& PayloadKey) const
{
	UInterchangeSourceData* PayloadSourceData = UInterchangeManager::GetInterchangeManager().CreateSourceData(PayloadKey);
	FGCObjectScopeGuard ScopedSourceData(PayloadSourceData);

	if(!PayloadSourceData)
	{
		return TOptional<UE::Interchange::FImportImage>();
	}

	UInterchangeTranslatorBase* SourceTranslator = UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(PayloadSourceData);
	FGCObjectScopeGuard ScopedSourceTranslator(SourceTranslator);
	const IInterchangeTexturePayloadInterface* TextureTranslator = Cast<IInterchangeTexturePayloadInterface>(SourceTranslator);

	if(!ensure(TextureTranslator))
	{
		return TOptional<UE::Interchange::FImportImage>();
	}

	return TextureTranslator->GetTexturePayloadData(PayloadSourceData, PayloadKey);
}

#if WITH_EDITOR
void UInterchangeMaterialXTranslator::ProcessStandardSurface(UInterchangeBaseNodeContainer& NodeContainer, MaterialX::NodePtr StandardSurfaceNode, MaterialX::DocumentPtr Document) const
{
	using namespace UE::Interchange::Materials;

	const FString StandardSurfaceName{ StandardSurfaceNode->getName().c_str() };
	UInterchangeShaderGraphNode* ShaderGraphNode = NewObject<UInterchangeShaderGraphNode>(&NodeContainer);
	const FString ShaderGraphNodeUID = TEXT("\\Material\\") + StandardSurfaceName;
	ShaderGraphNode->InitializeNode(ShaderGraphNodeUID, StandardSurfaceName, EInterchangeNodeContainerType::TranslatedAsset);
	NodeContainer.AddNode(ShaderGraphNode);
	ShaderGraphNode->SetCustomShaderType(StandardSurface::Name.ToString());

	TMap<FString, UInterchangeShaderNode*> NamesToShaderNodes;
	NamesToShaderNodes.Add(StandardSurfaceName, ShaderGraphNode);

	//Base
	{
		//Weight
		{
			mx::InputPtr InputBase = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Base, Document);

			if(!ConnectNodeGraphOutputToInput(InputBase, ShaderGraphNode, StandardSurface::Parameters::Base.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputBase, StandardSurface::Parameters::Base.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Base);
			}
		}

		//Color
		{
			mx::InputPtr InputBaseColor = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::BaseColor, Document);

			if(!ConnectNodeGraphOutputToInput(InputBaseColor, ShaderGraphNode, StandardSurface::Parameters::BaseColor.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddLinearColorAttribute(InputBaseColor, StandardSurface::Parameters::BaseColor.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Color3::BaseColor);
			}
		}
	}

	//DiffuseRoughness
	{
		mx::InputPtr InputDiffuseRoughness = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::DiffuseRoughness, Document);

		if(!ConnectNodeGraphOutputToInput(InputDiffuseRoughness, ShaderGraphNode, StandardSurface::Parameters::DiffuseRoughness.ToString(), NamesToShaderNodes, NodeContainer))
		{
			AddFloatAttribute(InputDiffuseRoughness, StandardSurface::Parameters::DiffuseRoughness.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::DiffuseRoughness);
		}
	}

	//Specular
	{
		//Weight
		{
			mx::InputPtr InputSpecular = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Specular, Document);

			if(!ConnectNodeGraphOutputToInput(InputSpecular, ShaderGraphNode, StandardSurface::Parameters::Specular.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSpecular, StandardSurface::Parameters::Specular.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Specular);
			}
		}

		//Roughness
		{
			mx::InputPtr InputSpecularRoughness = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SpecularRoughness, Document);

			if(!ConnectNodeGraphOutputToInput(InputSpecularRoughness, ShaderGraphNode, StandardSurface::Parameters::SpecularRoughness.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSpecularRoughness, StandardSurface::Parameters::SpecularRoughness.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::SpecularRoughness);
			}
		}

		//IOR
		{
			mx::InputPtr InputSpecularIOR = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SpecularIOR, Document);

			if(!ConnectNodeGraphOutputToInput(InputSpecularIOR, ShaderGraphNode, StandardSurface::Parameters::SpecularIOR.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSpecularIOR, StandardSurface::Parameters::SpecularIOR.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::SpecularIOR);
			}
		}

		//Anisotropy
		{
			mx::InputPtr InputSpecularAnisotropy = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SpecularAnisotropy, Document);

			if(!ConnectNodeGraphOutputToInput(InputSpecularAnisotropy, ShaderGraphNode, StandardSurface::Parameters::SpecularAnisotropy.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSpecularAnisotropy, StandardSurface::Parameters::SpecularAnisotropy.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::SpecularAnisotropy);
			}
		}

		//Rotation
		{
			mx::InputPtr InputSpecularRotation = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SpecularRotation, Document);

			if(!ConnectNodeGraphOutputToInput(InputSpecularRotation, ShaderGraphNode, StandardSurface::Parameters::SpecularRotation.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSpecularRotation, StandardSurface::Parameters::SpecularRotation.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::SpecularRotation);
			}
		}
	}

	//Metallic
	{
		mx::InputPtr InputMetalness = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Metalness, Document);

		if(!ConnectNodeGraphOutputToInput(InputMetalness, ShaderGraphNode, StandardSurface::Parameters::Metalness.ToString(), NamesToShaderNodes, NodeContainer))
		{
			AddFloatAttribute(InputMetalness, StandardSurface::Parameters::Metalness.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Metalness);
		}
	}

	//Subsurface
	{
		//Weight
		{
			mx::InputPtr InputSubsurface = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Subsurface, Document);

			if(!ConnectNodeGraphOutputToInput(InputSubsurface, ShaderGraphNode, StandardSurface::Parameters::Subsurface.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSubsurface, StandardSurface::Parameters::Subsurface.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Subsurface);
			}
		}

		//Color
		{
			mx::InputPtr InputSubsurfaceColor = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SubsurfaceColor, Document);

			if(!ConnectNodeGraphOutputToInput(InputSubsurfaceColor, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceColor.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddLinearColorAttribute(InputSubsurfaceColor, StandardSurface::Parameters::SubsurfaceColor.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Color3::SubsurfaceColor);
			}
		}

		//Radius
		{
			mx::InputPtr InputSubsurfaceRadius = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SubsurfaceRadius, Document);

			if(!ConnectNodeGraphOutputToInput(InputSubsurfaceRadius, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceRadius.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddLinearColorAttribute(InputSubsurfaceRadius, StandardSurface::Parameters::SubsurfaceRadius.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Color3::SubsurfaceRadius);
			}
		}

		//Scale
		{
			mx::InputPtr InputSubsurfaceScale = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SubsurfaceScale, Document);

			if(!ConnectNodeGraphOutputToInput(InputSubsurfaceScale, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceScale.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSubsurfaceScale, StandardSurface::Parameters::SubsurfaceScale.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::SubsurfaceScale);
			}
		}
	}

	//Sheen
	{
		//Weight
		{
			mx::InputPtr InputSheen = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Sheen, Document);

			if(!ConnectNodeGraphOutputToInput(InputSheen, ShaderGraphNode, StandardSurface::Parameters::Sheen.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSheen, StandardSurface::Parameters::Sheen.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Sheen);
			}
		}

		//Color
		{
			mx::InputPtr InputSheenColor = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SheenColor, Document);

			if(!ConnectNodeGraphOutputToInput(InputSheenColor, ShaderGraphNode, StandardSurface::Parameters::SubsurfaceColor.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddLinearColorAttribute(InputSheenColor, StandardSurface::Parameters::SheenColor.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Color3::SheenColor);
			}
		}

		//Roughness
		{
			mx::InputPtr InputSheenRoughness = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::SheenRoughness, Document);

			if(!ConnectNodeGraphOutputToInput(InputSheenRoughness, ShaderGraphNode, StandardSurface::Parameters::SheenRoughness.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputSheenRoughness, StandardSurface::Parameters::SheenRoughness.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::SheenRoughness);
			}
		}
	}

	//Coat
	{
		//Weight
		{
			mx::InputPtr InputCoat = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Coat, Document);

			if(!ConnectNodeGraphOutputToInput(InputCoat, ShaderGraphNode, StandardSurface::Parameters::Coat.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputCoat, StandardSurface::Parameters::Coat.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Coat);
			}
		}

		//Roughness
		{
			mx::InputPtr InputCoatRoughness = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::CoatRoughness, Document);

			if(!ConnectNodeGraphOutputToInput(InputCoatRoughness, ShaderGraphNode, StandardSurface::Parameters::CoatRoughness.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputCoatRoughness, StandardSurface::Parameters::CoatRoughness.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::CoatRoughness);
			}
		}

		//Normal
		{
			//No need to take the default input if there is no Normal input
			mx::InputPtr InputCoatNormal = StandardSurfaceNode->getInput(mx::StandardSurface::Input::CoatNormal);

			if(InputCoatNormal)
			{
				ConnectNodeGraphOutputToInput(InputCoatNormal, ShaderGraphNode, StandardSurface::Parameters::CoatNormal.ToString(), NamesToShaderNodes, NodeContainer);
			}
		}
	}

	//ThinFilmThickness
	{
		mx::InputPtr InputThinFilmThickness = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::ThinFilmThickness, Document);

		if(!ConnectNodeGraphOutputToInput(InputThinFilmThickness, ShaderGraphNode, StandardSurface::Parameters::ThinFilmThickness.ToString(), NamesToShaderNodes, NodeContainer))
		{
			AddFloatAttribute(InputThinFilmThickness, StandardSurface::Parameters::ThinFilmThickness.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::ThinFilmThickness);
		}
	}

	//Emission
	{
		//Weight
		{
			mx::InputPtr InputEmission = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::Emission, Document);

			if(!ConnectNodeGraphOutputToInput(InputEmission, ShaderGraphNode, StandardSurface::Parameters::Emission.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddFloatAttribute(InputEmission, StandardSurface::Parameters::Emission.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Float::Emission);
			}
		}

		//Color
		{
			mx::InputPtr InputEmissionColor = GetStandardSurfaceInput(StandardSurfaceNode, mx::StandardSurface::Input::EmissionColor, Document);

			if(!ConnectNodeGraphOutputToInput(InputEmissionColor, ShaderGraphNode, StandardSurface::Parameters::EmissionColor.ToString(), NamesToShaderNodes, NodeContainer))
			{
				AddLinearColorAttribute(InputEmissionColor, StandardSurface::Parameters::EmissionColor.ToString(), ShaderGraphNode, mx::StandardSurface::DefaultValue::Color3::EmissionColor);
			}
		}
	}

	//Normal
	{
		//No need to take the default input if there is no Normal input
		mx::InputPtr InputNormal = StandardSurfaceNode->getInput(mx::StandardSurface::Input::Normal);

		if(InputNormal)
		{
			ConnectNodeGraphOutputToInput(InputNormal, ShaderGraphNode, StandardSurface::Parameters::Normal.ToString(), NamesToShaderNodes, NodeContainer);
		}
	}

	//Tangent
	{
		//No need to take the default input if there is no Tangent input
		mx::InputPtr InputTangent = StandardSurfaceNode->getInput(mx::StandardSurface::Input::Tangent);

		if(InputTangent)
		{
			ConnectNodeGraphOutputToInput(InputTangent, ShaderGraphNode, StandardSurface::Parameters::Tangent.ToString(), NamesToShaderNodes, NodeContainer);
		}
	}
}

void UInterchangeMaterialXTranslator::ProcessLightShader(UInterchangeBaseNodeContainer& NodeContainer, MaterialX::NodePtr LightShaderNode, MaterialX::DocumentPtr Document) const
{
	const FString FileName{ FPaths::GetBaseFilename(LightShaderNode->getActiveSourceUri().c_str()) };
	const FString LightNodeLabel = FString(LightShaderNode->getName().c_str());
	UInterchangeSceneNode* SceneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
	const FString SceneNodeUID = TEXT("\\Light\\") + FileName + TEXT("\\") + FString(LightShaderNode->getName().c_str());
	SceneNode->InitializeNode(SceneNodeUID, LightNodeLabel, EInterchangeNodeContainerType::TranslatedScene);
	NodeContainer.AddNode(SceneNode);

	UInterchangeBaseLightNode* LightNode = nullptr;
	if(LightShaderNode->getCategory() == mx::Category::PointLight)
	{
		LightNode = CreatePointLightNode(LightShaderNode, SceneNode, NodeContainer, Document);
	}
	else if(LightShaderNode->getCategory() == mx::Category::DirectionalLight)
	{
		LightNode = CreateDirectionalLightNode(LightShaderNode, SceneNode, NodeContainer, Document);
	}
	else if(LightShaderNode->getCategory() == mx::Category::SpotLight)
	{
		LightNode = CreateSpotLightNode(LightShaderNode, SceneNode, NodeContainer, Document);
	}
	else // MaterialX has no standardized lights, these 3 are the most common ones though and serves as an example in the format
	{
		LightNode = CreatePointLightNode(LightShaderNode, SceneNode, NodeContainer, Document);
	}

	const FString LightNodeUid = TEXT("\\Light\\") + LightNodeLabel;
	LightNode->InitializeNode(LightNodeUid, LightNodeLabel, EInterchangeNodeContainerType::TranslatedAsset);
	NodeContainer.AddNode(LightNode);
	const FString Uid = LightNode->GetUniqueID();
	SceneNode->SetCustomAssetInstanceUid(Uid);

	// Color
	{
		mx::InputPtr LightColor = LightShaderNode->getInput(mx::Lights::Input::Color);
		const mx::Color3 Color = mx::fromValueString<mx::Color3>(LightColor->getValueString());
		LightNode->SetCustomLightColor({ Color[0], Color[1], Color[2] });
	}

	// Intensity
	{
		mx::InputPtr LightIntensity = LightShaderNode->getInput(mx::Lights::Input::Intensity);
		LightNode->SetCustomIntensity(mx::fromValueString<float>(LightIntensity->getValueString()));
	}
}

static void GetLightDirection(mx::InputPtr DirectionInput, FTransform & Transform)
{
	mx::Vector3 Direction = mx::fromValueString<mx::Vector3>(DirectionInput->getValueString());

	//Get rotation to go from UE's default direction of directional light and the direction of the MX directional light node
	const FVector LightDirection{ Direction[2], Direction[0], Direction[1] };
	const FVector TransformDirection = Transform.GetUnitAxis(EAxis::X); // it's the default direction of a UE directional light
	Transform.SetRotation(FQuat::FindBetween(LightDirection, TransformDirection));
}

static void GetLightPosition(mx::InputPtr PositionInput, FTransform& Transform)
{
	const mx::Vector3 Position = mx::fromValueString<mx::Vector3>(PositionInput->getValueString());
	Transform.SetLocation(FVector{ Position[0] * 100, Position[1] * 100, Position[2] * 100 });
}

UInterchangeBaseLightNode* UInterchangeMaterialXTranslator::CreateDirectionalLightNode(MaterialX::NodePtr DirectionalLightShaderNode, UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer& NodeContainer, MaterialX::DocumentPtr Document) const
{
	UInterchangeDirectionalLightNode* LightNode = NewObject<UInterchangeDirectionalLightNode>(&NodeContainer);

	// Direction
	{
		mx::InputPtr DirectionInput = GetDirectionalLightInput(DirectionalLightShaderNode, mx::Lights::DirectionalLight::Input::Direction, Document);

		FTransform Transform;
		GetLightDirection(DirectionInput, Transform);

		SceneNode->SetCustomLocalTransform(&NodeContainer, Transform);
	}

	return LightNode;
}

UInterchangeBaseLightNode* UInterchangeMaterialXTranslator::CreatePointLightNode(MaterialX::NodePtr PointLightShaderNode, UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer& NodeContainer, MaterialX::DocumentPtr Document) const
{
	UInterchangePointLightNode* LightNode = NewObject<UInterchangePointLightNode>(&NodeContainer);
	LightNode->SetCustomIntensityUnits(ELightUnits::Candelas);

	// Decay rate
	{
		mx::InputPtr DecayRateInput = GetPointLightInput(PointLightShaderNode, mx::Lights::PointLight::Input::DecayRate, Document);
		const float DecayRate = mx::fromValueString<float>(DecayRateInput->getValueString());
		LightNode->SetCustomUseInverseSquaredFalloff(false);
		LightNode->SetCustomLightFalloffExponent(DecayRate);
	}

	// Position
	{
		mx::InputPtr PositionInput = GetPointLightInput(PointLightShaderNode, mx::Lights::PointLight::Input::Position, Document);
		FTransform Transform;
		GetLightPosition(PositionInput, Transform);
		SceneNode->SetCustomLocalTransform(&NodeContainer, Transform);
	}

	return LightNode;
}

UInterchangeBaseLightNode* UInterchangeMaterialXTranslator::CreateSpotLightNode(MaterialX::NodePtr SpotLightShaderNode, UInterchangeSceneNode* SceneNode, UInterchangeBaseNodeContainer& NodeContainer, MaterialX::DocumentPtr Document) const
{
	UInterchangeSpotLightNode* LightNode = NewObject<UInterchangeSpotLightNode>(&NodeContainer);

	LightNode->SetCustomIntensityUnits(ELightUnits::Candelas);

	// Decay rate
	{
		mx::InputPtr DecayRateInput = GetSpotLightInput(SpotLightShaderNode, mx::Lights::SpotLight::Input::DecayRate, Document);
		const float DecayRate = mx::fromValueString<float>(DecayRateInput->getValueString());
		LightNode->SetCustomUseInverseSquaredFalloff(false);
		LightNode->SetCustomLightFalloffExponent(DecayRate);
	}

	{
		FTransform Transform;
		// Position
		{
			mx::InputPtr PositionInput = GetSpotLightInput(SpotLightShaderNode, mx::Lights::SpotLight::Input::Position, Document);
			GetLightPosition(PositionInput, Transform);
		}

		// Direction
		{
			mx::InputPtr DirectionInput = GetSpotLightInput(SpotLightShaderNode, mx::Lights::DirectionalLight::Input::Direction, Document);
			GetLightDirection(DirectionInput, Transform);
		}
		SceneNode->SetCustomLocalTransform(&NodeContainer, Transform);
	}

	// Inner angle
	{
		mx::InputPtr InnerAngleInput = GetSpotLightInput(SpotLightShaderNode, mx::Lights::SpotLight::Input::InnerAngle, Document);
		const float InnerAngle = FMath::RadiansToDegrees(mx::fromValueString<float>(InnerAngleInput->getValueString()));
		LightNode->SetCustomInnerConeAngle(InnerAngle);
	}

	// Outer angle
	{
		mx::InputPtr OuterAngleInput = GetSpotLightInput(SpotLightShaderNode, mx::Lights::SpotLight::Input::OuterAngle, Document);
		const float OuterAngle = FMath::RadiansToDegrees(mx::fromValueString<float>(OuterAngleInput->getValueString()));
		LightNode->SetCustomOuterConeAngle(OuterAngle);
	}

	return LightNode;
}

bool UInterchangeMaterialXTranslator::ConnectNodeGraphOutputToInput(MaterialX::InputPtr InputToNodeGraph, UInterchangeShaderNode* ShaderNode, const FString& ParentInputName, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer& NodeContainer) const
{
	using namespace UE::Interchange::Materials::Standard::Nodes;

	bool bHasNodeGraph = false;

	if(InputToNodeGraph->hasNodeGraphString())
	{
		bHasNodeGraph = true;

		mx::OutputPtr Output = InputToNodeGraph->getConnectedOutput();

		if(!Output)
		{
			UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
			Message->Text = FText::Format(LOCTEXT("OutputNotFound", "Couldn't find a connected output to ({0})"),
										  FText::FromString(InputToNodeGraph->getName().c_str()));

			return false;
		}

		for(mx::Edge Edge : Output->traverseGraph())
		{
			if(mx::NodePtr UpstreamNode = Edge.getUpstreamElement()->asA<mx::Node>())
			{
				UInterchangeShaderNode* ParentShaderNode = ShaderNode;
				FString InputChannelName = ParentInputName;

				//Replace the input's name by the one used in UE
				RenameInputsNames(UpstreamNode);

				if(mx::NodePtr DownstreamNode = Edge.getDownstreamElement()->asA<mx::Node>())
				{
					if(UInterchangeShaderNode** FoundNode = NamesToShaderNodes.Find(DownstreamNode->getName().c_str()))
					{
						ParentShaderNode = *FoundNode;
					}

					if(mx::InputPtr ConnectedInput = Edge.getConnectingElement()->asA<mx::Input>())
					{
						InputChannelName = ConnectedInput->getName().c_str();
					}
				}

				if(ConnectNodeOutputToInput(UpstreamNode, ParentShaderNode, InputChannelName, NamesToShaderNodes, NodeContainer))
				{
					continue;
				}
				else if(UpstreamNode->getCategory() == mx::Category::Constant)
				{
					if(mx::InputPtr InputConstant = UpstreamNode->getInput("value"); !AddAttribute(InputConstant, InputChannelName, ParentShaderNode))
					{
						UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
						Message->Text = FText::Format(LOCTEXT("InputTypeNotSupported", "<{0}>: \"{1}\" is not supported yet"),
													  FText::FromString(InputConstant->getName().c_str()),
													  FText::FromString(InputConstant->getType().c_str()));
					}

				}
				else if(UpstreamNode->getCategory() == mx::Category::Extract)
				{
					UInterchangeShaderNode* MaskShaderNode = CreateShaderNode(UpstreamNode->getName().c_str(), Mask::Name.ToString(), ParentShaderNode, NamesToShaderNodes, NodeContainer);

					if(mx::InputPtr InputIndex = UpstreamNode->getInput("index"))
					{
						const int32 Index = mx::fromValueString<int>(InputIndex->getValueString());
						switch(Index)
						{
						case 0: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::R, true); break;
						case 1: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::G, true); break;
						case 2: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::B, true); break;
						case 3: MaskShaderNode->AddBooleanAttribute(Mask::Attributes::A, true); break;
						default:
							UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
							Message->Text = FText::FromString(TEXT("Wrong index number for extract node, values are from [0-3]"));
							break;
						}
					}

					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, MaskShaderNode->GetUniqueID());
				}
				//dot means identity, input == output
				else if(UpstreamNode->getCategory() == mx::Category::Dot || UpstreamNode->getCategory() == mx::Category::NormalMap)
				{
					if(mx::InputPtr Input = GetInputFromOldName(UpstreamNode, "in"))
					{
						Input->setName(TCHAR_TO_UTF8(*InputChannelName)); //let's take the parent node's input name
						NamesToShaderNodes.FindOrAdd(UpstreamNode->getName().c_str(), ParentShaderNode);
					}
				}
				else if(UpstreamNode->getCategory() == mx::Category::Image || UpstreamNode->getCategory() == mx::Category::TiledImage)
				{
					if(UInterchangeTextureNode* TextureNode = CreateTextureNode(UpstreamNode, NodeContainer))
					{
						//By default set the output of a texture to RGB, if the type is float then it's either up to an extract node or the Material Input to handle it
						FString OutputChannel{ TEXT("RGB") };

						if(UpstreamNode->getType() == "vector4" || UpstreamNode->getType() == "color4")
							OutputChannel = TEXT("RGBA");

						UInterchangeShaderNode* TextureShaderNode = CreateShaderNode(UpstreamNode->getName().c_str(), TextureSample::Name.ToString(), ParentShaderNode, NamesToShaderNodes, NodeContainer);
						TextureShaderNode->AddStringAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(TextureSample::Inputs::Texture.ToString()), TextureNode->GetUniqueID());
						UInterchangeShaderPortsAPI::ConnectOuputToInput(ParentShaderNode, InputChannelName, TextureShaderNode->GetUniqueID(), OutputChannel);
					}
					else
					{
						AddAttribute(UpstreamNode->getInput("default"), InputChannelName, ParentShaderNode);
					}
				}
				else if(UpstreamNode->getCategory() == mx::Category::TexCoord)
				{
					UInterchangeShaderNode* TextureCoordinateNode = CreateShaderNode(UpstreamNode->getName().c_str(), TextureCoordinate::Name.ToString(), ParentShaderNode, NamesToShaderNodes, NodeContainer);
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, TextureCoordinateNode->GetUniqueID());
				}
				else
				{
					UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
					Message->Text = FText::Format(LOCTEXT("NodeCategoryNotSupported", "<{0}> is not supported yet"),
												  FText::FromString(UpstreamNode->getCategory().c_str()));
				}
			}
		}
	}

	return bHasNodeGraph;
}

bool UInterchangeMaterialXTranslator::ConnectNodeOutputToInput(MaterialX::NodePtr Node, UInterchangeShaderNode* ParentShaderNode, const FString& InputChannelName, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer& NodeContainer) const
{
	bool bIsConnected = false;

	if(const FString* ShaderType = NodeNamesMaterialX2UE.Find(Node->getCategory().c_str()))
	{
		UInterchangeShaderNode* OperatorNode = nullptr;

		OperatorNode = CreateShaderNode(Node->getName().c_str(), *ShaderType, ParentShaderNode, NamesToShaderNodes, NodeContainer);

		for(mx::InputPtr Input : Node->getInputs())
		{
			if(Input->hasValue())
			{
				if(const FString* InputNameFound = UEInputs.Find(Input->getName().c_str()))
				{
					AddAttribute(Input, *InputNameFound, OperatorNode);
				}
			}
		}

		bIsConnected = UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(ParentShaderNode, InputChannelName, OperatorNode->GetUniqueID());
	}

	return bIsConnected;
}

UInterchangeShaderNode* UInterchangeMaterialXTranslator::CreateShaderNode(const FString& NodeName, const FString& ShaderType, UInterchangeShaderNode* ParentNode, TMap<FString, UInterchangeShaderNode*>& NamesToShaderNodes, UInterchangeBaseNodeContainer& NodeContainer) const
{
	UInterchangeShaderNode* Node;

	if(UInterchangeShaderNode** FoundNode = NamesToShaderNodes.Find(NodeName); !FoundNode)
	{
		const FString NodeUID = ParentNode->GetUniqueID() + TEXT("_") + NodeName;
		Node = NewObject<UInterchangeShaderNode>(&NodeContainer);
		Node->InitializeNode(NodeUID, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		NodeContainer.AddNode(Node);
		NodeContainer.SetNodeParentUid(NodeUID, ParentNode->GetUniqueID());
		Node->SetCustomShaderType(ShaderType);

		NamesToShaderNodes.Add(NodeName, Node);
	}
	else
	{
		Node = *FoundNode;
	}

	return Node;
}

UInterchangeTextureNode* UInterchangeMaterialXTranslator::CreateTextureNode(MaterialX::NodePtr Node, UInterchangeBaseNodeContainer& NodeContainer) const
{
	UInterchangeTextureNode* TextureNode = nullptr;

	//A node image should have an input file otherwise the user should check its default value
	if(Node)
	{
		if(mx::InputPtr InputFile = Node->getInput("file"); InputFile && InputFile->hasValue())
		{
			FString Filepath{ InputFile->getValueString().c_str() };
			const FString FilePrefix = GetFilePrefix(InputFile);
			Filepath = FPaths::Combine(FilePrefix, Filepath);
			const FString Filename = FPaths::GetCleanFilename(Filepath);
			const FString TextureNodeUID = TEXT("\\Texture\\") + Filename;

			//Only add the TextureNode once
			TextureNode = const_cast<UInterchangeTextureNode*>(Cast<UInterchangeTextureNode>(NodeContainer.GetNode(TextureNodeUID)));
			if(TextureNode == nullptr)
			{
				TextureNode = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
				TextureNode->InitializeNode(TextureNodeUID, Filename, EInterchangeNodeContainerType::TranslatedAsset);
				NodeContainer.AddNode(TextureNode);

				if(FPaths::IsRelative(Filepath))
				{
					Filepath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Node->getActiveSourceUri().c_str()), Filepath);
				}

				TextureNode->SetPayLoadKey(Filepath);

				const FString ColorSpace = GetColorSpace(InputFile);
				const bool bIsSRGB = ColorSpace == TEXT("srgb_texture");
				TextureNode->SetCustomSRGB(bIsSRGB);
			}
		}
	}

	return TextureNode;
}

const FString& UInterchangeMaterialXTranslator::GetMatchedInputName(MaterialX::InputPtr Input) const
{
	static FString Empty{ "" };

	if(Input)
	{
		if(const FString* Result = InputNamesMaterialX2UE.Find(Input->getName().c_str()))
		{
			return *Result;
		}
	}

	return Empty;
}

void UInterchangeMaterialXTranslator::RenameInputsNames(MaterialX::NodePtr Node) const
{
	if(Node)
	{
		if(const std::string& IsVisited = Node->getAttribute(mx::Attributes::IsVisited); IsVisited.empty())
		{
			Node->setAttribute(mx::Attributes::IsVisited, "true");

			for(mx::InputPtr Input : Node->getInputs())
			{
				if(const FString& Name = GetMatchedInputName(Input); !Name.IsEmpty())
				{
					Input->setAttribute(mx::Attributes::OldName, Input->getName()); // keep the old name for further processing
					Input->setName(TCHAR_TO_UTF8(*Name));
				}
			}
		}
	}
}

MaterialX::InputPtr UInterchangeMaterialXTranslator::GetInputFromOldName(MaterialX::NodePtr Node, const char* OldNameAttribute) const
{
	mx::InputPtr InputRes{ nullptr };

	for(mx::InputPtr Input : Node->getInputs())
	{
		if(Input->getAttribute(mx::Attributes::OldName) == OldNameAttribute)
		{
			InputRes = Input;
		}
	}

	return InputRes;
}

MaterialX::InputPtr UInterchangeMaterialXTranslator::GetStandardSurfaceInput(MaterialX::NodePtr StandardSurface, const char* InputName, MaterialX::DocumentPtr Document) const
{
	mx::InputPtr Input = StandardSurface->getInput(InputName);

	if(!Input)
	{
		Input = Document->getNodeDef(mx::NodeDefinition::StandardSurface)->getInput(InputName);
	}

	return Input;
}

MaterialX::InputPtr UInterchangeMaterialXTranslator::GetPointLightInput(MaterialX::NodePtr PointLight, const char* InputName, MaterialX::DocumentPtr Document) const
{
	mx::InputPtr Input = PointLight->getInput(InputName);

	if(!Input)
	{
		Input = Document->getNodeDef(mx::NodeDefinition::PointLight)->getInput(InputName);
	}

	return Input;
}

MaterialX::InputPtr UInterchangeMaterialXTranslator::GetDirectionalLightInput(MaterialX::NodePtr DirectionalLight, const char* InputName, MaterialX::DocumentPtr Document) const
{
	mx::InputPtr Input = DirectionalLight->getInput(InputName);

	if(!Input)
	{
		Input = Document->getNodeDef(mx::NodeDefinition::DirectionalLight)->getInput(InputName);
	}

	return Input;
}

MaterialX::InputPtr UInterchangeMaterialXTranslator::GetSpotLightInput(MaterialX::NodePtr SpotLight, const char* InputName, MaterialX::DocumentPtr Document) const
{
	mx::InputPtr Input = SpotLight->getInput(InputName);

	if(!Input)
	{
		Input = Document->getNodeDef(mx::NodeDefinition::SpotLight)->getInput(InputName);
	}

	return Input;
}

bool UInterchangeMaterialXTranslator::AddAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode) const
{
	if(Input)
	{
		if(Input->getType() == "float")
		{
			return ShaderNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), mx::fromValueString<float>(Input->getValueString()));
		}

		FLinearColor LinearColor;

		bool bIsColor = false;

		if(bool bIsColor3 = (Input->getType() == "color3" || Input->getType() == "vector3"))
		{
			mx::Color3 Color = mx::fromValueString<mx::Color3>(Input->getValueString());
			LinearColor = FLinearColor{ Color[0], Color[1], Color[2] };
			bIsColor = true;
		}
		else if(bool bIsColor4 = (Input->getType() == "color4" || Input->getType() == "vector4"))
		{
			mx::Color4 Color = mx::fromValueString<mx::Color4>(Input->getValueString());
			LinearColor = FLinearColor{ Color[0], Color[1], Color[2], Color[3] };
			bIsColor = true;
		}

		if(bIsColor)
		{
			return ShaderNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), LinearColor);
		}
	}

	return false;
}

bool UInterchangeMaterialXTranslator::AddFloatAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode, float DefaultValue) const
{
	if(Input)
	{
		if(Input->hasValueString())
		{
			float Value = mx::fromValueString<float>(Input->getValueString());

			if(!FMath::IsNearlyEqual(Value, DefaultValue))
			{
				return ShaderNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), Value);
			}
		}
	}

	return false;
}

bool UInterchangeMaterialXTranslator::AddLinearColorAttribute(MaterialX::InputPtr Input, const FString& InputChannelName, UInterchangeShaderNode* ShaderNode, const FLinearColor& DefaultValue) const
{
	if(Input)
	{
		if(Input->hasValueString())
		{
			mx::Color3 Color = mx::fromValueString<mx::Color3>(Input->getValueString());
			FLinearColor Value{ Color[0], Color[1], Color[2] };

			if(!Value.Equals(DefaultValue))
			{
				return ShaderNode->AddLinearColorAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(InputChannelName), Value);
			}
		}
	}

	return false;
}

FString UInterchangeMaterialXTranslator::GetFilePrefix(MaterialX::ElementPtr Element) const
{
	FString FilePrefix;

	if(Element)
	{
		if(Element->hasFilePrefix())
		{
			return FString(Element->getFilePrefix().c_str());
		}
		else
		{
			return GetFilePrefix(Element->getParent());
		}
	}

	return FilePrefix;
}

FString UInterchangeMaterialXTranslator::GetColorSpace(MaterialX::ElementPtr Element) const
{
	FString ColorSpace;

	if(Element)
	{
		if(Element->hasColorSpace())
		{
			return FString(Element->getColorSpace().c_str());
		}
		else
		{
			return GetColorSpace(Element->getParent());
		}
	}

	return ColorSpace;
}
#endif //WITH_EDITOR

#undef LOCTEXT_NAMESPACE