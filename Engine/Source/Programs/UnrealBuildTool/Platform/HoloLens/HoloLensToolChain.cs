// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	static class Extension
	{
		public static void AddFormat(this List<string> list, string formatString, params object[] args)
		{
			list.Add(String.Format(formatString, args));
		}
	}

	class HoloLensToolChain : UEToolChain
	{

		/// <summary>
		/// The Visual C++ environment
		/// </summary>
		protected VCEnvironment EnvVars;

		/// <summary>
		/// The target being built
		/// </summary>
		protected ReadOnlyTargetRules Target;

		public HoloLensToolChain(ReadOnlyTargetRules Target)
		{
			this.Target = Target;
			EnvVars = Target.WindowsPlatform.Environment;

			if (Target.WindowsPlatform.ObjSrcMapFile != null)
			{
				try
				{
					File.Delete(Target.WindowsPlatform.ObjSrcMapFile);
				}
				catch
				{
				}
			}

			// by default tools chains don't parse arguments, but we want to be able to check the -architectures flag defined above. This is
			// only necessary when AndroidToolChain is used during UAT
			CommandLine.ParseArguments(Environment.GetCommandLineArgs(), this);
		}
		public override void SetEnvironmentVariables()
		{
			EnvVars.SetEnvironmentVariables();
		}

		void AppendCLArguments_Global(CppCompileEnvironment CompileEnvironment, VCEnvironment EnvVars, List<string> Arguments)
		{
			//Arguments.Add("/showIncludes");

			// Suppress generation of object code for unreferenced inline functions. Enabling this option is more standards compliant, and causes a big reduction
			// in object file sizes (and link times) due to the amount of stuff we inline.
			Arguments.Add("/Zc:inline");

			// Prevents the compiler from displaying its logo for each invocation.
			Arguments.Add("/nologo");

			// Enable intrinsic functions.
			Arguments.Add("/Oi");

			// Pack struct members on 8-byte boundaries.
			// Note: we do this even for 32bit builds because it's the required packing mode
			// for WinRT types, and currently enum class forward declarations (which are everywhere)
			// are interpreted as WinRT.
			Arguments.Add("/Zp8");

			if (CompileEnvironment.Platform != UnrealTargetPlatform.HoloLens)
			{
				// Allow the compiler to generate SSE2 instructions. (On by default in 64bit)
				Arguments.Add("/arch:SSE2");
			}

			// Separate functions for linker.
			Arguments.Add("/Gy");

			// Relaxes floating point precision semantics to allow more optimization.
			Arguments.Add("/fp:fast");

			// Compile into an .obj file, and skip linking.
			Arguments.Add("/c");

			// Allow 900% of the default memory allocation limit.
			Arguments.Add("/Zm900");

			// Allow large object files to avoid hitting the 2^16 section limit when running with -StressTestUnity.
			Arguments.Add("/bigobj");

			// Disable "The file contains a character that cannot be represented in the current code page" warning for non-US windows.
			Arguments.Add("/wd4819");

			{
				VCToolChain.AddDefinition(Arguments, "_CRT_STDIO_LEGACY_WIDE_SPECIFIERS", "1");
				//VCToolChain.AddDefinition(Arguments, "USE_SECURE_CRT", "1");
			}

			// @todo HoloLens: Disable "unreachable code" warning since auto-included vccorlib.h triggers it
			Arguments.Add("/wd4702");

			// Disable "usage of ATL attributes is deprecated" since WRL headers generate this
			Arguments.Add("/wd4467");

			// @todo HoloLens: Silence the hash_map deprecation errors for now. This should be replaced with unordered_map for the real fix.
			{
				VCToolChain.AddDefinition(Arguments, "_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS");
			}

			// If compiling as a DLL, set the relevant defines
			if (CompileEnvironment.bIsBuildingDLL)
			{
				VCToolChain.AddDefinition(Arguments, "_WINDLL");
			}

			//
			//	Debug
			//
			if (CompileEnvironment.Configuration == CppConfiguration.Debug)
			{
				// Disable compiler optimization.
				Arguments.Add("/Od");

				// Favor code size (especially useful for embedded platforms).
				Arguments.Add("/Os");

				// Allow inline method expansion unless E&C support is requested
				if( !CompileEnvironment.bSupportEditAndContinue )
				{
					Arguments.Add("/Ob2");
				}

				if (CompileEnvironment.bUseDebugCRT)
				{
					Arguments.Add("/RTC1");
				}
			}
			//
			//	Development and LTCG
			//
			else
			{
				// Maximum optimizations if desired.
				if (CompileEnvironment.bOptimizeCode)
				{
					Arguments.Add("/Ox");

					// Allow optimized code to be debugged more easily.  This makes PDBs a bit larger, but doesn't noticeably affect
					// compile times.  The executable code is not affected at all by this switch, only the debugging information.
					// VC2013 Update 3 has a new flag for doing this
					Arguments.Add("/Zo");
				}

				// Favor code speed.
				Arguments.Add("/Ot");

				// Only omit frame pointers on the PC (which is implied by /Ox) if wanted.
				if (CompileEnvironment.bOmitFramePointers == false)
				{
					Arguments.Add("/Oy-");
				}

				// Allow inline method expansion
				Arguments.Add("/Ob2");

				//
				// LTCG
				//
				if (CompileEnvironment.Configuration == CppConfiguration.Shipping)
				{
					if (CompileEnvironment.bAllowLTCG)
					{
						// Enable link-time code generation.
						Arguments.Add("/GL");
					}
				}
			}

			//
			//	PC
			//

			// Prompt the user before reporting internal errors to Microsoft.
			Arguments.Add("/errorReport:prompt");

			// Enable C++ exception handling, but not C exceptions.
			Arguments.Add("/EHsc");

			// If enabled, create debug information.
			if (CompileEnvironment.bCreateDebugInfo)
			{
				// Store debug info in .pdb files.
				// @todo clang: PDB files are emited from Clang but do not fully work with Visual Studio yet (breakpoints won't hit due to "symbol read error")
				if (CompileEnvironment.bUsePDBFiles)
				{
					// Create debug info suitable for E&C if wanted.
					if (CompileEnvironment.bSupportEditAndContinue &&
						// We only need to do this in debug as that's the only configuration that supports E&C.
						CompileEnvironment.Configuration == CppConfiguration.Debug)
					{
						Arguments.Add("/ZI");
					}
					// Regular PDB debug information.
					else
					{
						Arguments.Add("/Zi");
					}
				}
				// Store C7-format debug info in the .obj files, which is faster.
				else
				{
					Arguments.Add("/Z7");
				}
			}

			// Static CRT not supported for HoloLens
			if (CompileEnvironment.bUseDebugCRT)
			{
				Arguments.Add("/MDd");
			}
			else
			{
				Arguments.Add("/MD");
			}

			DirectoryReference PlatformWinMDLocation = HoloLens.GetCppCXMetadataLocation(EnvVars.Compiler, EnvVars.ToolChainDir);
			if (PlatformWinMDLocation != null)
			{
				Arguments.AddFormat(@" /AI""{0}""", PlatformWinMDLocation);
				Arguments.AddFormat(@" /FU""{0}\platform.winmd""", PlatformWinMDLocation);
			}
		}

		static void AppendCLArguments_CPP(CppCompileEnvironment CompileEnvironment, List<string> Arguments)
		{
			// Enable Windows Runtime extensions.  Do this even for libs (plugins) so that these too can consume WinRT APIs
			Arguments.Add("/ZW");

			// Don't automatically add metadata references.  We'll do that ourselves to avoid referencing windows.winmd directly:
			// we've hit problems where types are somehow in windows.winmd on some installations but not others, leading to either
			// missing or duplicated type references.
			Arguments.Add("/ZW:nostdlib");

			// Explicitly compile the file as C++.
			Arguments.Add("/TP");

			if (!CompileEnvironment.bEnableBufferSecurityChecks)
			{
				// This will disable buffer security checks (which are enabled by default) that the MS compiler adds around arrays on the stack,
				// Which can add some performance overhead, especially in performance intensive code
				// Only disable this if you know what you are doing, because it will be disabled for the entire module!
				Arguments.Add("/GS-");
			}

			if (CompileEnvironment.bUseRTTI)
			{
				// Enable C++ RTTI.
				Arguments.Add("/GR");
			}
			else
			{
				// Disable C++ RTTI.
				Arguments.Add("/GR-");
			}

			// Level 4 warnings.
			Arguments.Add("/W3");
		}

		static void AppendCLArguments_C(List<string> Arguments)
		{
			// Explicitly compile the file as C.
			Arguments.Add("/TC");

			// Level 0 warnings.  Needed for external C projects that produce warnings at higher warning levels.
			Arguments.Add("/W0");
		}

		void AppendLinkArguments(LinkEnvironment LinkEnvironment, List<string> Arguments)
		{
			// Don't create a side-by-side manifest file for the executable.
			Arguments.Add("/MANIFEST:NO");

			// Prevents the linker from displaying its logo for each invocation.
			Arguments.Add("/NOLOGO");

			if (LinkEnvironment.bCreateDebugInfo)
			{
				// Output debug info for the linked executable.

				// Allow partial PDBs for faster linking
				if (LinkEnvironment.bUseFastPDBLinking)
				{
					Arguments.Add("/DEBUG:FASTLINK");
				}
				else
				{ 
					Arguments.Add("/DEBUG");
				}
			}

			// Prompt the user before reporting internal errors to Microsoft.
			Arguments.Add("/errorReport:prompt");

			//
			//	PC
			//
			Arguments.Add("/APPCONTAINER");
			// this helps with store API compliance validation tools, adding additional pdb info
			Arguments.Add("/PROFILE");

			Arguments.Add("/SUBSYSTEM:WINDOWS");

			Arguments.Add("/MACHINE:" + Target.WindowsPlatform.GetArchitectureSubpath());

			if (LinkEnvironment.bIsBuildingConsoleApplication && !LinkEnvironment.bIsBuildingDLL && !String.IsNullOrEmpty(LinkEnvironment.WindowsEntryPointOverride))
			{
				// Use overridden entry point
				Arguments.Add("/ENTRY:" + LinkEnvironment.WindowsEntryPointOverride);
			}

			// Allow the OS to load the EXE at different base addresses than its preferred base address.
			Arguments.Add("/FIXED:No");

			// Explicitly declare that the executable is compatible with Data Execution Prevention.
			Arguments.Add("/NXCOMPAT");

			// Set the default stack size.
			Arguments.Add("/STACK:5000000,131072");

			// Allow delay-loaded DLLs to be explicitly unloaded.
			Arguments.Add("/DELAY:UNLOAD");

			if (LinkEnvironment.bIsBuildingDLL)
			{
				Arguments.Add("/DLL");
			}

			// Don't embed the full PDB path; we want to be able to move binaries elsewhere. They will always be side by side.
			Arguments.Add("/PDBALTPATH:%_PDB%");

			//
			//	Shipping & LTCG
			//
			if (LinkEnvironment.bAllowLTCG &&
				LinkEnvironment.Configuration == CppConfiguration.Shipping)
			{
				// Use link-time code generation.
				Arguments.Add("/LTCG");

				// This is where we add in the PGO-Lite linkorder.txt if we are using PGO-Lite
				//Arguments.Add("/ORDER:@linkorder.txt");
				//Arguments.Add("/VERBOSE");
			}

			//
			//	Shipping binary
			//
			if (LinkEnvironment.Configuration == CppConfiguration.Shipping)
			{
				// Generate an EXE checksum.
				Arguments.Add("/RELEASE");

				// Eliminate unreferenced symbols.
				Arguments.Add("/OPT:REF");

				// Remove redundant COMDATs.
				Arguments.Add("/OPT:ICF");
			}
			//
			//	Regular development binary. 
			//
			else
			{
				// Keep symbols that are unreferenced.
				Arguments.Add("/OPT:NOREF");

				// Disable identical COMDAT folding.
				Arguments.Add("/OPT:NOICF");
			}

			// Enable incremental linking if wanted.
			if (LinkEnvironment.bUseIncrementalLinking)
			{
				Arguments.Add("/INCREMENTAL");
			}
			else
			{
				Arguments.Add("/INCREMENTAL:NO");
			}

			// Suppress warnings about missing PDB files for statically linked libraries.  We often don't want to distribute
			// PDB files for these libraries.
			Arguments.Add("/ignore:4099");    // warning LNK4099: PDB '<file>' was not found with '<file>'
		}

		static void AppendLibArguments(LinkEnvironment LinkEnvironment, List<string> Arguments)
		{
			// Prevents the linker from displaying its logo for each invocation.
			Arguments.Add("/NOLOGO");

			// Prompt the user before reporting internal errors to Microsoft.
			Arguments.Add("/errorReport:prompt");

			//
			//	PC
			//

			Arguments.Add("/SUBSYSTEM:WINDOWS");

			//
			//	Shipping & LTCG
			//
			if (LinkEnvironment.Configuration == CppConfiguration.Shipping)
			{
				// Use link-time code generation.
				Arguments.Add("/LTCG");
			}

			// Ignore warning about /ZW in static libraries. It's not relevant since UE modules
			// have no reason to export new WinRT types, and ignoring it quiets noise when using
			// WinRT APIs from plugins.
			Arguments.Add("/ignore:4264");
		}

		public override CPPOutput CompileCPPFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, string ModuleName, List<Action> Actions)
		{
			List<string> SharedArguments = new List<string>();
			AppendCLArguments_Global(CompileEnvironment, EnvVars, SharedArguments);

			// Add include paths to the argument list.
			foreach (DirectoryReference IncludePath in CompileEnvironment.UserIncludePaths)
			{
				VCToolChain.AddIncludePath(SharedArguments, IncludePath, Target.HoloLensPlatform.Compiler, CompileEnvironment.bPreprocessOnly);
			}
			foreach (DirectoryReference IncludePath in CompileEnvironment.SystemIncludePaths)
			{
				VCToolChain.AddIncludePath(SharedArguments, IncludePath, Target.HoloLensPlatform.Compiler, CompileEnvironment.bPreprocessOnly);
			}

			foreach (DirectoryReference IncludePath in EnvVars.IncludePaths)
			{
				VCToolChain.AddIncludePath(SharedArguments, IncludePath, Target.HoloLensPlatform.Compiler, CompileEnvironment.bPreprocessOnly);
			}

			// Add preprocessor definitions to the argument list.
			foreach (string Definition in CompileEnvironment.Definitions)
			{
				// Escape all quotation marks so that they get properly passed with the command line.
				var DefinitionArgument = Definition.Contains("\"") ? Definition.Replace("\"", "\\\"") : Definition;
				VCToolChain.AddDefinition(SharedArguments,  DefinitionArgument);
			}
			
			// Create a compile action for each source file.
			CPPOutput Result = new CPPOutput();
			foreach (FileItem SourceFile in InputFiles)
			{
				Action CompileAction = new Action(ActionType.Compile);
				Actions.Add(CompileAction);
				CompileAction.CommandDescription = "Compile";

				List<string> FileArguments = new List<string>();
				bool bIsPlainCFile = Path.GetExtension(SourceFile.AbsolutePath).ToUpperInvariant() == ".C";

				// Add the C++ source file and its included files to the prerequisite item list.
				CompileAction.PrerequisiteItems.Add(SourceFile);

				bool bEmitsObjectFile = true;
				if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Create)
				{
					// Generate a CPP File that just includes the precompiled header.
					FileReference PCHCPPPath = CompileEnvironment.PrecompiledHeaderIncludeFilename.ChangeExtension(".cpp");
					FileItem PCHCPPFile = FileItem.CreateIntermediateTextFile(
						PCHCPPPath,
						string.Format("#include \"{0}\"\r\n", CompileEnvironment.PrecompiledHeaderIncludeFilename.FullName.Replace('\\', '/'))
						);

					// Make sure the original source directory the PCH header file existed in is added as an include
					// path -- it might be a private PCH header and we need to make sure that its found!
					string OriginalPCHHeaderDirectory = Path.GetDirectoryName(SourceFile.AbsolutePath);
					FileArguments.AddFormat(" /I \"{0}\"", OriginalPCHHeaderDirectory);

					var PrecompiledFileExtension = UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.HoloLens).GetBinaryExtension(UEBuildBinaryType.PrecompiledHeader);
					// Add the precompiled header file to the produced items list.
					FileItem PrecompiledHeaderFile = FileItem.GetItemByFileReference(
						FileReference.Combine(
							OutputDir,
							Path.GetFileName(SourceFile.AbsolutePath) + PrecompiledFileExtension
							)
						);
					CompileAction.ProducedItems.Add(PrecompiledHeaderFile);
					Result.PrecompiledHeaderFile = PrecompiledHeaderFile;

					// Add the parameters needed to compile the precompiled header file to the command-line.
					FileArguments.AddFormat(" /Yc\"{0}\"", CompileEnvironment.PrecompiledHeaderIncludeFilename);
					FileArguments.AddFormat(" /Fp\"{0}\"", PrecompiledHeaderFile.AbsolutePath);

					// If we're creating a PCH that will be used to compile source files for a library, we need
					// the compiled modules to retain a reference to PCH's module, so that debugging information
					// will be included in the library.  This is also required to avoid linker warning "LNK4206"
					// when linking an application that uses this library.
					if (CompileEnvironment.bIsBuildingLibrary)
					{
						// NOTE: The symbol name we use here is arbitrary, and all that matters is that it is
						// unique per PCH module used in our library
						string FakeUniquePCHSymbolName = CompileEnvironment.PrecompiledHeaderIncludeFilename.GetFileNameWithoutExtension();
						FileArguments.AddFormat(" /Yl{0}", FakeUniquePCHSymbolName);
					}

					FileArguments.AddFormat(" \"{0}\"", PCHCPPFile.AbsolutePath);

					CompileAction.StatusDescription = PCHCPPPath.GetFileName();
				}
				else
				{
					if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Include)
					{
						CompileAction.PrerequisiteItems.Add(CompileEnvironment.PrecompiledHeaderFile);

						FileArguments.Add(String.Format("/FI\"{0}\"", CompileEnvironment.PrecompiledHeaderIncludeFilename.FullName));
						FileArguments.Add(String.Format("/Yu\"{0}\"", CompileEnvironment.PrecompiledHeaderIncludeFilename.FullName));
						FileArguments.Add(String.Format("/Fp\"{0}\"", CompileEnvironment.PrecompiledHeaderFile.AbsolutePath));
					}

					// Add the source file path to the command-line.
					FileArguments.AddFormat(" \"{0}\"", SourceFile.AbsolutePath);

					CompileAction.StatusDescription = Path.GetFileName(SourceFile.AbsolutePath);
				}

				foreach (FileItem ForceIncludeFile in CompileEnvironment.ForceIncludeFiles)
				{
					FileArguments.AddFormat(" /FI\"{0}\"", ForceIncludeFile.AbsolutePath);
				}

				if (bEmitsObjectFile)
				{
					var ObjectFileExtension = UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.HoloLens).GetBinaryExtension(UEBuildBinaryType.Object);
					// Add the object file to the produced item list.
					FileItem ObjectFile = FileItem.GetItemByFileReference(
						FileReference.Combine(
							OutputDir,
							Path.GetFileName(SourceFile.AbsolutePath) + ObjectFileExtension
							)
						);
					CompileAction.ProducedItems.Add(ObjectFile);
					Result.ObjectFiles.Add(ObjectFile);
					FileArguments.AddFormat(" /Fo\"{0}\"", ObjectFile.AbsolutePath);
				}

				// Create PDB files if we were configured to do that.
				if (CompileEnvironment.bUsePDBFiles)
				{
					string PDBFileName;
					bool bActionProducesPDB = false;

					// All files using the same PCH are required to share the same PDB that was used when compiling the PCH
					if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Include)
					{
						PDBFileName = CompileEnvironment.PrecompiledHeaderIncludeFilename.GetFileName();
					}
					// Files creating a PCH use a PDB per file.
					else if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Create)
					{
						PDBFileName = CompileEnvironment.PrecompiledHeaderIncludeFilename.GetFileName();
						bActionProducesPDB = true;
					}
					// Ungrouped C++ files use a PDB per file.
					else if (!bIsPlainCFile)
					{
						PDBFileName = Path.GetFileName(SourceFile.AbsolutePath);
						bActionProducesPDB = true;
					}
					// Group all plain C files that doesn't use PCH into the same PDB
					else
					{
						PDBFileName = "MiscPlainC";
					}

						// Specify the PDB file that the compiler should write to.
					FileItem PDBFile = FileItem.GetItemByFileReference(
							FileReference.Combine(
									OutputDir,
									PDBFileName + ".pdb"
									)
								);
					FileArguments.AddFormat(" /Fd\"{0}\"", PDBFile.AbsolutePath);

					// Only use the PDB as an output file if we want PDBs and this particular action is
					// the one that produces the PDB (as opposed to no debug info, where the above code
					// is needed, but not the output PDB, or when multiple files share a single PDB, so
					// only the action that generates it should count it as output directly)
					if (CompileEnvironment.bUsePDBFiles && bActionProducesPDB)
					{
						CompileAction.ProducedItems.Add(PDBFile);
						Result.DebugDataFiles.Add(PDBFile);
					}
				}

				// Add C or C++ specific compiler arguments.
				if (bIsPlainCFile)
				{
					AppendCLArguments_C(FileArguments);
				}
				else
				{
					AppendCLArguments_CPP(CompileEnvironment, FileArguments);
				}

				CompileAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				CompileAction.CommandPath = EnvVars.CompilerPath;

				string[] AdditionalArguments = String.IsNullOrEmpty(CompileEnvironment.AdditionalArguments) ? new string[0] : new string[] { CompileEnvironment.AdditionalArguments };

				if (!ProjectFileGenerator.bGenerateProjectFiles
					&& Target.WindowsPlatform.Compiler != WindowsCompiler.Clang
					&& CompileAction.ProducedItems.Count > 0)
				{
					FileItem TargetFile = CompileAction.ProducedItems[0];
					FileReference ResponseFileName = new FileReference(TargetFile.AbsolutePath + ".response");
					FileItem ResponseFileItem = FileItem.CreateIntermediateTextFile(ResponseFileName, SharedArguments.Concat(FileArguments).Concat(AdditionalArguments).Select(x => Utils.ExpandVariables(x)));
					CompileAction.CommandArguments = " @\"" + ResponseFileName + "\"";
					CompileAction.PrerequisiteItems.Add(ResponseFileItem);
				}
				else
				{
					CompileAction.CommandArguments = String.Join(" ", SharedArguments.Concat(FileArguments).Concat(AdditionalArguments));
				}
				
				if (CompileEnvironment.bGenerateDependenciesFile)
				{
					List<string> CommandArguments = new List<string>();
					CompileAction.DependencyListFile = FileItem.GetItemByFileReference(FileReference.Combine(OutputDir, String.Format("{0}.txt", SourceFile.Location.GetFileName())));
					CompileAction.ProducedItems.Add(CompileAction.DependencyListFile);
					CommandArguments.Add(String.Format("-dependencies={0}", Utils.MakePathSafeToUseWithCommandLine(CompileAction.DependencyListFile.Location)));

					CommandArguments.Add(String.Format("-compiler={0}", Utils.MakePathSafeToUseWithCommandLine(CompileAction.CommandPath)));
					CommandArguments.Add("--");
					CommandArguments.Add(Utils.MakePathSafeToUseWithCommandLine(CompileAction.CommandPath));
					CommandArguments.Add(CompileAction.CommandArguments);
					CommandArguments.Add("/showIncludes");
					CompileAction.CommandArguments = string.Join(" ", CommandArguments);
					CompileAction.CommandPath = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build", "Windows", "cl-filter", "cl-filter.exe");
				}

				if (CompileEnvironment.PrecompiledHeaderAction == PrecompiledHeaderAction.Create)
				{
					Log.TraceVerbose("Creating PCH: " + CompileEnvironment.PrecompiledHeaderIncludeFilename);
					Log.TraceVerbose("	 Command: " + CompileAction.CommandArguments);
				}
				else
				{
					Log.TraceVerbose("   Compiling: " + CompileAction.StatusDescription);
					Log.TraceVerbose("	 Command: " + CompileAction.CommandArguments);
				}

				// VC++ always outputs the source file name being compiled, so we don't need to emit this ourselves
				CompileAction.bShouldOutputStatusDescription = false;

				// Don't farm out creation of precompiled headers as it is the critical path task.
				CompileAction.bCanExecuteRemotely =
					CompileEnvironment.PrecompiledHeaderAction != PrecompiledHeaderAction.Create ||
					CompileEnvironment.bAllowRemotelyCompiledPCHs
					;

			}
			return Result;
		}

		public override CPPOutput CompileRCFiles(CppCompileEnvironment CompileEnvironment, List<FileItem> InputFiles, DirectoryReference OutputDir, List<Action> Actions)
		{
			CPPOutput Result = new CPPOutput();

			foreach (FileItem RCFile in InputFiles)
			{
				Action CompileAction = new Action(ActionType.Compile);
				CompileAction.CommandDescription = "Resource";
				CompileAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
				CompileAction.CommandPath = EnvVars.ResourceCompilerPath;
				CompileAction.StatusDescription = Path.GetFileName(RCFile.AbsolutePath);
				CompileAction.PrerequisiteItems.AddRange(CompileEnvironment.ForceIncludeFiles);

				// Resource tool can run remotely if possible
				CompileAction.bCanExecuteRemotely = true;

				List<string> Arguments = new List<string>();

				// Suppress header spew
				Arguments.Add("/nologo");

				// If we're compiling for 64-bit Windows, also add the _WIN64 definition to the resource
				// compiler so that we can switch on that in the .rc file using #ifdef.
				if (CompileEnvironment.Architecture == "x64" || CompileEnvironment.Architecture == "ARM64")
				{
					VCToolChain.AddDefinition(Arguments, "_WIN64");
				}

				// Language
				Arguments.Add("/l 0x409");

				// Include paths. Don't use AddIncludePath() here, since it uses the full path and exceeds the max command line length.
				foreach (DirectoryReference IncludePath in CompileEnvironment.UserIncludePaths)
				{
					Arguments.Add(String.Format("/I \"{0}\"", IncludePath));
				}

				// System include paths.
				foreach (DirectoryReference SystemIncludePath in CompileEnvironment.SystemIncludePaths)
				{
					Arguments.Add(String.Format("/I \"{0}\"", SystemIncludePath));
				}
				foreach (DirectoryReference SystemIncludePath in EnvVars.IncludePaths)
				{
					Arguments.Add(String.Format("/I \"{0}\"", SystemIncludePath));
				}

				// Preprocessor definitions.
				foreach (string Definition in CompileEnvironment.Definitions)
				{
					if (!Definition.Contains("_API"))
					{
						VCToolChain.AddDefinition(Arguments, Definition);
					}
				}

				// Add the RES file to the produced item list.
				FileItem CompiledResourceFile = FileItem.GetItemByFileReference(
					FileReference.Combine(
						OutputDir,
						Path.GetFileName(RCFile.AbsolutePath) + ".res"
						)
					);
				CompileAction.ProducedItems.Add(CompiledResourceFile);
				Arguments.Add(String.Format("/fo \"{0}\"", CompiledResourceFile.AbsolutePath));
				Result.ObjectFiles.Add(CompiledResourceFile);

				// Add the RC file as a prerequisite of the action.
				Arguments.Add(String.Format(" \"{0}\"", RCFile.AbsolutePath));

				CompileAction.CommandArguments = String.Join(" ", Arguments);

				// Add the C++ source file and its included files to the prerequisite item list.
				CompileAction.PrerequisiteItems.Add(RCFile);
				Actions.Add(CompileAction);
			}

			return Result;
		}

		public override FileItem LinkFiles(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly, List<Action> Actions) //(LinkEnvironment LinkEnvironment, bool bBuildImportLibraryOnly)
		{
			if (LinkEnvironment.bIsBuildingDotNetAssembly)
			{
				return FileItem.GetItemByFileReference(LinkEnvironment.OutputFilePath);
			}

			bool bIsBuildingLibrary = LinkEnvironment.bIsBuildingLibrary || bBuildImportLibraryOnly;

			// Get link arguments.
			List<string> Arguments = new List<string>();
			if (bIsBuildingLibrary)
			{
				AppendLibArguments(LinkEnvironment, Arguments);
			}
			else
			{
				AppendLinkArguments(LinkEnvironment, Arguments);
			}

			if (Target.HoloLensPlatform.Compiler != WindowsCompiler.Clang && LinkEnvironment.bPrintTimingInfo)
			{
				Arguments.Add("/time+");
			}

			// If we're only building an import library, add the '/DEF' option that tells the LIB utility
			// to simply create a .LIB file and .EXP file, and don't bother validating imports
			if (bBuildImportLibraryOnly)
			{
				Arguments.Add("/DEF");

				// Ensure that the import library references the correct filename for the linked binary.
				Arguments.Add(String.Format("/NAME:\"{0}\"", LinkEnvironment.OutputFilePath.GetFileName()));

				// Ignore warnings about object files with no public symbols.
				Arguments.Add("/IGNORE:4221");
			}


			// Add delay loaded DLLs.
			if (!bIsBuildingLibrary)
			{
				// Delay-load these DLLs.
				foreach (string DelayLoadDLL in LinkEnvironment.DelayLoadDLLs.Distinct())
				{
					Arguments.Add(String.Format("/DELAYLOAD:\"{0}\"", DelayLoadDLL));
				}

				// Pass the module definition file to the linker if we have one
				if (LinkEnvironment.ModuleDefinitionFile != null && LinkEnvironment.ModuleDefinitionFile.Length > 0)
				{
					Arguments.Add(String.Format("/DEF:\"{0}\"", LinkEnvironment.ModuleDefinitionFile));
				}
			}

			// Set up the library paths for linking this binary
			if (bBuildImportLibraryOnly)
			{
				// When building an import library, ignore all the libraries included via embedded #pragma lib declarations. 
				// We shouldn't need them to generate exports.
				Arguments.Add("/NODEFAULTLIB");
			}
			else if (!LinkEnvironment.bIsBuildingLibrary)
			{
				// Add the library paths to the argument list.
				foreach (DirectoryReference LibraryPath in LinkEnvironment.LibraryPaths)
				{
					Arguments.Add(String.Format("/LIBPATH:\"{0}\"", LibraryPath));
				}
				foreach (DirectoryReference LibraryPath in EnvVars.LibraryPaths)
				{
					Arguments.Add(String.Format("/LIBPATH:\"{0}\"", LibraryPath));
				}

				// Add the excluded default libraries to the argument list.
				foreach (string ExcludedLibrary in LinkEnvironment.ExcludedLibraries)
				{
					Arguments.Add(String.Format("/NODEFAULTLIB:\"{0}\"", ExcludedLibrary));
				}
			}

			// For targets that are cross-referenced, we don't want to write a LIB file during the link step as that
			// file will clobber the import library we went out of our way to generate during an earlier step.  This
			// file is not needed for our builds, but there is no way to prevent MSVC from generating it when
			// linking targets that have exports.  We don't want this to clobber our LIB file and invalidate the
			// existing timstamp, so instead we simply emit it with a different name
			FileReference ImportLibraryFilePath = FileReference.Combine(LinkEnvironment.IntermediateDirectory,
														 LinkEnvironment.OutputFilePath.GetFileNameWithoutExtension() + ".lib");

			if (LinkEnvironment.bIsCrossReferenced && !bBuildImportLibraryOnly)
			{
				ImportLibraryFilePath = ImportLibraryFilePath.ChangeExtension(".suppressed" + ImportLibraryFilePath.GetExtension());
			}

			FileItem OutputFile;
			if (bBuildImportLibraryOnly)
			{
				OutputFile = FileItem.GetItemByFileReference(ImportLibraryFilePath);
			}
			else
			{
				OutputFile = FileItem.GetItemByFileReference(LinkEnvironment.OutputFilePath);
			}

			List<FileItem> ProducedItems = new List<FileItem>();
			ProducedItems.Add(OutputFile);

			List<FileItem> PrerequisiteItems = new List<FileItem>();

			// Add the input files to a response file, and pass the response file on the command-line.
			List<string> InputFileNames = new List<string>();
			foreach (FileItem InputFile in LinkEnvironment.InputFiles)
			{
				InputFileNames.Add(string.Format("\"{0}\"", InputFile.AbsolutePath));
				PrerequisiteItems.Add(InputFile);
			}

			if (!bIsBuildingLibrary)
			{
				foreach (string AdditionalLibrary in LinkEnvironment.AdditionalLibraries)
				{
					InputFileNames.Add(string.Format("\"{0}\"", AdditionalLibrary));

					// If the library file name has a relative path attached (rather than relying on additional
					// lib directories), then we'll add it to our prerequisites list.  This will allow UBT to detect
					// when the binary needs to be relinked because a dependent external library has changed.
					//if( !String.IsNullOrEmpty( Path.GetDirectoryName( AdditionalLibrary ) ) )
					{
						PrerequisiteItems.Add(FileItem.GetItemByPath(AdditionalLibrary));
					}
				}
			}

			Arguments.AddRange(InputFileNames);

			// Add the output file to the command-line.
			Arguments.Add(String.Format("/OUT:\"{0}\"", OutputFile.AbsolutePath));

			if (bBuildImportLibraryOnly || (LinkEnvironment.bHasExports && !bIsBuildingLibrary))
			{
				// An export file is written to the output directory implicitly; add it to the produced items list.
				FileReference ExportFilePath = ImportLibraryFilePath.ChangeExtension(".exp");
				FileItem ExportFile = FileItem.GetItemByFileReference(ExportFilePath);
				ProducedItems.Add(ExportFile);
			}

			if (!bIsBuildingLibrary)
			{
				// There is anything to export
				if (LinkEnvironment.bHasExports
					// Shipping monolithic builds don't need exports
					&& (!((LinkEnvironment.Configuration == CppConfiguration.Shipping) /*&& (LinkEnvironment.bShouldCompileMonolithic != false)*/)))
				{
					// Write the import library to the output directory for nFringe support.
					FileItem ImportLibraryFile = FileItem.GetItemByFileReference(ImportLibraryFilePath);
					Arguments.Add(String.Format("/IMPLIB:\"{0}\"", ImportLibraryFilePath));
					ProducedItems.Add(ImportLibraryFile);
				}

				if (LinkEnvironment.bCreateDebugInfo)
				{
					// Write the PDB file to the output directory.			
					{
						FileReference PDBFilePath = FileReference.Combine(LinkEnvironment.OutputDirectory, Path.GetFileNameWithoutExtension(OutputFile.AbsolutePath) + ".pdb");
						FileItem PDBFile = FileItem.GetItemByFileReference(PDBFilePath);
						Arguments.Add(String.Format("/PDB:\"{0}\"", PDBFilePath));
						ProducedItems.Add(PDBFile);
					}

					// Write the MAP file to the output directory.			
					if (LinkEnvironment.bCreateMapFile)
					{
						FileReference MAPFilePath = FileReference.Combine(LinkEnvironment.OutputDirectory, Path.GetFileNameWithoutExtension(OutputFile.AbsolutePath) + ".map");
						FileItem MAPFile = FileItem.GetItemByFileReference(MAPFilePath);
						Arguments.Add(String.Format("/MAP:\"{0}\"", MAPFilePath));
						ProducedItems.Add(MAPFile);

						// Export a list of object file paths, so we can locate the object files referenced by the map file
						ExportObjectFilePaths(LinkEnvironment, Path.ChangeExtension(MAPFilePath.FullName, ".objpaths"));
					}
				}

				// Add the additional arguments specified by the environment.
				if (!String.IsNullOrEmpty(LinkEnvironment.AdditionalArguments))
				{
					Arguments.Add(LinkEnvironment.AdditionalArguments.Trim());
				}
			}

			// Create a response file for the linker, unless we're generating IntelliSense data
			FileReference ResponseFileName = GetResponseFileName(LinkEnvironment, OutputFile);
			if (!ProjectFileGenerator.bGenerateProjectFiles)
			{
				FileItem ResponseFile = FileItem.CreateIntermediateTextFile(ResponseFileName, String.Join(Environment.NewLine, Arguments));
				PrerequisiteItems.Add(ResponseFile);
			}

			// Create an action that invokes the linker.
			Action LinkAction = new Action(ActionType.Link);
			Actions.Add(LinkAction);
			LinkAction.CommandDescription = "Link";
			LinkAction.WorkingDirectory = UnrealBuildTool.EngineSourceDirectory;
			LinkAction.CommandPath = bIsBuildingLibrary ? EnvVars.LibraryManagerPath : EnvVars.LinkerPath;
			LinkAction.CommandArguments = String.Format("@\"{0}\"", ResponseFileName);
			LinkAction.ProducedItems.AddRange(ProducedItems);
			LinkAction.PrerequisiteItems.AddRange(PrerequisiteItems);
			LinkAction.StatusDescription = Path.GetFileName(OutputFile.AbsolutePath);

			// ensure compiler timings are captured when we execute the action.
			if (!WindowsPlatform.bAllowClangLinker && LinkEnvironment.bPrintTimingInfo)
			{
				LinkAction.bPrintDebugInfo = true;
			}

			// VS 15.3+ does not touch lib files if they do not contain any modifications, but we need to ensure the timestamps are updated to avoid repeatedly building them.
			if (bBuildImportLibraryOnly || (LinkEnvironment.bHasExports && !bIsBuildingLibrary))
			{
				LinkAction.DeleteItems.AddRange(LinkAction.ProducedItems.Where(x => x.Location.HasExtension(".lib") || x.Location.HasExtension(".exp")));
			}

			// Delete PDB files for all produced items, since incremental updates are slower than full ones.
			if (!LinkEnvironment.bUseIncrementalLinking)
			{
				LinkAction.DeleteItems.AddRange(LinkAction.ProducedItems.Where(x => x.Location.HasExtension(".pdb")));
			}


			// Tell the action that we're building an import library here and it should conditionally be
			// ignored as a prerequisite for other actions
			LinkAction.bProducesImportLibrary = bBuildImportLibraryOnly || LinkEnvironment.bIsBuildingDLL;

			// Allow remote linking.  Especially in modular builds with many small DLL files, this is almost always very efficient
			LinkAction.bCanExecuteRemotely = true;

			Log.TraceVerbose("     Linking: " + LinkAction.StatusDescription);
			Log.TraceVerbose("     Command: " + LinkAction.CommandArguments);

			return OutputFile;
		}

		private void ExportObjectFilePaths(LinkEnvironment LinkEnvironment, string FileName)
		{
			// Write the list of object file directories
			HashSet<DirectoryReference> ObjectFileDirectories = new HashSet<DirectoryReference>();
			foreach (FileItem InputFile in LinkEnvironment.InputFiles)
			{
				ObjectFileDirectories.Add(InputFile.Location.Directory);
			}
			foreach (string AdditionalLibrary in LinkEnvironment.AdditionalLibraries)
			{
				// Need to handle import libraries that are about to be built (but may not exist yet), third party libraries with relative paths in the UE4 tree, and system libraries in the system path
				FileReference AdditionalLibraryLocation = new FileReference(AdditionalLibrary);
				if (Path.IsPathRooted(AdditionalLibrary) || FileReference.Exists(AdditionalLibraryLocation))
				{
					ObjectFileDirectories.Add(AdditionalLibraryLocation.Directory);
				}
			}
			foreach (DirectoryReference LibraryPath in LinkEnvironment.LibraryPaths)
			{
				ObjectFileDirectories.Add(LibraryPath);
			}
			foreach (string LibraryPath in (Environment.GetEnvironmentVariable("LIB") ?? "").Split(new char[] { ';' }, StringSplitOptions.RemoveEmptyEntries))
			{
				ObjectFileDirectories.Add(new DirectoryReference(LibraryPath));
			}
			Directory.CreateDirectory(Path.GetDirectoryName(FileName));
			File.WriteAllLines(FileName, ObjectFileDirectories.Select(x => x.FullName).OrderBy(x => x).ToArray());
		}

		/// <summary>
		/// Gets the default include paths for the given platform.
		/// </summary>
		public static string GetVCIncludePaths(UnrealTargetPlatform Platform, WindowsCompiler Compiler)
		{
			Debug.Assert(Platform == UnrealTargetPlatform.HoloLens);


			// Also add any include paths from the INCLUDE environment variable.  MSVC is not necessarily running with an environment that
			// matches what UBT extracted from the vcvars*.bat using SetEnvironmentVariablesFromBatchFile().  We'll use the variables we
			// extracted to populate the project file's list of include paths
			// @todo projectfiles: Should we only do this for VC++ platforms?
			var IncludePaths = Environment.GetEnvironmentVariable("INCLUDE");
			if (!String.IsNullOrEmpty(IncludePaths) && !IncludePaths.EndsWith(";"))
			{
				IncludePaths += ";";
			}

			return IncludePaths;
		}

		/** Formats compiler output from Clang so that it is clickable in Visual Studio */
		protected static void ClangCompilerOutputFormatter(object sender, DataReceivedEventArgs e)
		{
			var Output = e.Data;
			if (Output == null)
			{
				return;
			}

			// @todo clang: Convert relative includes to absolute files so they'll be clickable
			Log.TraceInformation(Output);
		}

		private static DirectoryReference CurrentWindowsSdkBinDir = null;
		private static Version CurrentWindowsSdkVersion;

		public static bool InitWindowsSdkToolPath(string SdkVersion)
		{
			if (string.IsNullOrEmpty(SdkVersion))
			{
				Log.TraceError("WinSDK version is empty");
				return false;
			}

			VersionNumber OutSdkVersion;
			DirectoryReference OutSdkDir;

			if (!WindowsPlatform.TryGetWindowsSdkDir(SdkVersion, out OutSdkVersion, out OutSdkDir))
			{
				Log.TraceError("Failed to find WinSDK " + SdkVersion);
				return false;
			}

			DirectoryReference WindowsSdkBinDir = DirectoryReference.Combine(OutSdkDir, "bin", OutSdkVersion.ToString(), Environment.Is64BitProcess ? "x64" : "x86");

			if(!DirectoryReference.Exists(WindowsSdkBinDir))
			{
				Log.TraceError("WinSDK " + SdkVersion + " doesn't exit");
				return false;
			}

			CurrentWindowsSdkVersion = new Version(OutSdkVersion.ToString());
			CurrentWindowsSdkBinDir = WindowsSdkBinDir;
			return true;
		}

		public static FileReference GetWindowsSdkToolPath(string ToolName)
		{
			FileReference file = FileReference.Combine(CurrentWindowsSdkBinDir, ToolName);

			if(!FileReference.Exists(file))
			{
				return null;
			}

			return file;
		}

		public static Version GetCurrentWindowsSdkVersion()
		{
			return CurrentWindowsSdkVersion;
		}

		public override string GetSDKVersion()
		{
			return CurrentWindowsSdkVersion.ToString();
		}

		public override void ModifyBuildProducts(ReadOnlyTargetRules Target, UEBuildBinary Binary, List<string> Libraries, List<UEBuildBundleResource> BundleResources, Dictionary<FileReference, BuildProductType> BuildProducts)
		{
			DirectoryReference HoloLensBinaryDirectory = Binary.OutputFilePath.Directory;

			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, "AppxManifest_"+ Target.Architecture + ".xml"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, "resources_"+ Target.Architecture + ".pri"), BuildProductType.BuildResource);
			if (Target.Configuration == UnrealTargetConfiguration.Development)
			{
				AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Name + Target.Architecture + ".exe"), BuildProductType.Executable);
				AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Name + Target.Architecture + ".pdb"), BuildProductType.SymbolFile);
			}
			else
			{
				AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Name + "-HoloLens-" + Target.Configuration + Target.Architecture + ".exe"), BuildProductType.Executable);
				AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Name + "-HoloLens-" + Target.Configuration + Target.Architecture + ".pdb"), BuildProductType.SymbolFile);
			}
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Name + "-HoloLens-" + Target.Configuration + Target.Architecture + ".target"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Architecture + "\\Resources\\Logo.png"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Architecture + "\\Resources\\resources.resw"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Architecture + "\\Resources\\SmallLogo.png"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Architecture + "\\Resources\\SplashScreen.png"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Architecture + "\\Resources\\WideLogo.png"), BuildProductType.BuildResource);
			AddBuildProductSafe(BuildProducts, FileReference.Combine(HoloLensBinaryDirectory, Target.Architecture + "\\Resources\\en\\resources.resw"), BuildProductType.BuildResource);
		}

		private void AddBuildProductSafe(Dictionary<FileReference, BuildProductType> BuildProducts, FileReference FileToAdd, BuildProductType ProductType)
		{
			if (!BuildProducts.ContainsKey(FileToAdd))
			{
				BuildProducts.Add(FileToAdd, ProductType);
			}
		}

	};
}
