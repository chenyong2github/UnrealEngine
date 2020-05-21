// Copyright Epic Games, Inc. All Rights Reserved.

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Rhino.PlugIns;

// Plug-in Description Attributes - all of these are optional.
// These will show in Rhino's option dialog, in the tab Plug-ins.
[assembly: PlugInDescription(DescriptionType.Address, "-")]
[assembly: PlugInDescription(DescriptionType.Country, "-")]
[assembly: PlugInDescription(DescriptionType.Email, "-")]
[assembly: PlugInDescription(DescriptionType.Phone, "-")]
[assembly: PlugInDescription(DescriptionType.Fax, "-")]
[assembly: PlugInDescription(DescriptionType.Organization, "Epic Games, Inc.")]
[assembly: PlugInDescription(DescriptionType.UpdateUrl, "-")]
[assembly: PlugInDescription(DescriptionType.WebSite, "-")]


#if !RHINO_VERSION_5
// Icons should be Windows .ico files and contain 32-bit images in the following sizes: 16, 24, 32, 48, and 256.
// This is a Rhino 6-only description.
[assembly: PlugInDescription(DescriptionType.Icon, "DatasmithRhino6.EmbeddedResources.UnrealEngine.ico")]
#endif

// General Information about an assembly is controlled through the following 
// set of attributes. Change these attribute values to modify the information
// associated with an assembly.
#if RHINO_VERSION_5
[assembly: AssemblyTitle("DatasmithRhino5")]
#elif RHINO_VERSION_6
[assembly: AssemblyTitle("DatasmithRhino6")]
#endif

// This will be used also for the plug-in description.
[assembly: AssemblyDescription("Export a Rhino 3D View to Unreal Datasmith")]

[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("Epic Games, Inc.")]
#if RHINO_VERSION_5
[assembly: AssemblyProduct("DatasmithRhinoExporter Add-In for Rhino 5")]
#elif RHINO_VERSION_6
[assembly: AssemblyProduct("DatasmithRhinoExporter Add-In for Rhino 6")]
#endif
[assembly: AssemblyCopyright("Copyright Epic Games, Inc. All Rights Reserved.")]
[assembly: AssemblyTrademark("")]
[assembly: AssemblyCulture("")]

// Setting ComVisible to false makes the types in this assembly not visible 
// to COM components.  If you need to access a type in this assembly from 
// COM, set the ComVisible attribute to true on that type.
[assembly: ComVisible(false)]

// The following GUID is for the ID of the typelib if this project is exposed to COM
#if RHINO_VERSION_5
[assembly: Guid("fb9a4610-a322-4e7f-8eed-9e29e01d12d8")] // This will also be the Guid of the Rhino plug-in
#elif RHINO_VERSION_6
[assembly: Guid("d1fdc795-b334-4933-b680-088119cdc6bb")] // This will also be the Guid of the Rhino plug-in
#endif

// Version information for an assembly consists of the following four values:
//
//      Major Version
//      Minor Version 
//      Build Number
//      Revision
//
// You can specify all the values or you can default the Build and Revision Numbers 
// by using the '*' as shown below:
// [assembly: AssemblyVersion("1.0.*")]

[assembly: AssemblyVersion("4.26.0.0")]
[assembly: AssemblyFileVersion("4.26.0.0")]

// Make compatible with Rhino Installer Engine
[assembly: AssemblyInformationalVersion("2")]
