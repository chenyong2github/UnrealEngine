// Tips for Getting Started:
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file

#ifndef PCH_H
#define PCH_H


// DatasmithSDK project can be built from the main solution, we check that datasmith headers are accessible
#if !__has_include("DatasmithCore.h")
#error Datasmith SDK is not accessible. Make sure the SDK has been built from the main solution, and verify that DatasmithSDK.props correctly points to the SDK.
#endif


#endif //PCH_H
