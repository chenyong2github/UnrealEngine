// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;

#nullable enable

namespace UnrealBuildToolTests
{
	[TestClass]
	public class LogEventMatcherTests
	{
		const string LogLine = "LogLine";

		class LoggerCapture : ILogger
		{
			int _logLineIndex;

			public List<LogEvent> _events = new List<LogEvent>();

			public IDisposable? BeginScope<TState>(TState state) => null;

			public bool IsEnabled(LogLevel logLevel) => true;

			public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception exception, Func<TState, Exception?, string> formatter)
			{
				LogEvent logEvent = LogEvent.Read(JsonLogEvent.FromLoggerState(logLevel, eventId, state, exception, formatter).Data.Span);
				if (logEvent.Level != LogLevel.Information || logEvent.Id != default || logEvent.Properties != null)
				{
					KeyValuePair<string, object>[] items = new[] { new KeyValuePair<string, object>(LogLine, _logLineIndex) };
					logEvent.Properties = (logEvent.Properties == null) ? items : Enumerable.Concat(logEvent.Properties, items);
					_events.Add(logEvent);
				}

				_logLineIndex++;
			}
		}

		[TestMethod]
		public void CompileEventMatcher()
		{
			// Visual C++ error
			{
				string[] lines =
				{
					@"  C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
					@"          with",
					@"          [",
					@"              UserClass=AFortVehicleManager",
					@"          ]",
					@"  C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp(78): note: Types pointed to are unrelated; conversion requires reinterpret_cast, C-style cast or function-style cast",
					@"  C:\Horde\Sync\Engine\Source\Runtime\Core\Public\Delegates/DelegateSignatureImpl.inl(871): note: see declaration of 'TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject'",
				};

				List<LogEvent> logEvents = Parse(String.Join("\n", lines));
				CheckEventGroup(logEvents, 0, 7, LogLevel.Error, KnownLogEvents.Compiler);

				Assert.AreEqual("C2664", logEvents[0].GetProperty("code").ToString());
				Assert.AreEqual("78", logEvents[0].GetProperty("line").ToString());

				LogValue fileProperty = (LogValue)logEvents[0].GetProperty("file");
				Assert.AreEqual(@"C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp", fileProperty.Text);
				Assert.AreEqual(LogValueType.SourceFile, fileProperty.Type);

				LogValue noteProperty1 = logEvents[5].GetProperty<LogValue>("file");
				Assert.AreEqual(@"C:\Horde/Fortnite Game/Source/FortniteGame/Private/FortVehicleManager.cpp", noteProperty1.Text);
				Assert.AreEqual(LogValueType.SourceFile, noteProperty1.Type);

				LogValue noteProperty2 = logEvents[6].GetProperty<LogValue>("file");
				Assert.AreEqual(@"C:\Horde\Sync\Engine\Source\Runtime\Core\Public\Delegates/DelegateSignatureImpl.inl", noteProperty2.Text);

				// FIXME: Fails on Linux. Properties dict is empty
				//Assert.AreEqual(@"SourceFile", NoteProperty2.Properties["type"].ToString());
			}
		}

		[TestMethod]
		public void MicrosoftEventMatcher()
		{
			// Generic Microsoft errors which can be parsed by visual studio
			{
				string[] lines =
				{
					@" C:\Horde\Foo\Bar.txt(20): warning TL2012: Some error message",
					@" C:\Horde\Foo\Bar.txt(20, 30) : warning TL2034: Some error message",
					@" CSC : error CS2012: Cannot open 'D:\Build\++UE4\Sync\Engine\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter\Resources\obj\Release\DatasmithRevitResources.dll' for writing -- 'The process cannot access the file 'D:\Build\++UE4\Sync\Engine\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter\Resources\obj\Release\DatasmithRevitResources.dll' because it is being used by another process.' [D:\Build\++UE4\Sync\Engine\Source\Programs\Enterprise\Datasmith\DatasmithRevitExporter\Resources\DatasmithRevitResources.csproj]"
				};

				List<LogEvent> logEvents = Parse(String.Join("\n", lines));
				Assert.AreEqual(3, logEvents.Count);

				// 0
				CheckEventGroup(logEvents.Slice(0, 1), 0, 1, LogLevel.Warning, KnownLogEvents.Microsoft);
				Assert.AreEqual("TL2012", logEvents[0].GetProperty("code").ToString());
				Assert.AreEqual("20", logEvents[0].GetProperty("line").ToString());

				LogValue fileProperty0 = (LogValue)logEvents[0].GetProperty("file");
				Assert.AreEqual(LogValueType.SourceFile, fileProperty0.Type);
				Assert.AreEqual(@"C:\Horde\Foo\Bar.txt", fileProperty0.Text);

				// 1
				CheckEventGroup(logEvents.Slice(1, 1), 1, 1, LogLevel.Warning, KnownLogEvents.Microsoft);
				Assert.AreEqual("TL2034", logEvents[1].GetProperty("code").ToString());
				Assert.AreEqual("20", logEvents[1].GetProperty("line").ToString());
				Assert.AreEqual("30", logEvents[1].GetProperty("column").ToString());

				LogValue fileProperty1 = logEvents[1].GetProperty<LogValue>("file");
				Assert.AreEqual(LogValueType.SourceFile, fileProperty1.Type);
				Assert.AreEqual(@"C:\Horde\Foo\Bar.txt", fileProperty1.Text);

				// 2
				CheckEventGroup(logEvents.Slice(2, 1), 2, 1, LogLevel.Error, KnownLogEvents.Microsoft);
				Assert.AreEqual("CS2012", logEvents[2].GetProperty("code").ToString());
				Assert.AreEqual("CSC", logEvents[2].GetProperty("tool").ToString());
			}
		}

		[TestMethod]
		public void WarningsAsErrorsEventMatcher()
		{
			// Visual C++ error
			{
				string[] lines =
				{
					@"C:\Horde\Engine\Plugins\Experimental\VirtualCamera\Source\VirtualCamera\Private\VCamBlueprintFunctionLibrary.cpp(249): error C2220: the following warning is treated as an error",
					@"C:\Horde\Engine\Plugins\Experimental\VirtualCamera\Source\VirtualCamera\Private\VCamBlueprintFunctionLibrary.cpp(249): warning C4996: 'UEditorLevelLibrary::PilotLevelActor': The Editor Scripting Utilities Plugin is deprecated - Use the function in Level Editor Subsystem Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.",
					@"..\Plugins\Editor\EditorScriptingUtilities\Source\EditorScriptingUtilities\Public\EditorLevelLibrary.h(122): note: see declaration of 'UEditorLevelLibrary::PilotLevelActor'",
					@"C:\Horde\Engine\Plugins\Experimental\VirtualCamera\Source\VirtualCamera\Private\VCamBlueprintFunctionLibrary.cpp(314): warning C4996: 'UEditorLevelLibrary::EditorSetGameView': The Editor Scripting Utilities Plugin is deprecated - Use the function in Level Editor Subsystem Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.",
					@"..\Plugins\Editor\EditorScriptingUtilities\Source\EditorScriptingUtilities\Public\EditorLevelLibrary.h(190): note: see declaration of 'UEditorLevelLibrary::EditorSetGameView'"
				};

				List<LogEvent> logEvents = Parse(String.Join("\n", lines));
				Assert.AreEqual(5, logEvents.Count);
				CheckEventGroup(logEvents.Slice(0, 1), 0, 1, LogLevel.Error, KnownLogEvents.Compiler);
				CheckEventGroup(logEvents.Slice(1, 2), 1, 2, LogLevel.Error, KnownLogEvents.Compiler);
				CheckEventGroup(logEvents.Slice(3, 2), 3, 2, LogLevel.Error, KnownLogEvents.Compiler);

				LogEvent logEvent = logEvents[1];
				Assert.AreEqual("C4996", logEvent.GetProperty("code").ToString());
				Assert.AreEqual(LogLevel.Error, logEvent.Level);

				//LogValue FileProperty = (LogValue)Event.Properties["file"];

				// FIXME: Fails on Linux. Properties dict is empty
				//Assert.AreEqual(@"//UE4/Main/Engine/Plugins/Experimental/VirtualCamera/Source/VirtualCamera/Private/VCamBlueprintFunctionLibrary.cpp@12345", FileProperty.Properties["depotPath"].ToString());

				LogValue noteProperty1 = logEvents[2].GetProperty<LogValue>("file");
				Assert.AreEqual(LogValueType.SourceFile, noteProperty1.Type);
				Assert.AreEqual(@"Engine\Plugins\Editor\EditorScriptingUtilities\Source\EditorScriptingUtilities\Public\EditorLevelLibrary.h", noteProperty1.Properties!["file"].ToString()!);
			}
		}

		[TestMethod]
		public void ClangEventMatcher()
		{
			string[] lines =
			{
				@"  In file included from D:\\build\\++Fortnite+Dev+Build+AWS+Incremental\\Sync\\FortniteGame\\Intermediate\\Build\\PS5\\FortniteClient\\Development\\FortInstallBundleManager\\Module.FortInstallBundleManager.cpp:3:",
				@"  In file included from D:\\build\\++Fortnite+Dev+Build+AWS+Incremental\\Sync\\FortniteGame\\Intermediate\\Build\\PS5\\FortniteClient\\Development\\FortInstallBundleManager\\Module.FortInstallBundleManager.cpp:3:",
				@"  D:/build/++Fortnite+Dev+Build+AWS+Incremental/Sync/FortniteGame/Plugins/Runtime/FortInstallBundleManager/Source/Private/FortInstallBundleManagerUtil.cpp(38,11): fatal error: 'PlatformInstallBundleSource.h' file not found",
				@"          #include ""PlatformInstallBundleSource.h""",
				@"                   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
				@"  1 error generated."
			};

			List<LogEvent> events = Parse(String.Join("\n", lines));
			CheckEventGroup(events, 0, 5, LogLevel.Error, KnownLogEvents.Compiler);
		}

		[TestMethod]
		public void IOSCompileErrorMatcher()
		{
			string[] lines =
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

			List<LogEvent> logEvents = Parse(String.Join("\n", lines), new DirectoryReference("/Users/build/Build/++UE4/Sync"));
			CheckEventGroup(logEvents, 1, 1, LogLevel.Error, KnownLogEvents.Compiler);

			LogEvent logEvent = logEvents[0];
			Assert.AreEqual("7", logEvent.GetProperty("line").ToString());
			Assert.AreEqual("48", logEvent.GetProperty("column").ToString());

			LogValue fileProperty = logEvent.GetProperty<LogValue>("file");
			Assert.AreEqual("/Users/build/Build/++UE4/Sync/Engine/Plugins/Runtime/AR/AzureSpatialAnchorsForARKit/Source/AzureSpatialAnchorsForARKit/Private/AzureSpatialAnchorsForARKit.cpp", fileProperty.Text);
		}

		[TestMethod]
		public void LinkerEventMatcher()
		{
			{
				List<LogEvent> logEvents = Parse(@"  TP_VehicleAdvPawn.cpp.obj : error LNK2019: unresolved external symbol ""__declspec(dllimport) private: static class UClass * __cdecl UPhysicalMaterial::GetPrivateStaticClass(void)"" (__imp_?GetPrivateStaticClass@UPhysicalMaterial@@CAPEAVUClass@@XZ) referenced in function ""class UPhysicalMaterial * __cdecl ConstructorHelpersInternal::FindOrLoadObject<class UPhysicalMaterial>(class FString &,unsigned int)"" (??$FindOrLoadObject@VUPhysicalMaterial@@@ConstructorHelpersInternal@@YAPEAVUPhysicalMaterial@@AEAVFString@@I@Z)");
				CheckEventGroup(logEvents, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
				Assert.AreEqual("__declspec(dllimport) private: static class UClass * __cdecl UPhysicalMaterial::GetPrivateStaticClass(void)", logEvents[0].GetProperty("symbol").ToString());
			}

			{
				List<LogEvent> logEvents = Parse(@"  D:\Build\++UE4\Sync\Templates\TP_VehicleAdv\Binaries\Win64\UE4Editor-TP_VehicleAdv.dll : fatal error LNK1120: 1 unresolved externals");
				Assert.AreEqual(1, logEvents.Count);
				Assert.AreEqual(LogLevel.Error, logEvents[0].Level);
			}

			{

				string[] lines =
				{
					@"tool 19.50.0.12 (rel,tool,19.500 @527452 x64) D:\Workspaces\AutoSDK\HostWin64\9.508.001\9.500\host_tools\bin\tool.exe",
					@"Link : error: L0039: reference to undefined symbol `UProjectPlayControllerComponent::HandleMoveToolSpawnedActor(ATestGameCreativeMoveTool*, AActor*, bool)' in file ""D:\Workspaces\TestGame\TestGame\Intermediate\Build\Win64\TestGame\Development\ProjectPlayGameRuntime\Module.ProjectPlayGameRuntime.gen.cpp.o""\",
					@"Link : error: L0039: reference to undefined symbol `UProjectPlayControllerComponent::HandleWeaponEquipped(ATestGameWeapon*, ATestGameWeapon*)' in file ""D:\Workspaces\TestGame\TestGame\Intermediate\Build\Win64\TestGameClient\Development\ProjectPlayGameRuntime\Module.ProjectPlayGameRuntime.gen.cpp.o""\",
					@"Module.ProjectPlayGameRuntime.gen.cpp : error: L0039: reference to undefined symbol `UProjectPlayControllerComponent::HandleMoveToolSpawnedActor(ATestGameMoveTool*, AActor*, bool)' in file ""D:\Workspaces\TestGame\TestGame\Intermediate\Build\Win64\TestGameClient\Development\ProjectPlayGameRuntime\Module.ProjectPlayGameRuntime.gen.cpp.o""\",
					@"Module.ProjectPlayGameRuntime.gen.cpp : error: L0039: reference to undefined symbol `UProjectPlayControllerComponent::HandleWeaponEquipped(ATestGameWeapon*, ATestGameWeapon*)' in file ""D:\Workspaces\TestGame\TestGame\Intermediate\Build\Win64\TestGame\Development\ProjectPlayGameRuntime\Module.ProjectPlayGameRuntime.gen.cpp.o""\",
					@"orbis-clang: error: linker command failed with exit code 1 (use -v to see invocation)"
				};

				List<LogEvent> logEvents = Parse(String.Join("\n", lines), new DirectoryReference("/Users/build/Build/++UE5/Sync"));
				Assert.AreEqual(5, logEvents.Count);
				CheckEventGroup(logEvents.Slice(0, 5), 1, 5, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

				Assert.AreEqual("UProjectPlayControllerComponent::HandleMoveToolSpawnedActor(ATestGameCreativeMoveTool*, AActor*, bool)", logEvents[0].GetProperty("symbol").ToString());
				Assert.AreEqual("UProjectPlayControllerComponent::HandleWeaponEquipped(ATestGameWeapon*, ATestGameWeapon*)", logEvents[1].GetProperty("symbol").ToString());
				Assert.AreEqual("UProjectPlayControllerComponent::HandleMoveToolSpawnedActor(ATestGameMoveTool*, AActor*, bool)", logEvents[2].GetProperty("symbol").ToString());
				Assert.AreEqual("UProjectPlayControllerComponent::HandleWeaponEquipped(ATestGameWeapon*, ATestGameWeapon*)", logEvents[3].GetProperty("symbol").ToString());

			}
		}

		[TestMethod]
		public void LinkerFatalEventMatcher()
		{
			string[] lines =
			{
				@"  webrtc.lib(celt.obj) : error LNK2005: tf_select_table already defined in celt.lib(celt.obj)",
				@"     Creating library D:\Build\++UE4\Sync\Engine\Binaries\Win64\UE4Game.lib and object D:\Build\++UE4\Sync\Engine\Binaries\Win64\UE4Game.exp",
				@"  Engine\Binaries\Win64\UE4Game.exe: fatal error LNK1169: one or more multiply defined symbols found"
			};

			List<LogEvent> logEvents = Parse(String.Join("\n", lines));
			Assert.AreEqual(2, logEvents.Count);

			CheckEventGroup(logEvents.Slice(0, 1), 0, 1, LogLevel.Error, KnownLogEvents.Linker_DuplicateSymbol);
			Assert.AreEqual("tf_select_table", logEvents[0].GetProperty("symbol").ToString());

			CheckEventGroup(logEvents.Slice(1, 1), 2, 1, LogLevel.Error, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void LinkEventMatcher()
		{
			string[] lines =
			{
				@"  Undefined symbols for architecture x86_64:",
				@"    ""FUdpPingWorker::SendDataSize"", referenced from:",
				@"        FUdpPingWorker::SendPing(FSocket&, unsigned char*, unsigned int, FInternetAddr const&, UDPPing::FUdpPingPacket&, double&) in Module.Icmp.cpp.o",
				@"  ld: symbol(s) not found for architecture x86_64",
				@"  clang: error: linker command failed with exit code 1 (use -v to see invocation)",
			};

			List<LogEvent> logEvents = Parse(String.Join("\n", lines));
			CheckEventGroup(logEvents, 0, 5, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
		}

		[TestMethod]
		public void LinkEventMatcher2()
		{
			string[] lines =
			{
				@"  ld.lld.exe: error: undefined symbol: Foo::Bar() const",
				@"  ld.lld.exe: error: undefined symbol: Foo::Bar2() const",
			};

			List<LogEvent> logEvents = Parse(String.Join("\n", lines));
			Assert.AreEqual(2, logEvents.Count);
			CheckEventGroup(logEvents, 0, 2, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = (LogValue)logEvents[0].GetProperty("symbol");
			Assert.AreEqual("Foo::Bar", symbolProperty.Properties!["identifier"].ToString());

			LogValue symbolProperty2 = (LogValue)logEvents[1].GetProperty("symbol");
			Assert.AreEqual("Foo::Bar2", symbolProperty2.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void LinkEventMatcher3()
		{
			string[] lines =
			{
				@"  prospero-lld: error: undefined symbol: USkeleton::GetBlendProfile(FName const&)",
				@"  >>> referenced by Module.Frosty.cpp",
				@"  >>>               D:\\build\\UE5M+Inc\\Sync\\Collaboration\\Frosty\\Frosty\\Intermediate\\Build\\PS5\\Frosty\\Development\\Frosty\\Module.Frosty.cpp.o:(UFrostyAnimInstance::GetBlendProfile(FName const&))",
				@"  prospero-clang: error: linker command failed with exit code 1 (use -v to see invocation)"
			};

			List<LogEvent> logEvents = Parse(String.Join("\n", lines));
			CheckEventGroup(logEvents, 0, 4, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = (LogValue)logEvents[0].GetProperty("symbol");
			Assert.AreEqual("USkeleton::GetBlendProfile", symbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void MacLinkErrorMatcher()
		{
			string[] lines =
			{
				@"   Undefined symbols for architecture arm64:",
				@"     ""Foo::Bar() const"", referenced from:"
			};

			List<LogEvent> logEvents = Parse(lines);
			CheckEventGroup(logEvents, 0, 2, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = (LogValue)logEvents[1].GetProperty("symbol");
			Assert.AreEqual(LogValueType.Symbol, symbolProperty.Type);
			Assert.AreEqual("Foo::Bar", symbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void MacLinkEventMatcher2()
		{
			string[] lines =
			{
				@"  Undefined symbols for architecture x86_64:",
				@"    ""_OBJC_CLASS_$_NSAppleScript"", referenced from:",
				@"        objc-class-ref in libsupp.a(macutil.o)",
				@"    ""_OBJC_CLASS_$_NSString"", referenced from:",
				@"        objc-class-ref in libsupp.a(macutil.o)",
				@"    ""_OBJC_CLASS_$_NSBundle"", referenced from:",
				@"        objc-class-ref in libsupp.a(macutil.o)",
				@"    ""_CGSessionCopyCurrentDictionary"", referenced from:",
				@"        _WindowServicesAvailable in libsupp.a(macutil.o)",
				@"  ld: symbol(s) not found for architecture x86_64"
			};

			List<LogEvent> logEvents = Parse(String.Join("\n", lines));
			CheckEventGroup(logEvents, 0, 10, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);
		}

		[TestMethod]
		public void LinuxLinkErrorMatcher()
		{
			string[] lines =
			{
				@"  ld.lld: error: unable to find library -lstdc++",
				@"  clang++: error: linker command failed with exit code 1 (use -v to see invocation)",
				@"  ld.lld: error: unable to find library -lstdc++",
				@"  clang++: error: linker command failed with exit code 1 (use -v to see invocation)"
			};

			List<LogEvent> logEvents = Parse(lines);
			Assert.AreEqual(4, logEvents.Count);

			CheckEventGroup(logEvents.Slice(0, 2), 0, 2, LogLevel.Error, KnownLogEvents.Linker);
			CheckEventGroup(logEvents.Slice(2, 2), 2, 2, LogLevel.Error, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void AndroidLinkErrorMatcher()
		{
			string[] lines =
			{
				@"   Foo.o:(.data.rel.ro + 0x5d88): undefined reference to `Foo::Bar()'"
			};

			List<LogEvent> logEvents = Parse(lines);
			CheckEventGroup(logEvents, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = (LogValue)logEvents[0].GetProperty("symbol");
			Assert.AreEqual("Foo::Bar", symbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void AndroidLinkWarningMatcher()
		{
			string[] lines =
			{
				@"  ld.lld: warning: found local symbol '__bss_start__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '_bss_end__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '_edata' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '__bss_end__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '_end' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '__end__' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
				@"  ld.lld: warning: found local symbol '__bss_start' in global part of symbol table in file D:/Build/++UE4/Sync/Engine/Plugins/Runtime/GooglePAD/Source/ThirdParty/play-core-native-sdk/libs/arm64-v8a/ndk21.3.6528147/c++_shared\libplaycore.so",
			};

			List<LogEvent> logEvents = Parse(lines);
			CheckEventGroup(logEvents, 0, 7, LogLevel.Warning, KnownLogEvents.Linker);
		}

		[TestMethod]
		public void LldLinkErrorMatcher()
		{
			string[] lines =
			{
				@"   ld.lld.exe: error: undefined symbol: Foo::Bar() const",
			};

			List<LogEvent> logEvents = Parse(lines);
			CheckEventGroup(logEvents, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = logEvents[0].GetProperty<LogValue>("symbol");
			Assert.AreEqual("Foo::Bar", symbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void GnuLinkErrorMatcher()
		{
			string[] lines =
			{
				@"   Link: error: L0039: reference to undefined symbol `Foo::Bar() const' in file",
			};

			List<LogEvent> logEvents = Parse(lines);
			CheckEventGroup(logEvents, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = logEvents[0].GetProperty<LogValue>("symbol");
			Assert.AreEqual("Foo::Bar", symbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void MicrosoftLinkErrorMatcher()
		{
			string[] lines =
			{
				@"   Foo.cpp.obj : error LNK2001: unresolved external symbol ""private: virtual void __cdecl Foo::Bar(class UStruct const *,void const *,struct FAssetBundleData &,class FName,class TSet<void const *,struct DefaultKeyFuncs<void const *,0>,class FDefaultSetAllocator> &)const "" (?InitializeAssetBundlesFromMetadata_Recursive@UAssetManager@@EEBAXPEBVUStruct@@PEBXAEAUFAssetBundleData@@VFName@@AEAV?$TSet@PEBXU?$DefaultKeyFuncs@PEBX$0A@@@VFDefaultSetAllocator@@@@@Z)"
			};

			List<LogEvent> logEvents = Parse(lines);
			CheckEventGroup(logEvents, 0, 1, LogLevel.Error, KnownLogEvents.Linker_UndefinedSymbol);

			LogValue symbolProperty = logEvents[0].GetProperty<LogValue>("symbol");
			Assert.AreEqual("Foo::Bar", symbolProperty.Properties!["identifier"].ToString());
		}

		[TestMethod]
		public void XoreaxErrorMatcher()
		{
			string[] lines =
			{
				@"--------------------Build System Warning---------------------------------------",
				@"Failed to connect to Coordinator:",
				@"    All builds will run in standalone mode.",
				@"--------------------Build System Warning (Agent 'Cloud_1p5rlm3rq_10 (Core #9)')",
				@"Remote tasks distribution:",
				@"    Tasks execution is impeded due to low agent responsiveness",
				@"-------------------------------------------------------------------------------",
				@"    LogXGEController: Warning: XGE's background service (BuildService.exe) is not running - service is likely disabled on this machine.",
				@"BUILD FAILED: Command failed (Result:1): C:\Program Files (x86)\IncrediBuild\xgConsole.exe ""d:\build\Sync\Engine\Programs\AutomationTool\Saved\Logs\UAT_XGE.xml"" /Rebuild /NoLogo /ShowAgent /ShowTime"
			};

			List<LogEvent> logEvents = Parse(lines);
			Assert.AreEqual(8, logEvents.Count);
			CheckEventGroup(logEvents.Slice(0, 3), 0, 3, LogLevel.Information, KnownLogEvents.Systemic_Xge_Standalone);
			CheckEventGroup(logEvents.Slice(3, 3), 3, 3, LogLevel.Information, KnownLogEvents.Systemic_Xge);
			CheckEventGroup(logEvents.Slice(6, 1), 7, 1, LogLevel.Information, KnownLogEvents.Systemic_Xge_ServiceNotRunning);
			CheckEventGroup(logEvents.Slice(7, 1), 8, 1, LogLevel.Error, KnownLogEvents.Systemic_Xge_BuildFailed);
		}

		static List<LogEvent> Parse(IEnumerable<string> lines)
		{
			return Parse(String.Join("\n", lines));
		}

		static List<LogEvent> Parse(string text)
		{
			return Parse(text, new DirectoryReference("C:\\Horde".Replace('\\', Path.DirectorySeparatorChar)));
		}

		static List<LogEvent> Parse(string text, DirectoryReference workspaceDir)
		{
			byte[] textBytes = Encoding.UTF8.GetBytes(text);

			Random generator = new Random(0);

			LoggerCapture logger = new LoggerCapture();

			using (LogEventParser parser = new LogEventParser(logger))
			{
				parser.AddMatchersFromAssembly(typeof(UnrealBuildTool.UnrealBuildTool).Assembly);

				int pos = 0;
				while (pos < textBytes.Length)
				{
					int len = Math.Min((int)(generator.NextDouble() * 256), textBytes.Length - pos);
					parser.WriteData(textBytes.AsMemory(pos, len));
					pos += len;
				}
			}
			return logger._events;
		}

		static void CheckEventGroup(IEnumerable<LogEvent> logEvents, int index, int count, LogLevel level, EventId eventId = default)
		{
			IEnumerator<LogEvent> enumerator = logEvents.GetEnumerator();
			for (int idx = 0; idx < count; idx++)
			{
				Assert.IsTrue(enumerator.MoveNext());

				LogEvent logEvent = enumerator.Current;
				Assert.AreEqual(level, logEvent.Level);
				Assert.AreEqual(eventId, logEvent.Id);
				Assert.AreEqual(idx, logEvent.LineIndex);
				Assert.AreEqual(count, logEvent.LineCount);
				Assert.AreEqual(index + idx, logEvent.GetProperty(LogLine));
			}
			Assert.IsFalse(enumerator.MoveNext());
		}
	}
}
