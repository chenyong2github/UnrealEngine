// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIONativeConfiguration.h"

#include "ColorSpace.h"
#include "OpenColorIOConfiguration.h"


#if WITH_OCIO
OCIO_NAMESPACE::ConstConfigRcPtr FOpenColorIONativeConfiguration::Get() const
{
	return Config;
}

void FOpenColorIONativeConfiguration::Set(OCIO_NAMESPACE::ConstConfigRcPtr InConfig)
{
	using namespace OCIO_NAMESPACE;

	if (InConfig == nullptr)
	{
		Config.reset();
	}
	else
	{
		ConstColorSpaceRcPtr InterchangeCS = InConfig->getColorSpace(InConfig->getCanonicalName(OpenColorIOInterchangeName));

		// When the aces interchange color space is present, we add the working color space as an additional option.
		if (InterchangeCS != nullptr && InConfig->getColorSpace(StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get()) == nullptr)
		{
			ColorSpaceRcPtr WorkingCS = InterchangeCS->createEditableCopy();
			WorkingCS->setName(StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get());
			WorkingCS->setFamily("UE");
			WorkingCS->clearAliases();

			ConfigRcPtr ConfigCopy = InConfig->createEditableCopy();
			ConfigCopy->addColorSpace(WorkingCS);

			Config = ConfigCopy;
		}
		else
		{
			Config = InConfig;
		}
	}
}

FOpenColorIONativeInterchangeConfiguration::FOpenColorIONativeInterchangeConfiguration()
{
	using namespace OCIO_NAMESPACE;
	using namespace UE::Color;

	ColorSpaceRcPtr AP0 = ColorSpace::Create();
	AP0->setName("ACES2065-1");
	AP0->setBitDepth(BIT_DEPTH_F32);
	AP0->setEncoding("scene-linear");

	ColorSpaceRcPtr WCS = ColorSpace::Create();
	WCS->setName(StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get());
	WCS->setBitDepth(BIT_DEPTH_F32);
	WCS->setEncoding("scene-linear");

	const FMatrix44d TransformMat = Transpose<double>(FColorSpaceTransform(FColorSpace::GetWorking(), FColorSpace(EColorSpace::ACESAP0)));
	MatrixTransformRcPtr MatrixTransform = MatrixTransform::Create();
	MatrixTransform->setMatrix(&TransformMat.M[0][0]);
	WCS->setTransform(MatrixTransform, COLORSPACE_DIR_TO_REFERENCE);

	Config = OCIO_NAMESPACE::Config::Create();
	Config->addColorSpace(AP0);
	Config->addColorSpace(WCS);
	Config->setRole("aces_interchange", "ACES2065-1");
}

OCIO_NAMESPACE::ConstConfigRcPtr FOpenColorIONativeInterchangeConfiguration::Get() const
{
	return Config;
}
#endif
