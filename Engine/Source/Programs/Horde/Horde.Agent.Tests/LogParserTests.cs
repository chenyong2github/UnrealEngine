// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeAgent.Parser;
using HordeAgent.Utility;
using HordeCommon;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestPlatform.Common.Utilities;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using NuGet.Frameworks;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Dynamic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace HordeAgentTests
{
	[TestClass]
	public class LogParserTests
	{
		const string LogLine = "LogLine";

		class LoggerCapture : ILogger
		{
			int LogLineIndex;

			public List<LogEvent> Events = new List<LogEvent>();

			public IDisposable? BeginScope<TState>(TState State) => null;

			public bool IsEnabled(LogLevel LogLevel) => true;

			public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception Exception, Func<TState, Exception?, string> Formatter)
			{
				if (State is LogEvent Event)
				{
					Event.Properties ??= new Dictionary<string, object>();
					Event.Properties.Add(LogLine, LogLineIndex);
					Events.Add(Event);
				}
				else if(LogLevel != LogLevel.Information || EventId.Id != 0)
				{
					Events.Add(LogEvent.FromState(LogLevel, EventId, State, Exception, Formatter));
				}
				LogLineIndex++;
			}
		}

		[TestMethod]
		public void StructuredOutputMatcher()
		{
			string[] Lines =
			{
				"{\"Timestamp\":\"2021-10-28T09:14:48.8561456-04:00\",\"Level\":\"Information\",\"MessageTemplate\":\"Hello {World}\",\"RenderedMessage\":\"Hello 123\",\"Properties\":{\"World\":123,\"EventId\":{\"Id\":123},\"SourceContext\":\"HordeAgent\",\"dd.env\":\"default\",\"dd.service\":\"hordeagent\",\"dd.version\":\"1.0.0\"}}",
				"{\"Timestamp\":\"2021-10-28T05:36:08\",\"Level\":\"Information\",\"RenderedMessage\":\"Building 43 projects (see Log \u0027Engine/Programs/AutomationTool/Saved/Logs/Log.txt\u0027 for more details)\"}",
				"{\"Timestamp\":\"2021-10-28T05:36:08\",\"Level\":\"Warning\",\"RenderedMessage\":\" Restore...\"}",
				"{\"Timestamp\":\"2021-10-28T05:36:15\",\"Level\":\"Error\",\"RenderedMessage\":\" Build...\"}",
			};

			List<LogEvent> Events = Parse(Lines);
			Assert.AreEqual(3, Events.Count);

			int Idx = 0;

			LogEvent Event = Events[Idx++];
			Assert.AreEqual(LogLevel.Information, Event.Level);
			Assert.AreEqual(new EventId(123), Event.Id);
			Assert.AreEqual("Hello 123", Event.Message);

			Event = Events[Idx++];
			Assert.AreEqual(LogLevel.Warning, Event.Level);
			Assert.AreEqual(new EventId(0), Event.Id);
			Assert.AreEqual(" Restore...", Event.Message);

			Event = Events[Idx++];
			Assert.AreEqual(LogLevel.Error, Event.Level);
			Assert.AreEqual(new EventId(0), Event.Id);
			Assert.AreEqual(" Build...", Event.Message);
		}

		[TestMethod]
		public void ExitCodeEventMatcher()
		{
			string[] Lines =
			{
				@"Took 620.820352s to run UE4Editor-Cmd.exe, ExitCode=777003",
				@"Editor terminated with exit code 777003 while running GenerateSkinSwapDetections for D:\Build\++UE5\Sync\FortniteGame\FortniteGame.uproject; see log D:\Build\++UE5\Sync\Engine\Programs\AutomationTool\Saved\Logs\GenerateSkinSwapDetections-2020.08.18-21.47.07.txt",
				@"AutomationTool exiting with ExitCode=1 (Error_Unknown)",
				@"BUILD FAILED"
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 1, 3, LogLevel.Error, KnownLogEvents.Generic);
		}

		[TestMethod]
		public void CrashEventMatcher()
		{
			string[] Lines =
			{
				@"   LogOutputDevice: Error: begin: stack for UAT",
				@"   LogOutputDevice: Error: === Handled ensure: ===",
				@"   LogOutputDevice: Error:",
				@"   LogOutputDevice: Error: Ensure condition failed: Foo [File:D:/build/++UE5/Sync/Foo.cpp] [Line: 233]",
				@"   LogOutputDevice: Error:",
				@"   LogOutputDevice: Error: Stack:",
				@"   LogOutputDevice: Error: [Callstack] 0x00007ff6b68a035d UnrealEditor-Cmd.exe!GuardedMain() [D:\build\++UE5\Sync\Engine\Source\Runtime\Launch\Private\Launch.cpp:129]",
				@"   LogOutputDevice: Error: [Callstack] 0x00007ff6b68a05fa UnrealEditor-Cmd.exe!GuardedMainWrapper() [D:\build\++UE5\Sync\Engine\Source\Runtime\Launch\Private\Windows\LaunchWindows.cpp:142]",
				@"   LogOutputDevice: Error: [Callstack] 0x00007ff6b68b522d UnrealEditor-Cmd.exe!WinMain() [D:\build\++UE5\Sync\Engine\Source\Runtime\Launch\Private\Windows\LaunchWindows.cpp:273]",
				@"   LogOutputDevice: Error: [Callstack] 0x00007ff6b68b7522 UnrealEditor-Cmd.exe!__scrt_common_main_seh() [D:\a01\_work\9\s\src\vctools\crt\vcstartup\src\startup\exe_common.inl:288]",
				@"   LogOutputDevice: Error: [Callstack] 0x00007ff96a517974 KERNEL32.DLL!UnknownFunction []",
				@"   LogOutputDevice: Error: [Callstack] 0x00007ff96d14a271 ntdll.dll!UnknownFunction []",
				@"   LogOutputDevice: Error:",
				@"   LogOutputDevice: Error: end: stack for UAT"
			};

			{
				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				CheckEventGroup(Events, 0, 14, LogLevel.Error, KnownLogEvents.Engine_Crash);
			}

			{
				List<LogEvent> Events = Parse(String.Join("\n", Lines).Replace("Error:", "Warning:"));
				CheckEventGroup(Events, 0, 14, LogLevel.Warning, KnownLogEvents.Engine_Crash);
			}
		}

		[TestMethod]
		public void CrashEventMatcher2()
		{
			string[] Lines =
			{
				@"Took 620.820352s to run UE4Editor-Cmd.exe, ExitCode=3",
				@"Took 620.820352s to run UE4Editor-Cmd.exe, ExitCode=30",
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.AutomationTool_CrashExitCode);
		}

		[TestMethod]
		public void CompileEventMatcher()
		{
			// Visual C++ error
			{
				string[] Lines =
				{
					@"  C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
					@"          with",
					@"          [",
					@"              UserClass=AFortVehicleManager",
					@"          ]",
					@"  C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp(78): note: Types pointed to are unrelated; conversion requires reinterpret_cast, C-style cast or function-style cast",
					@"  C:\Horde\Sync\Engine\Source\Runtime\Core\Public\Delegates/DelegateSignatureImpl.inl(871): note: see declaration of 'TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject'",
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				CheckEventGroup(Events, 0, 7, LogLevel.Error, KnownLogEvents.Compiler);

				Assert.AreEqual("C2664", Events[0].Properties!["code"].ToString());
				Assert.AreEqual("78", Events[0].Properties!["line"].ToString());

				LogValue FileProperty = (LogValue)Events[0].Properties!["file"];
				Assert.AreEqual(@"C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp", FileProperty.Text);
				Assert.AreEqual(@"SourceFile", FileProperty.Type);
				Assert.AreEqual(@"Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp", FileProperty.Properties!["relativePath"].ToString());
				Assert.AreEqual(@"//UE4/Main/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp@12345", FileProperty.Properties["depotPath"].ToString());

				LogValue NoteProperty1 = (LogValue)Events[5].Properties!["file"];
				Assert.AreEqual(@"C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp", NoteProperty1.Text);
				Assert.AreEqual(@"SourceFile", NoteProperty1.Type);

				LogValue NoteProperty2 = (LogValue)Events[6].Properties!["file"];
				Assert.AreEqual(@"C:\Horde\Sync\Engine\Source\Runtime\Core\Public\Delegates/DelegateSignatureImpl.inl", NoteProperty2.Text);

				// FIXME: Fails on Linux. Properties dict is empty
				//Assert.AreEqual(@"SourceFile", NoteProperty2.Properties["type"].ToString());
			}
		}

		[TestMethod]
		public void SymbolStripSpuriousEventMatcher()
		{
			// Symbol stripping error
			{
				string[] Lines =
				{
					@"Stripping symbols: d:\build\++UE5\Sync\Engine\Plugins\Runtime\GoogleVR\GoogleVRController\Binaries\Win64\UnrealEditor-GoogleVRController.pdb -> d:\build\++UE5\Sync\ArchiveForUGS\Staging\Engine\Plugins\Runtime\GoogleVR\GoogleVRController\Binaries\Win64\UnrealEditor-GoogleVRController.pdb",
					@"ERROR: Error: EC_OK -- ??",
					@"ERROR:",
					@"Stripping symbols: d:\build\++UE5\Sync\Engine\Plugins\Runtime\GoogleVR\GoogleVRHMD\Binaries\Win64\UnrealEditor-GoogleVRHMD.pdb -> d:\build\++UE5\Sync\ArchiveForUGS\Staging\Engine\Plugins\Runtime\GoogleVR\GoogleVRHMD\Binaries\Win64\UnrealEditor-GoogleVRHMD.pdb",
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				CheckEventGroup(Events, 1, 1, LogLevel.Information, KnownLogEvents.Systemic_PdbUtil);
			}
		}

		[TestMethod]
		public void CsCompileEventMatcher()
		{
			// C# compile error
			{
				string[] Lines =
				{
					@"  GenerateSigningRequestDialog.cs(22,7): error CS0246: The type or namespace name 'Org' could not be found (are you missing a using directive or an assembly reference?) [c:\Horde\Engine\Source\Programs\IOS\iPhonePackager\iPhonePackager.csproj]",
					@"  Utilities.cs(16,7): error CS0246: The type or namespace name 'Org' could not be found (are you missing a using directive or an assembly reference?) [c:\Horde\Engine\Source\Programs\IOS\iPhonePackager\iPhonePackager.csproj]"
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				Assert.AreEqual(2, Events.Count);

				CheckEventGroup(Events.Slice(0, 1), 0, 1, LogLevel.Error, KnownLogEvents.Compiler);
				Assert.AreEqual("CS0246", Events[0].Properties!["code"].ToString());
				Assert.AreEqual("22", Events[0].Properties!["line"].ToString());

				LogValue FileProperty1 = (LogValue)Events[0].Properties!["file"];
				Assert.AreEqual(@"GenerateSigningRequestDialog.cs", FileProperty1.Text);
				Assert.AreEqual(@"SourceFile", FileProperty1.Type);
				Assert.AreEqual(@"Engine/Source/Programs/IOS/iPhonePackager/GenerateSigningRequestDialog.cs", FileProperty1.Properties!["relativePath"].ToString());
				Assert.AreEqual(@"//UE4/Main/Engine/Source/Programs/IOS/iPhonePackager/GenerateSigningRequestDialog.cs@12345", FileProperty1.Properties["depotPath"].ToString());

				CheckEventGroup(Events.Slice(1, 1), 1, 1, LogLevel.Error, KnownLogEvents.Compiler);
				Assert.AreEqual("CS0246", Events[1].Properties!["code"].ToString());
				Assert.AreEqual("16", Events[1].Properties!["line"].ToString());

				LogValue FileProperty2 = (LogValue)Events[1].Properties!["file"];
				Assert.AreEqual(@"Utilities.cs", FileProperty2.Text);
				Assert.AreEqual(@"SourceFile", FileProperty2.Type);
				Assert.AreEqual(@"Engine/Source/Programs/IOS/iPhonePackager/Utilities.cs", FileProperty2.Properties!["relativePath"].ToString());
				Assert.AreEqual(@"//UE4/Main/Engine/Source/Programs/IOS/iPhonePackager/Utilities.cs@12345", FileProperty2.Properties["depotPath"].ToString());
			}
		}

		[TestMethod]
		public void CsCompileEventMatcher2()
		{
			// C# compile error
			{
				string[] Lines =
				{
					@"  Configuration\TargetRules.cs(1497,58): warning CS8625: Cannot convert null literal to non-nullable reference type. [C:\Horde\Engine\Source\Programs\UnrealBuildTool\UnrealBuildTool.csproj]",
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				CheckEventGroup(Events, 0, 1, LogLevel.Warning, KnownLogEvents.Compiler);

				Assert.AreEqual("CS8625", Events[0].Properties!["code"].ToString());
				Assert.AreEqual("1497", Events[0].Properties!["line"].ToString());

				LogValue FileProperty = (LogValue)Events[0].Properties!["file"];
				Assert.AreEqual(@"Configuration\TargetRules.cs", FileProperty.Text);
				Assert.AreEqual(@"SourceFile", FileProperty.Type);
				Assert.AreEqual(@"Engine/Source/Programs/UnrealBuildTool/Configuration/TargetRules.cs", FileProperty.Properties!["relativePath"].ToString());
				Assert.AreEqual(@"//UE4/Main/Engine/Source/Programs/UnrealBuildTool/Configuration/TargetRules.cs@12345", FileProperty.Properties!["depotPath"].ToString());
			}
		}

		[TestMethod]
		public void CsCompileEventMatcher3()
		{
			// C# compile error from UBT
			{
				string[] Lines =
				{
					@"  ERROR: c:\Horde\Engine\Source\Runtime\CoreOnline\CoreOnline.Build.cs(4,7): error CS0246: The type or namespace name 'Tools' could not be found (are you missing a using directive or an assembly reference?)",
					@"  WARNING: C:\horde\Engine\Source\Runtime\CoreOnline\CoreOnline.Build.cs(4,7): warning CS0246: The type or namespace name 'Tools' could not be found (are you missing a using directive or an assembly reference?)"
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				Assert.AreEqual(2, Events.Count);

				CheckEventGroup(Events.Slice(0, 1), 0, 1, LogLevel.Error, KnownLogEvents.Compiler);
				Assert.AreEqual("CS0246", Events[0].Properties!["code"].ToString());
				Assert.AreEqual("4", Events[0].Properties!["line"].ToString());

				LogValue FileProperty = (LogValue)Events[0].Properties!["file"];
				Assert.AreEqual(@"c:\Horde\Engine\Source\Runtime\CoreOnline\CoreOnline.Build.cs", FileProperty.Text);
				Assert.AreEqual(@"SourceFile", FileProperty.Type);
				Assert.AreEqual(@"Engine/Source/Runtime/CoreOnline/CoreOnline.Build.cs", FileProperty.Properties!["relativePath"].ToString());
				Assert.AreEqual(@"//UE4/Main/Engine/Source/Runtime/CoreOnline/CoreOnline.Build.cs@12345", FileProperty.Properties!["depotPath"].ToString());

				CheckEventGroup(Events.Slice(1, 1), 1, 1, LogLevel.Warning, KnownLogEvents.Compiler);
				Assert.AreEqual("CS0246", Events[1].Properties!["code"].ToString());
				Assert.AreEqual("4", Events[1].Properties!["line"].ToString());

				LogValue FileProperty1 = (LogValue)Events[1].Properties!["file"];
				Assert.AreEqual(@"C:\horde\Engine\Source\Runtime\CoreOnline\CoreOnline.Build.cs", FileProperty1.Text);
				Assert.AreEqual(@"SourceFile", FileProperty1.Type);
				Assert.AreEqual(@"Engine/Source/Runtime/CoreOnline/CoreOnline.Build.cs", FileProperty1.Properties!["relativePath"].ToString());
				Assert.AreEqual(@"//UE4/Main/Engine/Source/Runtime/CoreOnline/CoreOnline.Build.cs@12345", FileProperty1.Properties!["depotPath"].ToString());
			}
		}

		[TestMethod]
		public void HttpEventMatcher()
		{
			string[] Lines =
			{
				@"WARNING: Failed to resolve binaries for artifact fe1b277b-7751-4a52-8059-ec3f943811de:xsx with error: fe1b277b-7751-4a52-8059-ec3f943811de:xsx Failed.Unexpected error retrieving response.BaseUrl = https://content-service-latest-gamedev.cdae.dev.use1a.on.epicgames.com/api. Status = Timeout. McpConfig = ValkyrieDevLatest."
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			CheckEventGroup(Events, 0, 1, LogLevel.Warning, KnownLogEvents.Generic);
		}

		[TestMethod]
		public void MicrosoftEventMatcher()
		{
			// Generic Microsoft errors which can be parsed by visual studio
			{
				string[] Lines =
				{
					@" C:\Horde\Foo\Bar.txt(20): warning TL2012: Some error message",
					@" C:\Horde\Foo\Bar.txt(20, 30) : warning TL2034: Some error message",
					@" CSC : error CS2012: Cannot open 'D:\Build\++UE4\Sync\Engine\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter\Resources\obj\Release\DatasmithRevitResources.dll' for writing -- 'The process cannot access the file 'D:\Build\++UE4\Sync\Engine\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter\Resources\obj\Release\DatasmithRevitResources.dll' because it is being used by another process.' [D:\Build\++UE4\Sync\Engine\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter\Resources\DatasmithRevitResources.csproj]"
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				Assert.AreEqual(3, Events.Count);

				// 0
				CheckEventGroup(Events.Slice(0, 1), 0, 1, LogLevel.Warning, KnownLogEvents.Microsoft);
				Assert.AreEqual("TL2012", Events[0].Properties!["code"].ToString());
				Assert.AreEqual("20", Events[0].Properties!["line"].ToString());

				LogValue FileProperty0 = (LogValue)Events[0].Properties!["file"];
				Assert.AreEqual(@"SourceFile", FileProperty0.Type);
				Assert.AreEqual(@"Foo/Bar.txt", FileProperty0.Properties!["relativePath"].ToString());

				// 1
				CheckEventGroup(Events.Slice(1, 1), 1, 1, LogLevel.Warning, KnownLogEvents.Microsoft);
				Assert.AreEqual("TL2034", Events[1].Properties!["code"].ToString());
				Assert.AreEqual("20", Events[1].Properties!["line"].ToString());
				Assert.AreEqual("30", Events[1].Properties!["column"].ToString());

				LogValue FileProperty1 = (LogValue)Events[1].Properties!["file"];
				Assert.AreEqual(@"SourceFile", FileProperty1.Type);
				Assert.AreEqual(@"Foo/Bar.txt", FileProperty1.Properties!["relativePath"].ToString());

				// 2
				CheckEventGroup(Events.Slice(2, 1), 2, 1, LogLevel.Error, KnownLogEvents.Microsoft);
				Assert.AreEqual("CS2012", Events[2].Properties!["code"].ToString());
				Assert.AreEqual("CSC", Events[2].Properties!["tool"].ToString());
			}
		}

		[TestMethod]
		public void WarningsAsErrorsEventMatcher()
		{
			// Visual C++ error
			{
				string[] Lines =
				{
					@"C:\Horde\Engine\Plugins\Experimental\VirtualCamera\Source\VirtualCamera\Private\VCamBlueprintFunctionLibrary.cpp(249): error C2220: the following warning is treated as an error",
					@"C:\Horde\Engine\Plugins\Experimental\VirtualCamera\Source\VirtualCamera\Private\VCamBlueprintFunctionLibrary.cpp(249): warning C4996: 'UEditorLevelLibrary::PilotLevelActor': The Editor Scripting Utilities Plugin is deprecated - Use the function in Level Editor Subsystem Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.",
					@"..\Plugins\Editor\EditorScriptingUtilities\Source\EditorScriptingUtilities\Public\EditorLevelLibrary.h(122): note: see declaration of 'UEditorLevelLibrary::PilotLevelActor'",
					@"C:\Horde\Engine\Plugins\Experimental\VirtualCamera\Source\VirtualCamera\Private\VCamBlueprintFunctionLibrary.cpp(314): warning C4996: 'UEditorLevelLibrary::EditorSetGameView': The Editor Scripting Utilities Plugin is deprecated - Use the function in Level Editor Subsystem Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.",
					@"..\Plugins\Editor\EditorScriptingUtilities\Source\EditorScriptingUtilities\Public\EditorLevelLibrary.h(190): note: see declaration of 'UEditorLevelLibrary::EditorSetGameView'"
				};

				List<LogEvent> Events = Parse(String.Join("\n", Lines));
				Assert.AreEqual(5, Events.Count);
				CheckEventGroup(Events.Slice(0, 1), 0, 1, LogLevel.Error, KnownLogEvents.Compiler);
				CheckEventGroup(Events.Slice(1, 2), 1, 2, LogLevel.Error, KnownLogEvents.Compiler);
				CheckEventGroup(Events.Slice(3, 2), 3, 2, LogLevel.Error, KnownLogEvents.Compiler);

				LogEvent Event = Events[1];
				Assert.AreEqual("C4996", Event.Properties!["code"].ToString());
				Assert.AreEqual(LogLevel.Error, Event.Level);

				LogValue FileProperty = (LogValue)Event.Properties["file"];

				// FIXME: Fails on Linux. Properties dict is empty
				//Assert.AreEqual(@"//UE4/Main/Engine/Plugins/Experimental/VirtualCamera/Source/VirtualCamera/Private/VCamBlueprintFunctionLibrary.cpp@12345", FileProperty.Properties["depotPath"].ToString());

				LogValue NoteProperty1 = (LogValue)Events[2].Properties!["file"];
				Assert.AreEqual(@"SourceFile", NoteProperty1.Type);
				Assert.AreEqual(@"//UE4/Main/Engine/Plugins/Editor/EditorScriptingUtilities/Source/EditorScriptingUtilities/Public/EditorLevelLibrary.h@12345", NoteProperty1.Properties!["depotPath"].ToString());
			}
		}

		[TestMethod]
		public void ClangEventMatcher()
		{
			string[] Lines =
			{
				@"  In file included from D:\\build\\++Fortnite+Dev+Build+AWS+Incremental\\Sync\\FortniteGame\\Intermediate\\Build\\PS5\\FortniteClient\\Development\\FortInstallBundleManager\\Module.FortInstallBundleManager.cpp:3:",
				@"  In file included from D:\\build\\++Fortnite+Dev+Build+AWS+Incremental\\Sync\\FortniteGame\\Intermediate\\Build\\PS5\\FortniteClient\\Development\\FortInstallBundleManager\\Module.FortInstallBundleManager.cpp:3:",
				@"  D:/build/++Fortnite+Dev+Build+AWS+Incremental/Sync/FortniteGame/Plugins/Runtime/FortInstallBundleManager/Source/Private/FortInstallBundleManagerUtil.cpp(38,11): fatal error: 'PlatformInstallBundleSource.h' file not found",
				@"          #include ""PlatformInstallBundleSource.h""",
				@"                   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
				@"  1 error generated."
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			CheckEventGroup(Events, 0, 5, LogLevel.Error, KnownLogEvents.Compiler);
		}

		[TestMethod]
		public void IOSCompileErrorMatcher()
		{
			string[] Lines =
			{
				@"  [76/9807] Compile MemoryChunkStoreStatistics.cpp",
				@"  /Users/build/Build/++UE4/Sync/Engine/Plugins/Runtime/AR/AzureSpatialAnchorsForARKit/Source/AzureSpatialAnchorsForARKit/Private/AzureSpatialAnchorsForARKit.cpp:7:48: error: unknown type name 'AzureSpatialAnchorsForARKit'; did you mean 'FAzureSpatialAnchorsForARKit'?",
				@"  IMPLEMENT_MODULE(FAzureSpatialAnchorsForARKit, AzureSpatialAnchorsForARKit)",
				@"                                                 ^~~~~~~~~~~~~~~~~~~~~~~~~~~",
				@"                                                 FAzureSpatialAnchorsForARKit",
				@"  /Users/build/Build/++UE4/Sync/Engine/Plugins/Runtime/AR/AzureSpatialAnchorsForARKit/Source/AzureSpatialAnchorsForARKit/Public/AzureSpatialAnchorsForARKit.h:10:7: note: 'FAzureSpatialAnchorsForARKit' declared here",
				@"  class FAzureSpatialAnchorsForARKit : public FAzureSpatialAnchorsBase, public IModuleInterface",
				@"        ^"
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines), new DirectoryReference("/Users/build/Build/++UE4/Sync"));
			CheckEventGroup(Events, 1, 1, LogLevel.Error, KnownLogEvents.Compiler);

			LogEvent Event = Events[0];
			Assert.AreEqual("7", Event.Properties!["line"].ToString());
			Assert.AreEqual("48", Event.Properties!["column"].ToString());

			LogValue FileProperty = (LogValue)Event.Properties["file"];
			Assert.AreEqual("//UE4/Main/Engine/Plugins/Runtime/AR/AzureSpatialAnchorsForARKit/Source/AzureSpatialAnchorsForARKit/Private/AzureSpatialAnchorsForARKit.cpp@12345", FileProperty.Properties!["depotPath"]);
		}

		[TestMethod]
		public void LinkerEventMatcher()
		{
			{
				List<LogEvent> Events = Parse(@"  TP_VehicleAdvPawn.cpp.obj : error LNK2019: unresolved external symbol ""__declspec(dllimport) private: static class UClass * __cdecl UPhysicalMaterial::GetPrivateStaticClass(void)"" (__imp_?GetPrivateStaticClass@UPhysicalMaterial@@CAPEAVUClass@@XZ) referenced in function ""class UPhysicalMaterial * __cdecl ConstructorHelpersInternal::FindOrLoadObject<class UPhysicalMaterial>(class FString &,unsigned int)"" (??$FindOrLoadObject@VUPhysicalMaterial@@@ConstructorHelpersInternal@@YAPEAVUPhysicalMaterial@@AEAVFString@@I@Z)");
				CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
				Assert.AreEqual("__declspec(dllimport) private: static class UClass * __cdecl UPhysicalMaterial::GetPrivateStaticClass(void)", Events[0].Properties!["symbol"].ToString());
			}

			{
				List<LogEvent> Events = Parse(@"  D:\Build\++UE4\Sync\Templates\TP_VehicleAdv\Binaries\Win64\UE4Editor-TP_VehicleAdv.dll : fatal error LNK1120: 1 unresolved externals");
				Assert.AreEqual(1, Events.Count);
				Assert.AreEqual(LogLevel.Error, Events[0].Level);
			}
		}

		[TestMethod]
		public void LinkerFatalEventMatcher()
		{
			string[] Lines =
			{
				@"  webrtc.lib(celt.obj) : error LNK2005: tf_select_table already defined in celt.lib(celt.obj)",
				@"     Creating library D:\Build\++UE4\Sync\Engine\Binaries\Win64\UE4Game.lib and object D:\Build\++UE4\Sync\Engine\Binaries\Win64\UE4Game.exp",
				@"  Engine\Binaries\Win64\UE4Game.exe: fatal error LNK1169: one or more multiply defined symbols found"
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			Assert.AreEqual(2, Events.Count);

			CheckEventGroup(Events.Slice(0, 1), 0, 1, LogLevel.Error, KnownLogEvents.Linker_DuplicateSymbol);
			Assert.AreEqual("tf_select_table", Events[0].Properties!["symbol"].ToString());

			CheckEventGroup(Events.Slice(1, 1), 2, 1, LogLevel.Error, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void SourceFileLineEventMatcher()
		{
			List<LogEvent> Events = Parse("ERROR: C:\\Horde\\InstalledEngineBuild.xml(50): Some error");
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.AutomationTool_SourceFileLine);

			Assert.AreEqual("ERROR", Events[0].Properties!["severity"].ToString());
			Assert.AreEqual("C:\\Horde\\InstalledEngineBuild.xml", Events[0].Properties!["file"].ToString());
			Assert.AreEqual("50", Events[0].Properties!["line"].ToString());
		}

		[TestMethod]
		public void SourceFileEventMatcher()
		{
			List<LogEvent> Events = Parse("  WARNING: Engine\\Plugins\\Test\\Foo.cpp: Missing copyright boilerplate");
			CheckEventGroup(Events, 0, 1, LogLevel.Warning, KnownLogEvents.AutomationTool_MissingCopyright);

			Assert.AreEqual("WARNING", Events[0].Properties!["severity"].ToString());
			Assert.AreEqual("Engine\\Plugins\\Test\\Foo.cpp", Events[0].Properties!["file"].ToString());
		}

		[TestMethod]
		public void MSBuildEventMatcher()
		{
			List<LogEvent> Events = Parse(@"  C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\Microsoft.Common.CurrentVersion.targets(4207,5): warning MSB3026: Could not copy ""obj\Development\DotNETUtilities.dll"" to ""..\..\..\..\Binaries\DotNET\DotNETUtilities.dll"". Beginning retry 2 in 1000ms. The process cannot access the file '..\..\..\..\Binaries\DotNET\DotNETUtilities.dll' because it is being used by another process. The file is locked by: ""UnrealAutomationTool(13236)"" [C:\Horde\Engine\Source\Programs\DotNETCommon\DotNETUtilities\DotNETUtilities.csproj]");
			CheckEventGroup(Events, 0, 1, LogLevel.Information, KnownLogEvents.Systemic_MSBuild);

			Assert.AreEqual("warning", Events[0].Properties!["severity"].ToString());
			
			// FIXME: Fails on Linux. Properties dict is empty
			//Assert.AreEqual(@"Engine\Source\Programs\DotNETCommon\DotNETUtilities\DotNETUtilities.csproj", GetSubProperty(Events[0], "file", "relativePath"));
		}

		[TestMethod]
		public void MonoEventMatcher()
		{
			string[] Lines =
			{
				@"Running: sh -c 'xbuild ""/Users/build/Build/++UE4/Sync/Engine/Source/Programs/AutomationTool/Gauntlet/Gauntlet.Automation.csproj"" /verbosity:quiet /nologo /target:Build  /p:Platform=AnyCPU  /p:Configuration=Development  /p:EngineDir=/Users/build/Build/++UE4/Sync/Engine /p:TreatWarningsAsErrors=false /p:NoWarn=""612,618,672,1591"" /p:BuildProjectReferences=true /p:DefineConstants=MONO /p:DefineConstants=__MonoCS__ /verbosity:quiet /nologo |grep -i error; if [ $? -ne 1 ]; then exit 1; else exit 0; fi'",
				@"  /Users/build/Build/++UE4/Sync/Engine/Source/Programs/AutomationTool/Gauntlet/Gauntlet.Automation.csproj: error : /Users/build/Build/++UE4/Sync/Engine/Source/Programs/AutomationTool/Gauntlet/Gauntlet.Automation.csproj: /Users/build/Build/++UE4/Sync/Engine/Source/Programs/AutomationTool/Gauntlet/Gauntlet.Automation.csproj could not import ""../../../../Platforms/*/Source/Programs/AutomationTool/Gauntlet/*.Gauntlet.targets""",
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			CheckEventGroup(Events, 1, 1, LogLevel.Error, KnownLogEvents.Generic);
		}

		[TestMethod]
		public void LinkEventMatcher()
		{
			string[] Lines =
			{
				@"  Undefined symbols for architecture x86_64:",
				@"    ""FUdpPingWorker::SendDataSize"", referenced from:",
				@"        FUdpPingWorker::SendPing(FSocket&, unsigned char*, unsigned int, FInternetAddr const&, UDPPing::FUdpPingPacket&, double&) in Module.Icmp.cpp.o",
				@"  ld: symbol(s) not found for architecture x86_64",
				@"  clang: error: linker command failed with exit code 1 (use -v to see invocation)",
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			CheckEventGroup(Events, 0, 5, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
		}

		[TestMethod]
		public void LinkEventMatcher2()
		{
			string[] Lines =
			{
				@"  ld.lld.exe: error: undefined symbol: Foo::Bar() const",
				@"  ld.lld.exe: error: undefined symbol: Foo::Bar2() const",
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			Assert.AreEqual(2, Events.Count);
			CheckEventGroup(Events, 0, 2, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[0].Properties!["symbol"];
			Assert.AreEqual("Foo::Bar", SymbolProperty.Properties!["identifier"].ToString());

			LogValue SymbolProperty2 = (LogValue)Events[1].Properties!["symbol"];
			Assert.AreEqual("Foo::Bar2", SymbolProperty2.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void LinkEventMatcher3()
		{
			string[] Lines =
			{
				@"  prospero-lld: error: undefined symbol: USkeleton::GetBlendProfile(FName const&)",
				@"  >>> referenced by Module.Frosty.cpp",
				@"  >>>               D:\\build\\UE5M+Inc\\Sync\\Collaboration\\Frosty\\Frosty\\Intermediate\\Build\\PS5\\Frosty\\Development\\Frosty\\Module.Frosty.cpp.o:(UFrostyAnimInstance::GetBlendProfile(FName const&))",
				@"  prospero-clang: error: linker command failed with exit code 1 (use -v to see invocation)"
			};

			List<LogEvent> Events = Parse(String.Join("\n", Lines));
			CheckEventGroup(Events, 0, 4, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[0].Properties!["symbol"];
			Assert.AreEqual("USkeleton::GetBlendProfile", SymbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void MacLinkErrorMatcher()
		{
			string[] Lines =
			{
				@"   Undefined symbols for architecture arm64:",
				@"     ""Foo::Bar() const"", referenced from:"
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 2, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[1].Properties!["symbol"];
			Assert.AreEqual("symbol", SymbolProperty.Type);
			Assert.AreEqual("Foo::Bar", SymbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void LinuxLinkErrorMatcher()
		{
			string[] Lines =
			{
				@"  ld.lld: error: unable to find library -lstdc++",
				@"  clang++: error: linker command failed with exit code 1 (use -v to see invocation)",
				@"  ld.lld: error: unable to find library -lstdc++",
				@"  clang++: error: linker command failed with exit code 1 (use -v to see invocation)"
			};

			List<LogEvent> Events = Parse(Lines);
			Assert.AreEqual(4, Events.Count);

			CheckEventGroup(Events.Slice(0, 2), 0, 2, LogLevel.Error, KnownLogEvents.Linker);
			CheckEventGroup(Events.Slice(2, 2), 2, 2, LogLevel.Error, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void AndroidLinkErrorMatcher()
		{
			string[] Lines =
			{
				@"   Foo.o:(.data.rel.ro + 0x5d88): undefined reference to `Foo::Bar()'"
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[0].Properties!["symbol"];
			Assert.AreEqual("Foo::Bar", SymbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void AndroidLinkWarningMatcher()
		{
			string[] Lines =
			{
				@"  ld.lld: warning: found local symbol '__bss_start__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '_bss_end__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '_edata' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '__bss_end__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '_end' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '__end__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '__bss_start' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 7, LogLevel.Warning, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void AndroidGradleErrorMatcher()
		{
			string[] Lines =
			{
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #0: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #1: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #2: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #3: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #4: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #5: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #6: shutdown",
				@"    AAPT2 aapt2-4.0.0-6051327-windows Daemon #7: shutdown",
				@"",
				@"    FAILURE: Build failed with an exception.",
				@"",
				@"    * What went wrong:",
				@"    Execution failed for task ':app:buildUeDevelopmentDebugPreBundle'.",
				@"    > Required array size too large",
				@"",
				@"    * Try:",
				@"    Run with --debug option to get more log output. Run with --scan to get full insights.",
				@"",
				@"    * Exception is:",
				@"    org.gradle.api.tasks.TaskExecutionException: Execution failed for task ':app:buildUeDevelopmentDebugPreBundle'.",
				@"    	at org.gradle.api.internal.tasks.execution.ExecuteActionsTaskExecuter.lambda$executeIfValid$1(ExecuteActionsTaskExecuter.java:205)",
				@"    	at org.gradle.internal.Try$Failure.ifSuccessfulOrElse(Try.java:263)",
				@"    	at org.gradle.api.internal.tasks.execution.ExecuteActionsTaskExecuter.executeIfValid(ExecuteActionsTaskExecuter.java:203)",
				@"    	at org.gradle.api.internal.tasks.execution.ExecuteActionsTaskExecuter.execute(ExecuteActionsTaskExecuter.java:184)",
				@"    	at org.gradle.api.internal.tasks.execution.CleanupStaleOutputsExecuter.execute(CleanupStaleOutputsExecuter.java:109)",
				@"    	at org.gradle.api.internal.tasks.execution.FinalizePropertiesTaskExecuter.execute(FinalizePropertiesTaskExecuter.java:46)",
				@"    	at org.gradle.api.internal.tasks.execution.ResolveTaskExecutionModeExecuter.execute(ResolveTaskExecutionModeExecuter.java:62)",
				@"    	at org.gradle.api.internal.tasks.execution.SkipTaskWithNoActionsExecuter.execute(SkipTaskWithNoActionsExecuter.java:57)",
				@"    	at org.gradle.api.internal.tasks.execution.SkipOnlyIfTaskExecuter.execute(SkipOnlyIfTaskExecuter.java:56)",
				@"    	at org.gradle.api.internal.tasks.execution.CatchExceptionTaskExecuter.execute(CatchExceptionTaskExecuter.java:36)",
				@"    	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter$1.executeTask(EventFiringTaskExecuter.java:77)",
				@"    	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter$1.call(EventFiringTaskExecuter.java:55)",
				@"    	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter$1.call(EventFiringTaskExecuter.java:52)",
				@"    	at org.gradle.internal.operations.DefaultBuildOperationExecutor$CallableBuildOperationWorker.execute(DefaultBuildOperationExecutor.java:416)",
				@"    	at org.gradle.internal.operations.DefaultBuildOperationExecutor$CallableBuildOperationWorker.execute(DefaultBuildOperationExecutor.java:406)",
				@"    	at org.gradle.internal.operations.DefaultBuildOperationExecutor$1.execute(DefaultBuildOperationExecutor.java:165)",
				@"    	at org.gradle.internal.operations.DefaultBuildOperationExecutor.execute(DefaultBuildOperationExecutor.java:250)",
				@"    	at org.gradle.internal.operations.DefaultBuildOperationExecutor.execute(DefaultBuildOperationExecutor.java:158)",
				@"    	at org.gradle.internal.operations.DefaultBuildOperationExecutor.call(DefaultBuildOperationExecutor.java:102)",
				@"    	at org.gradle.internal.operations.DelegatingBuildOperationExecutor.call(DelegatingBuildOperationExecutor.java:36)",
				@"    	at org.gradle.api.internal.tasks.execution.EventFiringTaskExecuter.execute(EventFiringTaskExecuter.java:52)",
				@"    	at org.gradle.execution.plan.LocalTaskNodeExecutor.execute(LocalTaskNodeExecutor.java:41)",
				@"",
				@"    Something else"
			};

			List<LogEvent> Events = Parse(Lines);
			Assert.AreEqual(33, Events.Count);
			for(int Idx = 0; Idx < 33; Idx++)
			{
				Assert.AreEqual(LogLevel.Error, Events[Idx].Level);
			}
			Assert.AreEqual(9, Events[0].Properties![LogLine]);
			Assert.AreEqual(Lines.Length, (int)Events[0].Properties![LogLine] + Events[0].LineCount + 2);
		}

		[TestMethod]
		public void LldLinkErrorMatcher()
		{
			string[] Lines =
			{
				@"   ld.lld.exe: error: undefined symbol: Foo::Bar() const",
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[0].Properties!["symbol"];
			Assert.AreEqual("Foo::Bar", SymbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void GnuLinkErrorMatcher()
		{
			string[] Lines =
			{
				@"   Link: error: L0039: reference to undefined symbol `Foo::Bar() const' in file",
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[0].Properties!["symbol"];
			Assert.AreEqual("Foo::Bar", SymbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void MicrosoftLinkErrorMatcher()
		{
			string[] Lines =
			{
				@"   Foo.cpp.obj : error LNK2001: unresolved external symbol ""private: virtual void __cdecl Foo::Bar(class UStruct const *,void const *,struct FAssetBundleData &,class FName,class TSet<void const *,struct DefaultKeyFuncs<void const *,0>,class FDefaultSetAllocator> &)const "" (?InitializeAssetBundlesFromMetadata_Recursive@UAssetManager@@EEBAXPEBVUStruct@@PEBXAEAUFAssetBundleData@@VFName@@AEAV?$TSet@PEBXU?$DefaultKeyFuncs@PEBX$0A@@@VFDefaultSetAllocator@@@@@Z)"
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue SymbolProperty = (LogValue)Events[0].Properties!["symbol"];
			Assert.AreEqual("Foo::Bar", SymbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void SuspendLogParsing()
		{
			string[] Lines =
			{
				@"  <-- Suspend Log Parsing -->",
				@"  Error: File Copy failed with Could not find a part of the path 'P:\Builds\Automation\Fortnite\Logs\++Fortnite+Release-14.60\CL-14584315\FortTest.QuickSmokeAthena_(XboxOne_Development_Client)\Client\Saved\Settings\FortniteGame\Saved\Config\CrashReportClient\UE4CC-XboxOne-C4477473430A2DD50ABDD297FF7811CD\CrashReportClient.ini'..",
				@"  <-- Resume Log Parsing -->"
			};

			List<LogEvent> Events = Parse(Lines);
			Assert.AreEqual(0, Events.Count);
		}

		[TestMethod]
		public void GauntletGenericErrorMatcher()
		{
			string[] Lines =
			{
				@"  Error: EngineTest.RunTests Group:HLOD (Win64 Development EditorGame) result=Failed",
				@"    # EngineTest.RunTests Group:HLOD Report",
				@"    ----------------------------------------",
				@"    ### Process Role: Editor (Win64 Development)",
				@"    ----------------------------------------",
				@"    ##### Result: Abnormal Exit: Reason=3/24 tests failed, Code=-1",
				@"    FatalErrors: 0, Ensures: 0, Errors: 8, Warnings: 20, Hash: 0",
				@"    ##### Artifacts",
				@"    Log: P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor\Saved_1\Editor\EditorOutput.log",
				@"    Commandline: d:\Build\++UE5\Sync\EngineTest\EngineTest.uproject   -gauntlet  -unattended  -stdout  -AllowStdOutLogVerbosity  -gauntlet.heartbeatperiod=30  -NoWatchdog  -FORCELOGFLUSH  -CrashForUAT  -buildmachine  -ReportExportPath=""P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor""  -ExecCmds=""Automation RunTests Group:HLOD; Quit;""  -ddc=default  -userdir=""d:\Build\++UE5\Sync/Tests\DeviceCache\Win64\LocalDevice0_UserDir""",
				@"    P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor\Saved_1\Editor",
				@"    ----------------------------------------",
				@"    ## Summary",
				@"    ### EngineTest.RunTests Group:HLOD Failed",
				@"    ### Editor: 3/24 tests failed",
				@"    See below for logs and any callstacks",
				@"    Context: Win64 Development EditorGame",
				@"    FatalErrors: 0, Ensures: 0, Errors: 8, Warnings: 20",
				@"    Result: Failed, ResultHash: 0",
				@"    21 of 24 tests passed"
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 20, LogLevel.Error, KnownLogEvents.Gauntlet);
		}

		[TestMethod]
		public void DockerWarningMatcher()
		{
			string[] Lines =
			{
				@"#14 8.477 cc -O2 -Wall -DLUA_ANSI -DENABLE_CJSON_GLOBAL -DREDIS_STATIC=''    -c -o lauxlib.o lauxlib.c",
				@"#14 8.499 lauxlib.c: In function 'luaL_loadfile':",
				@"#14 8.499 lauxlib.c:577:4: warning: this 'while' clause does not guard... [-Wmisleading-indentation]",
				@"#14 8.499     while ((c = getc(lf.f)) != EOF && c != LUA_SIGNATURE[0]) ;",
				@"#14 8.499     ^~~~~",
				@"#14 8.499 lauxlib.c:578:5: note: ...this statement, but the latter is misleadingly indented as if it were guarded by the 'while'",
				@"#14 8.499      lf.extraline = 0;",
				@"#14 8.499      ^~",
				@"#14 8.643 cc -O2 -Wall -DLUA_ANSI -DENABLE_CJSON_GLOBAL -DREDIS_STATIC=''    -c -o lbaselib.o lbaselib.c",
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 2, 1, LogLevel.Warning, KnownLogEvents.Compiler);
		}

		[TestMethod]
		public void DockerErrorMatcher()
		{
			string[] Lines =
			{
				@"  #14 9.301 cc -O2 -Wall -DLUA_ANSI -DENABLE_CJSON_GLOBAL -DREDIS_STATIC=''    -c -o lua.o lua.c",
				@"  #14 9.419 cc -o lua  lua.o liblua.a -lm",
				@"  #14 9.447 /usr/bin/ld: liblua.a(loslib.o): in function `os_tmpname':",
				@"  #14 9.447 loslib.c:(.text+0x280): warning: the use of `tmpnam' is dangerous, better use `mkstemp'",
				@"  #14 9.448 cc -O2 -Wall -DLUA_ANSI -DENABLE_CJSON_GLOBAL -DREDIS_STATIC=''    -c -o luac.o luac.c"
			};
			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 2, 2, LogLevel.Warning, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void GauntletErrorMatcher()
		{
			string[] Lines =
			{
				@"  Error: EngineTest.RunTests Group:HLOD (Win64 Development EditorGame) result=Failed",
				@"    # EngineTest.RunTests Group:HLOD Report",
				@"    ----------------------------------------",
				@"    ### Process Role: Editor (Win64 Development)",
				@"    ----------------------------------------",
				@"    ##### Result: Abnormal Exit: Reason=3/24 tests failed, Code=-1",
				@"    FatalErrors: 0, Ensures: 0, Errors: 8, Warnings: 20, Hash: 0",
				@"",
				@"    ##### Artifacts",
				@"    Log: P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor\Saved_1\Editor\EditorOutput.log",
				@"",
				@"    Commandline: d:\Build\++UE5\Sync\EngineTest\EngineTest.uproject   -gauntlet  -unattended  -stdout  -AllowStdOutLogVerbosity  -gauntlet.heartbeatperiod=30  -NoWatchdog  -FORCELOGFLUSH  -CrashForUAT  -buildmachine  -ReportExportPath=""P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor""  -ExecCmds=""Automation RunTests Group:HLOD; Quit;""  -ddc=default  -userdir=""d:\Build\++UE5\Sync/Tests\DeviceCache\Win64\LocalDevice0_UserDir""",
				@"",
				@"    P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor\Saved_1\Editor",
				@"",
				@"    ----------------------------------------",
				@"    ## Summary",
				@"    ### EngineTest.RunTests Group:HLOD Failed",
				@"    ### Editor: 3/24 tests failed",
				@"    See below for logs and any callstacks",
				@"",
				@"    Context: Win64 Development EditorGame",
				@"",
				@"    FatalErrors: 0, Ensures: 0, Errors: 8, Warnings: 20",
				@"    Result: Failed, ResultHash: 0",
				@"",
				@"    21 of 24 tests passed",
				@"",
				@"    ### The following tests failed:",
				@"    ##### SectionFlags: SectionFlags",
				@"    * LogAutomationController: Building static mesh SectionFlags... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Building static mesh SectionFlags... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SectionFlags_LOD_0_None' test failed, Screenshots were different!  Global Difference = 0.058361, Max Local Difference = 0.821376 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"",
				@"    ##### SimpleMerge: SimpleMerge",
				@"    * LogAutomationController: Building static mesh SM_TeapotHLOD... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Building static mesh SM_TeapotHLOD... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_0_None' was similar!  Global Difference = 0.000298, Max Local Difference = 0.010725 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_1_None' test failed, Screenshots were different!  Global Difference = 0.006954, Max Local Difference = 0.129438 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_2_None' test failed, Screenshots were different!  Global Difference = 0.007732, Max Local Difference = 0.127959 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_3_None' test failed, Screenshots were different!  Global Difference = 0.009140, Max Local Difference = 0.172337 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_0_BaseColor' was similar!  Global Difference = 0.000000, Max Local Difference = 0.000000 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_1_BaseColor' was similar!  Global Difference = 0.002068, Max Local Difference = 0.045858 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_2_BaseColor' was similar!  Global Difference = 0.002377, Max Local Difference = 0.045858 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"    * LogAutomationController: Screenshot 'ActorMerging_SimpleMeshMerge_LOD_3_BaseColor' was similar!  Global Difference = 0.002647, Max Local Difference = 0.057322 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"",
				@"    ##### SingleLODMerge: SingleLODMerge",
				@"    * LogAutomationController: Building static mesh Pencil2... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Building static mesh Pencil2... [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Private\Logging\LogMacros.cpp(92)]",
				@"    * LogAutomationController: Err0r: Screenshot 'ActorMerging_SingleLODMerge_LOD_0_BaseColor' test failed, Screenshots were different!  Global Difference = 0.013100, Max Local Difference = 0.131657 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]",
				@"",
				@"    ### Links",
				@"    View results here: http://automation.epicgames.net/reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor/index.html",
				@"",
				@"    Open results in UnrealEd from P:/Builds/Automation/Reports/++UE5+Main/EngineTest/++UE5+Main-CL-14167122/HLOD_Win64Editor",
			};


			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 55, LogLevel.Error, KnownLogEvents.Gauntlet_UnitTest);

			Assert.AreEqual("HLOD", Events[29].Properties!["group"].ToString());
			Assert.AreEqual("SectionFlags", Events[29].Properties!["name"].ToString());
			Assert.AreEqual("SectionFlags", Events[29].Properties!["friendly_name"].ToString());

			Assert.AreEqual("HLOD", Events[34].Properties!["group"].ToString());
			Assert.AreEqual("SimpleMerge", Events[34].Properties!["name"].ToString());
			Assert.AreEqual("SimpleMerge", Events[34].Properties!["friendly_name"].ToString());

			Assert.AreEqual("HLOD", Events[46].Properties!["group"].ToString());
			Assert.AreEqual("SingleLODMerge", Events[46].Properties!["name"].ToString());
			Assert.AreEqual("SingleLODMerge", Events[46].Properties!["friendly_name"].ToString());
		}

		[TestMethod]
		public void GauntletScreenshotErrorMatcher()
		{
			string Text = @"  Error: LogAutomationController: Error: Screenshot 'ActorMerging_SectionFlags_LOD_0_None' test failed, Screenshots were different!  Global Difference = 0.058361, Max Local Difference = 0.821376 [D:\Build\++UE5\Sync\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h(546)]";

			List<LogEvent> Events = Parse(Text);
			CheckEventGroup(Events, 0, 1, LogLevel.Error, KnownLogEvents.Gauntlet_ScreenshotTest);

			LogEvent Event = Events[0];
			Assert.AreEqual("ActorMerging_SectionFlags_LOD_0_None", Event.Properties!["screenshot"].ToString());
		}

		[TestMethod]
		public void SystemicErrorMatcher()
		{
			string[] Lines =
			{
				@"    LogDerivedDataCache: Warning: Access to //epicgames.net/root/DDC-Global-UE4 appears to be slow. 'Touch' will be disabled and queries/writes will be limited."
			};

			List<LogEvent> Events = Parse(Lines);
			CheckEventGroup(Events, 0, 1, LogLevel.Information, KnownLogEvents.Systemic_SlowDDC);
		}

		[TestMethod]
		public void XoreaxErrorMatcher()
		{
			string[] Lines =
			{
				@"--------------------Build System Warning---------------------------------------",
				@"Failed to connect to Coordinator:",
				@"    All builds will run in standalone mode.",
				@"--------------------Build System Warning (Agent 'Cloud_1p5rlm3rq_10 (Core #9)')",
				@"Remote tasks distribution:",
				@"    Tasks execution is impeded due to low agent responsiveness",
				@"-------------------------------------------------------------------------------",
				@"    LogXGEController: Warning: XGE's background service (BuildService.exe) is not running - service is likely disabled on this machine.",
			};

			List<LogEvent> Events = Parse(Lines);
			Assert.AreEqual(7, Events.Count);
			CheckEventGroup(Events.Slice(0, 3), 0, 3, LogLevel.Information, KnownLogEvents.Systemic_Xge_Standalone);
			CheckEventGroup(Events.Slice(3, 3), 3, 3, LogLevel.Information, KnownLogEvents.Systemic_Xge);
			CheckEventGroup(Events.Slice(6, 1), 7, 1, LogLevel.Information, KnownLogEvents.Systemic_Xge_ServiceNotRunning);
		}

		string GetSubProperty(LogEvent Event, string SpanName, string Name)
		{
			LogEventSpan Span = (LogEventSpan)Event.Properties![SpanName];
			Console.WriteLine("DUMP\n\n\n");
			foreach (KeyValuePair<string, object> kvp in Span.Properties)
			{
				Console.WriteLine("Key = {0}, Value = {1}", kvp.Key, kvp.Value);
			}
			return Span.Properties[Name].ToString()!;
		}

		List<LogEvent> Parse(IEnumerable<string> Lines)
		{
			return Parse(String.Join("\n", Lines));
		}

		List<LogEvent> Parse(string Text)
		{
			return Parse(Text, new DirectoryReference("C:\\Horde".Replace('\\', Path.DirectorySeparatorChar)));
		}

		List<LogEvent> Parse(string Text, DirectoryReference WorkspaceDir)
		{
			LogParserContext Context = new LogParserContext();
			Context.WorkspaceDir = WorkspaceDir;
			Context.PerforceStream = "//UE4/Main";
			Context.PerforceChange = 12345;

			List<string> IgnorePatterns = new List<string>();

			byte[] TextBytes = Encoding.UTF8.GetBytes(Text);

			Random Generator = new Random(0);

			LoggerCapture Logger = new LoggerCapture();
			using (LogParser Parser = new LogParser(Logger, Context, IgnorePatterns))
			{
				int Pos = 0;
				while(Pos < TextBytes.Length)
				{
					int Len = Math.Min((int)(Generator.NextDouble() * 256), TextBytes.Length - Pos);
					Parser.WriteData(TextBytes.AsMemory(Pos, Len));
					Pos += Len;
				}
			}
			return Logger.Events;
		}

		void CheckEventGroup(IEnumerable<LogEvent> Events, int Index, int Count, LogLevel Level, EventId EventId = default)
		{
			IEnumerator<LogEvent> Enumerator = Events.GetEnumerator();
			for (int Idx = 0; Idx < Count; Idx++)
			{
				Assert.IsTrue(Enumerator.MoveNext());

				LogEvent Event = Enumerator.Current;
				Assert.AreEqual(Level, Event.Level);
				Assert.AreEqual(EventId, Event.Id);
				Assert.AreEqual(Idx, Event.LineIndex);
				Assert.AreEqual(Count, Event.LineCount);
				Assert.AreEqual(Index + Idx, Event.Properties![LogLine]);
			}
			Assert.IsFalse(Enumerator.MoveNext());
		}
	}
}
