// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TransformTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FTransformTypesSpec,
	"GeometryProcessing.Unit",
	EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
END_DEFINE_SPEC(FTransformTypesSpec)


FVector MakeRandomVector()
{
	FVector Temp(FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f));
	Temp.Normalize();
	return Temp;
}

void FTransformTypesSpec::Define()
{
	Describe("FTransform3<float>", [this]()
	{
		It("FTransform_Comparisons", [this]()
		{
			FMath::RandInit(313377);
			for (int k = 0; k < 1000; ++k)
			{
				FQuat Rotation(MakeRandomVector(), FMath::RandRange(-100.0f, 100.0f));
				FVector Translation = MakeRandomVector();
				FVector Scale = FVector(1.1f,1.1f,1.1f) + MakeRandomVector();		// need to stay away from zero scale or floats will explode and precision is too low
				FTransform UETransform(Rotation, Translation, Scale);
				FTransform3f GPTransform(UETransform);

				float tol = 0.001f;		// float precision is very bad...some float comparisons will fail at 0.0001f
				for (int j = 0; j < 1000; ++j)
				{
					FVector TestVector = MakeRandomVector();

					FVector UETransformP = UETransform.TransformPosition(TestVector);
					FVector GPTransformP = (FVector)GPTransform.TransformPosition((FVector3f)TestVector);
					TestEqual("TransformPosition", UETransformP, GPTransformP, tol);

					FVector UETransformV = UETransform.TransformVector(TestVector);
					FVector GPTransformV = (FVector)GPTransform.TransformVector((FVector3f)TestVector);
					TestEqual("TransformVector", UETransformV, GPTransformV, tol);

					FVector UETransformNS = UETransform.TransformVectorNoScale(TestVector);
					FVector GPTransformNS = (FVector)GPTransform.TransformVectorNoScale((FVector3f)TestVector);
					TestEqual("TransformVectorNoScale", UETransformNS, GPTransformNS, tol);

					FVector UEInverseP = UETransform.InverseTransformPosition(TestVector);
					FVector GPInverseP = (FVector)GPTransform.InverseTransformPosition((FVector3f)TestVector);
					TestEqual("InverseTransformPosition", UEInverseP, GPInverseP, tol);

					FVector UEInverseV = UETransform.InverseTransformVector(TestVector);
					FVector GPInverseV = (FVector)GPTransform.InverseTransformVector((FVector3f)TestVector);
					TestEqual("InverseTransformVector", UEInverseV, GPInverseV, tol);

					FVector UEInverseNS = UETransform.InverseTransformVectorNoScale(TestVector);
					FVector GPInverseNS = (FVector)GPTransform.InverseTransformVectorNoScale((FVector3f)TestVector);
					TestEqual("InverseTransformVectorNoScale", UEInverseNS, GPInverseNS, tol);
				}
			}
		});
	});
}
#endif
