# crunchersharp
Program analyses debugger information file (PDB, so Microsoft Visual C++ only) and presents info about user defined structures (size, padding, etc). 

Original blog post: http://msinilo.pl/blog/?p=425
# Getting Started

### Windows 10 Visual Studio 2019

1. Open command prompt or PowerShell **as Admin** 
3. Find the directory where your `msdia` dll is located, which by default is loacted with you Visual Studio installation:

  ```
  cd C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE
  ```

4. Register the `msdia140` dll manually: 

  ```
  regsvr32 msdia140.dll
  ```

5. Build the MSVC Project that you want with **full** debug symbols. By default in VS 2019/2017 it is set to 
use `/debug:fast`, we need `/debug:full`. This setting is located in Visual Studio project settings at `Linker > Debugging > Generate Debug Info`.
 * To change this in UE projects you will need set the Unreal Build Tool flag called `bUseFastPDBLinking`.
  Currently (4.24) the default value is to be turned off, which is what you want for using Cruncher. 

6. Open up Cruncher in Visual Studio
7. Right click on the C# Project and select "Properties"
8. Go to "Debug" and Select `Enable native code debugging` at the bottom. 
  * Make sure you have "All Configurations" selected here to ensure you have it in both Release and Debug
9. Create an `x86` build configuration by clicking the Solution Platforms dropdown and selecting `Configuration Manager...` 
   then clicking on the `Active Solution Platform` and creating a new `x86` setting.
  * This has to be x86 due to the `msdia140` DLL, if you don't do this you may get unresolved symbols in Release modes. 


![Screenshot](http://msinilo.pl/blog2/images/Crunchingbytes_118E2/cruncher.jpg "Example screenshot")