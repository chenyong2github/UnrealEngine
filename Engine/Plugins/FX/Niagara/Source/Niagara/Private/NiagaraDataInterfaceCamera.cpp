// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCamera.h"
#include "NiagaraTypes.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"
#include "GameFramework/PlayerController.h"

const FName UNiagaraDataInterfaceCamera::GetCameraOcclusionRectangleName(TEXT("QueryOcclusionFactorWithRectangleGPU"));
const FName UNiagaraDataInterfaceCamera::GetCameraOcclusionCircleName(TEXT("QueryOcclusionFactorWithCircleGPU"));
const FName UNiagaraDataInterfaceCamera::GetViewPropertiesName(TEXT("GetViewPropertiesGPU"));
const FName UNiagaraDataInterfaceCamera::GetClipSpaceTransformsName(TEXT("GetClipSpaceTransformsGPU"));
const FName UNiagaraDataInterfaceCamera::GetViewSpaceTransformsName(TEXT("GetViewSpaceTransformsGPU"));
const FName UNiagaraDataInterfaceCamera::GetCameraPropertiesName(TEXT("GetCameraPropertiesCPU/GPU"));
const FName UNiagaraDataInterfaceCamera::GetFieldOfViewName(TEXT("GetFieldOfView"));

UNiagaraDataInterfaceCamera::UNiagaraDataInterfaceCamera(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy = MakeShared<FNiagaraDataIntefaceProxyCameraQuery, ESPMode::ThreadSafe>();
}

void UNiagaraDataInterfaceCamera::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

bool UNiagaraDataInterfaceCamera::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	CameraDataInterface_InstanceData* PIData = new (PerInstanceData) CameraDataInterface_InstanceData;
	return true;
}

bool UNiagaraDataInterfaceCamera::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	CameraDataInterface_InstanceData* PIData = (CameraDataInterface_InstanceData*)PerInstanceData;
	if (!PIData)
	{
		return true;
	}
	
	UWorld* World = SystemInstance->GetWorldManager()->GetWorld();
	if (World && PlayerControllerIndex < World->GetNumPlayerControllers())
	{
		int32 i = 0;
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = Iterator->Get();
			if (i == PlayerControllerIndex && PlayerController)
			{
				PIData->CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
				PIData->CameraRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
				PIData->CameraFOV = PlayerController->PlayerCameraManager->GetFOVAngle();
			}
			i++;
		}
		return false;
	}
	PIData->CameraLocation = FVector::ZeroVector;
	PIData->CameraRotation = FRotator(0);
	PIData->CameraFOV = 0;
	
	return false;
}

void UNiagaraDataInterfaceCamera::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = GetCameraOcclusionRectangleName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetCameraOcclusionRectFunctionDescription", "This function returns the occlusion factor of a sprite. It samples the depth buffer in a rectangular grid around the given world position and compares each sample with the camera distance.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Center World Position")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Width World")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Height World")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Steps Per Line")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Visibility Fraction")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Fraction")));
	OutFunctions.Add(Sig);

	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetCameraOcclusionCircleName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetCameraOcclusionCircleFunctionDescription", "This function returns the occlusion factor of a sprite. It samples the depth buffer in concentric rings around the given world position and compares each sample with the camera distance.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Center World Position")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Window Diameter World")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Samples per ring")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Number of sample rings")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Visibility Fraction")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Fraction")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetViewPropertiesName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetViewPropertiesDescription", "This function returns the properties of the current view. Only valid for gpu particles.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Position World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Forward Vector")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Up Vector")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Right Vector")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("View Size And Inverse Size")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Screen To View Space")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetClipSpaceTransformsName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetClipSpaceTransformsDescription", "This function returns the clip transforms for the current view. Only valid for gpu particles.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("World To Clip Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Translated World To Clip Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To View Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Screen To World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Screen To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To Previous Clip Transform")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetViewSpaceTransformsName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetViewSpaceTransformsDescription", "This function returns the relevant transforms for the current view. Only valid for gpu particles.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Translated World To View Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("View To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Translated World To Camera View Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Camera View To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("View To Clip Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("View To ClipNoAA Transform")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetFieldOfViewName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetNiagaraFOVDescription", "This function returns the field of view angle (in degrees) for the active camera. For gpu particles this returns the x axis fov.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Field Of View Angle")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetCameraPropertiesName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetCameraPositionDescription", "This function returns the position of the currently active camera.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Camera Position World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Forward Vector World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Up Vector World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Right Vector World")));
	OutFunctions.Add(Sig);
}

bool UNiagaraDataInterfaceCamera::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsSample;
	ArgsSample.Add(TEXT("FunctionName"), InstanceFunctionName);

	if (DefinitionFunctionName == GetCameraOcclusionRectangleName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(in float3 In_SampleCenterWorldPos, in float In_SampleWindowWidthWorld, in float In_SampleWindowHeightWorld, in float In_SampleSteps, out float Out_VisibilityFraction, out float Out_SampleFraction)
			{
				float CameraDistance = length(In_SampleCenterWorldPos.xyz - View.WorldViewOrigin.xyz);
				float4 SamplePosition = float4(In_SampleCenterWorldPos + View.PreViewTranslation, 1);
				float4 ClipPosition = mul(SamplePosition, View.TranslatedWorldToClip);
				float2 ScreenPosition = ClipPosition.xy / ClipPosition.w;
				float2 ScreenUV = ScreenPosition * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;

				float Steps = In_SampleSteps <= 1 ? 0 : In_SampleSteps;
				float TotalSamples = 0;
				float OccludedSamples = 0;

				float4 SampleWidthClip = mul(float4(View.ViewRight * In_SampleWindowWidthWorld, 0) + SamplePosition, View.TranslatedWorldToClip);
				float4 SampleHeightClip = mul(float4(View.ViewUp * In_SampleWindowHeightWorld, 0) + SamplePosition, View.TranslatedWorldToClip);
				
				float2 SampleWidthUV = SampleWidthClip.xy / SampleWidthClip.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;
				float2 SampleHeightUV = SampleHeightClip.xy / SampleHeightClip.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;
				
				float SampleWidth = ScreenUV.x > 1 ? 0 : SampleWidthUV.x - ScreenUV.x;
				float SampleHeight = ScreenUV.y > 1 ? 0 : SampleHeightUV.y - ScreenUV.y;

				if (Steps > 0) 
				{
					for (int ys = 0; ys < Steps; ys++)
					{
						float SampleY = ScreenUV.y - 0.5 * SampleHeight + ys * SampleHeight / (Steps - 1);
						if (SampleY > 1 || SampleY < 0)
						{
							continue;
						}
						for (int xs = 0; xs < Steps; xs++)
						{
							float SampleX = ScreenUV.x - 0.5 * SampleWidth + xs * SampleWidth / (Steps - 1);
							if (SampleX > 1 || SampleX < 0)
							{
								continue;
							}
			
							float Depth = CalcSceneDepth(float2(SampleX, SampleY));
							if (Depth < CameraDistance) 
							{
								OccludedSamples++;
							}
							TotalSamples++;
						} 
					}
				}
				Out_VisibilityFraction = TotalSamples > 0 ? 1 - OccludedSamples / TotalSamples : 0;
				Out_SampleFraction = Steps == 0 ? 0 : (TotalSamples / (Steps * Steps));
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (DefinitionFunctionName == GetCameraOcclusionCircleName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(in float3 In_SampleCenterWorldPos, in float In_SampleWindowDiameterWorld, in float In_SampleRays, in float In_SampleStepsPerRay, out float Out_VisibilityFraction, out float Out_SampleFraction)
			{
				const float PI = 3.14159265;
				const float SPIRAL_TURN = 2 * PI * 0.61803399; // use golden ratio to rotate sample pattern each ring so we get a spiral
				float CameraDistance = length(In_SampleCenterWorldPos.xyz - View.WorldViewOrigin.xyz);
				float4 SamplePosition = float4(In_SampleCenterWorldPos + View.PreViewTranslation, 1);
				float4 ClipPosition = mul(SamplePosition, View.TranslatedWorldToClip);
				float2 ScreenPosition = ClipPosition.xy / ClipPosition.w;
				float2 ScreenUV = ScreenPosition * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;

				float Rays = In_SampleRays <= 1 ? 0 : In_SampleRays;
				float Steps = In_SampleStepsPerRay < 1 ? 0 : In_SampleStepsPerRay;
				float TotalSamples = 0;
				float OccludedSamples = 0;

				if (ScreenUV.x <= 1 && ScreenUV.x >= 0 && ScreenUV.y <= 1 && ScreenUV.y >= 0)
				{
					float Depth = CalcSceneDepth(ScreenUV);
					if (Depth < CameraDistance) 
					{
						OccludedSamples++;
					}
					TotalSamples++;
				}
				if (Steps > 0) 
				{
					float Degrees = 0;
					for (int Step = 1; Step <= Steps; Step++)
					{
						float LerpFactor = Step / Steps;
						Degrees += SPIRAL_TURN;
						for (int ray = 0; ray < Rays; ray++)
						{
							// calc ray direction vector
							float3 RayDirection = cos(Degrees) * View.ViewUp + sin(Degrees) * View.ViewRight;
							float4 RayClip = mul(float4(RayDirection * In_SampleWindowDiameterWorld / 2, 0) + SamplePosition, View.TranslatedWorldToClip);
							float2 RayUV = RayClip.xy / RayClip.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;

							if ((ScreenUV.x > 1 && RayUV.x < 0) || (ScreenUV.y > 1 && RayUV.y < 0) || (ScreenUV.x < 0 && RayUV.x > 1) || (ScreenUV.y < 0 && RayUV.y > 1))
							{
								continue;
							}
						
							float2 SampleUV = lerp(ScreenUV, RayUV, float2(LerpFactor, LerpFactor));
							
							if (SampleUV.x > 1 || SampleUV.x < 0 || SampleUV.y > 1 || SampleUV.y < 0)
							{
								continue;
							}
			
							float Depth = CalcSceneDepth(SampleUV);
							if (Depth < CameraDistance) 
							{
								OccludedSamples++;
							}
							TotalSamples++;
							Degrees += 2 * PI / Rays;
						}						
					}
				}
				Out_VisibilityFraction = TotalSamples > 0 ? 1 - OccludedSamples / TotalSamples : 0;
				Out_SampleFraction = Steps == 0 ? 0 : (TotalSamples / (Rays * Steps + 1));
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (DefinitionFunctionName == GetViewPropertiesName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_ViewPositionWorld, out float3 Out_ViewForwardVector, out float3 Out_ViewUpVector, out float3 Out_ViewRightVector, out float4 Out_ViewSizeAndInverseSize, out float4 Out_ScreenToViewSpace)
			{
				Out_ViewPositionWorld.xyz = View.WorldViewOrigin.xyz;
				Out_ViewForwardVector.xyz = View.ViewForward.xyz;
				Out_ViewUpVector.xyz = View.ViewUp.xyz;
				Out_ViewRightVector.xyz = View.ViewRight.xyz;
				Out_ViewSizeAndInverseSize = View.ViewSizeAndInvSize;
				Out_ScreenToViewSpace = View.ScreenToViewSpace;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (DefinitionFunctionName == GetFieldOfViewName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float Out_FieldOfViewAngle)
			{
				Out_FieldOfViewAngle = degrees(View.FieldOfViewWideAngles.x);
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (DefinitionFunctionName == GetClipSpaceTransformsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}out float4x4 Out_WorldToClipTransform, out float4x4 Out_TranslatedWorldToClipTransform, out float4x4 Out_ClipToWorldTransform, out float4x4 Out_ClipToViewTransform,
				out float4x4 Out_ClipToTranslatedWorldTransform, out float4x4 Out_ScreenToWorldTransform, out float4x4 Out_ScreenToTranslatedWorldTransform, out float4x4 Out_ClipToPreviousClipTransform)
			{
				Out_WorldToClipTransform = View.WorldToClip;
				Out_TranslatedWorldToClipTransform = View.TranslatedWorldToClip;
				Out_ClipToWorldTransform = View.ClipToWorld;
				Out_ClipToViewTransform = View.ClipToView;
				Out_ClipToTranslatedWorldTransform = View.ClipToTranslatedWorld;
				Out_ScreenToWorldTransform = View.ScreenToWorld;
				Out_ScreenToTranslatedWorldTransform = View.ScreenToTranslatedWorld;
				Out_ClipToPreviousClipTransform = View.ClipToPrevClip;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (DefinitionFunctionName == GetViewSpaceTransformsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float4x4 Out_TranslatedWorldToViewTransform, out float4x4 Out_ViewToTranslatedWorldTransform, out float4x4 Out_TranslatedWorldToCameraViewTransform,
				out float4x4 Out_CameraViewToTranslatedWorldTransform, out float4x4 Out_ViewToClipTransform, out float4x4 Out_ViewToClipNoAATransform)
			{
				Out_TranslatedWorldToViewTransform = View.TranslatedWorldToView;
				Out_ViewToTranslatedWorldTransform = View.ViewToTranslatedWorld;
				Out_TranslatedWorldToCameraViewTransform = View.TranslatedWorldToCameraView;
				Out_CameraViewToTranslatedWorldTransform = View.CameraViewToTranslatedWorld;
				Out_ViewToClipTransform = View.ViewToClip;
				Out_ViewToClipNoAATransform = View.ViewToClipNoAA;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (DefinitionFunctionName == GetCameraPropertiesName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_CameraPositionWorld, out float3 Out_ViewForwardVector, out float3 Out_ViewUpVector, out float3 Out_ViewRightVector)
			{				
				Out_CameraPositionWorld.xyz = View.WorldCameraOrigin.xyz;
				Out_ViewForwardVector.xyz = View.ViewForward.xyz;
				Out_ViewUpVector.xyz = View.ViewUp.xyz;
				Out_ViewRightVector.xyz = View.ViewRight.xyz;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	return false;
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraFOV);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraProperties);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, QueryOcclusionFactorGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, QueryOcclusionFactorCircleGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewPropertiesGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetClipSpaceTransformsGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewSpaceTransformsGPU);
void UNiagaraDataInterfaceCamera::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetCameraOcclusionRectangleName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, QueryOcclusionFactorGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetCameraOcclusionCircleName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, QueryOcclusionFactorCircleGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetFieldOfViewName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraFOV)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetCameraPropertiesName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraProperties)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetViewSpaceTransformsName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewSpaceTransformsGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetClipSpaceTransformsName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetClipSpaceTransformsGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetViewPropertiesName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewPropertiesGPU)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. Received Name: %s"), *BindingInfo.Name.ToString());
	}
}

void UNiagaraDataInterfaceCamera::GetCameraFOV(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFov(Context);

	float Fov = InstData.Get()->CameraFOV;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutFov.GetDestAndAdvance() = Fov;
	}
}

void UNiagaraDataInterfaceCamera::GetCameraProperties(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamPosZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamForwardX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamForwardY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamForwardZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamUpX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamUpY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamUpZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamRightX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamRightY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamRightZ(Context);

	CameraDataInterface_InstanceData* CamData = InstData.Get();
	float XPos = CamData->CameraLocation.X;
	float YPos = CamData->CameraLocation.Y;
	float ZPos = CamData->CameraLocation.Z;

	FVector Forward = CamData->CameraRotation.RotateVector(FVector::ForwardVector);
	FVector Up = CamData->CameraRotation.RotateVector(FVector::UpVector);
	FVector Right = CamData->CameraRotation.RotateVector(FVector::RightVector);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*CamPosX.GetDestAndAdvance() = XPos;
		*CamPosY.GetDestAndAdvance() = YPos;
		*CamPosZ.GetDestAndAdvance() = ZPos;

		*CamForwardX.GetDestAndAdvance() = Forward.X;
		*CamForwardY.GetDestAndAdvance() = Forward.Y;
		*CamForwardZ.GetDestAndAdvance() = Forward.Z;

		*CamUpX.GetDestAndAdvance() = Up.X;
		*CamUpY.GetDestAndAdvance() = Up.Y;
		*CamUpZ.GetDestAndAdvance() = Up.Z;

		*CamRightX.GetDestAndAdvance() = Right.X;
		*CamRightY.GetDestAndAdvance() = Right.Y;
		*CamRightZ.GetDestAndAdvance() = Right.Z;
	}
}

// ------- Dummy implementations for CPU execution ------------

void UNiagaraDataInterfaceCamera::QueryOcclusionFactorGPU(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> PosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamZ(Context);
	VectorVM::FExternalFuncInputHandler<float> WidthParam(Context);
	VectorVM::FExternalFuncInputHandler<float> HeightParam(Context);
	VectorVM::FExternalFuncInputHandler<float> StepsParam(Context);

	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutOcclusion(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFactor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		PosParamX.GetAndAdvance();
		PosParamY.GetAndAdvance();
		PosParamZ.GetAndAdvance();
		WidthParam.GetAndAdvance();
		HeightParam.GetAndAdvance();
		StepsParam.GetAndAdvance();

		*OutOcclusion.GetDestAndAdvance() = 0;
		*OutFactor.GetDestAndAdvance() = 0;
	}
}

void UNiagaraDataInterfaceCamera::QueryOcclusionFactorCircleGPU(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> PosParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> PosParamZ(Context);
	VectorVM::FExternalFuncInputHandler<float> RadiusParam(Context);
	VectorVM::FExternalFuncInputHandler<float> RaysParam(Context);
	VectorVM::FExternalFuncInputHandler<float> StepsParam(Context);

	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> OutOcclusion(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFactor(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		PosParamX.GetAndAdvance();
		PosParamY.GetAndAdvance();
		PosParamZ.GetAndAdvance();
		RadiusParam.GetAndAdvance();
		RaysParam.GetAndAdvance();
		StepsParam.GetAndAdvance();

		*OutOcclusion.GetDestAndAdvance() = 0;
		*OutFactor.GetDestAndAdvance() = 0;
	}
}

void UNiagaraDataInterfaceCamera::GetViewPropertiesGPU(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);
	TArray<VectorVM::FExternalFuncRegisterHandler<float>> OutParams;
	OutParams.Reserve(20);
	for (int i = 0; i < 20; i++)
	{
		OutParams.Emplace(Context);
	}

	for (int32 k = 0; k < Context.NumInstances; ++k)
	{
		for (int i = 0; i < 20; i++)
		{
			*OutParams[i].GetDestAndAdvance() = 0;
		}
	}
}

void UNiagaraDataInterfaceCamera::GetClipSpaceTransformsGPU(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);
	TArray<VectorVM::FExternalFuncRegisterHandler<float>> OutParams;
	OutParams.Reserve(128);
	for (int i = 0; i < 128; i++)
	{
		OutParams.Emplace(Context);
	}

	for (int32 k = 0; k < Context.NumInstances; ++k)
	{
		for (int i = 0; i < 128; i++)
		{
			*OutParams[i].GetDestAndAdvance() = 0;
		}
	}
}

void UNiagaraDataInterfaceCamera::GetViewSpaceTransformsGPU(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<CameraDataInterface_InstanceData> InstData(Context);
	TArray<VectorVM::FExternalFuncRegisterHandler<float>> OutParams;
	OutParams.Reserve(96);
	for (int i = 0; i < 96; i++)
	{
		OutParams.Emplace(Context);
	}

	for (int32 k = 0; k < Context.NumInstances; ++k)
	{
		for (int i = 0; i < 96; i++)
		{
			*OutParams[i].GetDestAndAdvance() = 0;
		}
	}
}

// ------------------------------------------------------------

struct FNiagaraDataInterfaceParametersCS_CameraQuery : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		PassUniformBuffer.Bind(ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << PassUniformBuffer;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread());
		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();

		TUniformBufferRef<FSceneTexturesUniformParameters> SceneTextureUniformParams = GNiagaraViewDataManager.GetSceneTextureUniformParameters();
		SetUniformBufferParameter(RHICmdList, ComputeShaderRHI, PassUniformBuffer, SceneTextureUniformParams);
	}

private:

	/** The SceneDepthTexture parameter for depth buffer query. */
	FShaderUniformBufferParameter PassUniformBuffer;
};

FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceCamera::ConstructComputeParameters() const
{
	return new FNiagaraDataInterfaceParametersCS_CameraQuery();
}

ETickingGroup UNiagaraDataInterfaceCamera::CalculateTickGroup(void * PerInstanceData) const
{
	return ETickingGroup::TG_PostUpdateWork;
}
