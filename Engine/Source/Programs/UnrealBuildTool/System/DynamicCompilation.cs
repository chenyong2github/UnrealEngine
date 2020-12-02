// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.CodeDom.Compiler;
using Microsoft.CSharp;
using System.Reflection;
using System.Diagnostics;
using Tools.DotNETCommon;

using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Emit;
using System.Reflection.Metadata;
using Microsoft.CodeAnalysis.Text;

namespace UnrealBuildTool
{
	/// <summary>
	/// Methods for dynamically compiling C# source files
	/// </summary>
	public class DynamicCompilation
	{
		/// <summary>
		/// Checks to see if the assembly needs compilation
		/// </summary>
		/// <param name="SourceFiles">Set of source files</param>
		/// <param name="AssemblyManifestFilePath">File containing information about this assembly, like which source files it was built with and engine version</param>
		/// <param name="OutputAssemblyPath">Output path for the assembly</param>
		/// <returns>True if the assembly needs to be built</returns>
		private static bool RequiresCompilation(HashSet<FileReference> SourceFiles, FileReference AssemblyManifestFilePath, FileReference OutputAssemblyPath)
		{
			// HACK: We cannot do this without breaking marketplace plugins. Need to distribute separate rules assemblies for those.
//			// Do not compile the file if it's installed
//			if (UnrealBuildTool.IsFileInstalled(OutputAssemblyPath))
//			{
//				Log.TraceLog("Skipping {0}: File is installed", OutputAssemblyPath);
//				return false;
//			}

			// Check to see if we already have a compiled assembly file on disk
			FileItem OutputAssemblyInfo = FileItem.GetItemByFileReference(OutputAssemblyPath);
			if (!OutputAssemblyInfo.Exists)
			{
				Log.TraceLog("Compiling {0}: Assembly does not exist", OutputAssemblyPath);
				return true;
			}

			// Check the time stamp of the UnrealBuildTool.exe file.  If Unreal Build Tool was compiled more
			// recently than the dynamically-compiled assembly, then we'll always recompile it.  This is
			// because Unreal Build Tool's code may have changed in such a way that invalidate these
			// previously-compiled assembly files.
			FileItem ExecutableItem = FileItem.GetItemByFileReference(UnrealBuildTool.GetUBTPath());
			if (ExecutableItem.LastWriteTimeUtc > OutputAssemblyInfo.LastWriteTimeUtc)
			{
				Log.TraceLog("Compiling {0}: {1} is newer", OutputAssemblyPath, ExecutableItem.Name);
				return true;
			}


			// Make sure we have a manifest of source files used to compile the output assembly.  If it doesn't exist
			// for some reason (not an expected case) then we'll need to recompile.
			FileItem AssemblySourceListFile = FileItem.GetItemByFileReference(AssemblyManifestFilePath);
			if (!AssemblySourceListFile.Exists)
			{
				Log.TraceLog("Compiling {0}: Missing source file list ({1})", OutputAssemblyPath, AssemblyManifestFilePath);
				return true;
			}

			JsonObject Manifest = JsonObject.Read(AssemblyManifestFilePath);

			// check if the engine version is different
			string EngineVersionManifest = Manifest.GetStringField("EngineVersion");
			string EngineVersionCurrent = FormatVersionNumber(ReadOnlyBuildVersion.Current);
			if (EngineVersionManifest != EngineVersionCurrent)
			{
				Log.TraceLog("Compiling {0}: Engine Version changed from {1} to {2}", OutputAssemblyPath, EngineVersionManifest, EngineVersionCurrent);
				return true;
			}


			// Make sure the source files we're compiling are the same as the source files that were compiled
			// for the assembly that we want to load
			HashSet<FileItem> CurrentSourceFileItems = new HashSet<FileItem>();
			foreach(string Line in Manifest.GetStringArrayField("SourceFiles"))
			{
				CurrentSourceFileItems.Add(FileItem.GetItemByPath(Line));
			}

			// Get the new source files
			HashSet<FileItem> SourceFileItems = new HashSet<FileItem>();
			foreach(FileReference SourceFile in SourceFiles)
			{
				SourceFileItems.Add(FileItem.GetItemByFileReference(SourceFile));
			}

			// Check if there are any differences between the sets
			foreach(FileItem CurrentSourceFileItem in CurrentSourceFileItems)
			{
				if(!SourceFileItems.Contains(CurrentSourceFileItem))
				{
					Log.TraceLog("Compiling {0}: Removed source file ({1})", OutputAssemblyPath, CurrentSourceFileItem);
					return true;
				}
			}
			foreach(FileItem SourceFileItem in SourceFileItems)
			{
				if(!CurrentSourceFileItems.Contains(SourceFileItem))
				{
					Log.TraceLog("Compiling {0}: Added source file ({1})", OutputAssemblyPath, SourceFileItem);
					return true;
				}
			}

			// Check if any of the timestamps are newer
			foreach(FileItem SourceFileItem in SourceFileItems)
			{
				if(SourceFileItem.LastWriteTimeUtc > OutputAssemblyInfo.LastWriteTimeUtc)
				{
					Log.TraceLog("Compiling {0}: {1} is newer", OutputAssemblyPath, SourceFileItem);
					return true;
				}
			}

			return false;
		}

		private static void LogDiagnostics(IEnumerable<Diagnostic> Diagnostics)
		{
			foreach (Diagnostic Diag in Diagnostics)
			{
				switch (Diag.Severity)
				{
					case DiagnosticSeverity.Error: 
					{
						Log.TraceError(Diag.ToString()); 
						break;
					}
					case DiagnosticSeverity.Hidden: 
					{
						break;
					}
					case DiagnosticSeverity.Warning: 
					{
						Log.TraceWarning(Diag.ToString()); 
						break;
					}
					case DiagnosticSeverity.Info: 
					{
						Log.TraceInformation(Diag.ToString()); 
						break;
					}
				}
			}
		}

		private static Assembly CompileAssembly(FileReference OutputAssemblyPath, HashSet<FileReference> SourceFileNames, List<string> ReferencedAssembies, List<string> PreprocessorDefines = null, bool TreatWarningsAsErrors = false)
		{
			CSharpParseOptions ParseOptions = new CSharpParseOptions(
				languageVersion:LanguageVersion.Latest, 
				kind:SourceCodeKind.Regular,
				preprocessorSymbols:PreprocessorDefines
			);

			List<SyntaxTree> SyntaxTrees = new List<SyntaxTree>();

			foreach (FileReference SourceFileName in SourceFileNames)
			{
				SourceText Source = SourceText.From(File.ReadAllText(SourceFileName.FullName));
				SyntaxTree Tree = CSharpSyntaxTree.ParseText(Source, ParseOptions, SourceFileName.FullName);

				IEnumerable<Diagnostic> Diagnostics = Tree.GetDiagnostics();
				if (Diagnostics.Any())
				{
					Log.TraceWarning($"Errors generated while parsing '{SourceFileName.FullName}'");
					LogDiagnostics(Tree.GetDiagnostics());
					return null;
				}

				SyntaxTrees.Add(Tree);
			}

			// Create the output directory if it doesn't exist already
			DirectoryInfo DirInfo = new DirectoryInfo(OutputAssemblyPath.Directory.FullName);
			if (!DirInfo.Exists)
			{
				try
				{
					DirInfo.Create();
				}
				catch (Exception Ex)
				{
					throw new BuildException(Ex, "Unable to create directory '{0}' for intermediate assemblies (Exception: {1})", OutputAssemblyPath, Ex.Message);
				}
			}

			List<MetadataReference> MetadataReferences = new List<MetadataReference>();
			if (ReferencedAssembies != null)
			{
				foreach (string Reference in ReferencedAssembies)
				{
					MetadataReferences.Add(MetadataReference.CreateFromFile(Reference));
				}
			}

			MetadataReferences.Add(MetadataReference.CreateFromFile(typeof(object).Assembly.Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Runtime").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Collections").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.IO").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.IO.FileSystem").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Linq").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Console").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Runtime.Extensions").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("netstandard").Location));
			
			// process start dependencies
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.ComponentModel.Primitives").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Diagnostics.Process").Location));
			
			// registry access
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("Microsoft.Win32.Registry").Location));

			// RNGCryptoServiceProvider, used to generate random hex bytes
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Security.Cryptography.Algorithms").Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(Assembly.Load("System.Security.Cryptography.Csp").Location));

			MetadataReferences.Add(MetadataReference.CreateFromFile(typeof(UnrealBuildTool).Assembly.Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(typeof(FileReference).Assembly.Location));
			MetadataReferences.Add(MetadataReference.CreateFromFile(typeof(UEBuildPlatformSDK).Assembly.Location));

			CSharpCompilationOptions CompilationOptions = new CSharpCompilationOptions(
				outputKind:OutputKind.DynamicallyLinkedLibrary,
#if DEBUG
				optimizationLevel: OptimizationLevel.Debug,
#else
				// Optimize the managed code in Development
				optimizationLevel: OptimizationLevel.Release,
#endif
				warningLevel:4,
				assemblyIdentityComparer:DesktopAssemblyIdentityComparer.Default,
				reportSuppressedDiagnostics:true
			);

			CSharpCompilation Compilation = CSharpCompilation.Create(
				assemblyName:OutputAssemblyPath.GetFileNameWithoutAnyExtensions(),
				syntaxTrees:SyntaxTrees,
				references:MetadataReferences,
				options:CompilationOptions
				);

			using (FileStream AssemblyStream = FileReference.Open(OutputAssemblyPath, FileMode.Create))
			{
				EmitOptions EmitOptions = new EmitOptions(
					includePrivateMembers:true
				);

				EmitResult Result = Compilation.Emit(
					peStream:AssemblyStream,
					options:EmitOptions);

				if (!Result.Success)
				{
					LogDiagnostics(Result.Diagnostics);
					return null;
				}
			}

			return Assembly.LoadFile(OutputAssemblyPath.FullName);
		}

		/// <summary>
		/// Dynamically compiles an assembly for the specified source file and loads that assembly into the application's
		/// current domain.  If an assembly has already been compiled and is not out of date, then it will be loaded and
		/// no compilation is necessary.
		/// </summary>
		/// <param name="OutputAssemblyPath">Full path to the assembly to be created</param>
		/// <param name="SourceFileNames">List of source file name</param>
		/// <param name="ReferencedAssembies"></param>
		/// <param name="PreprocessorDefines"></param>
		/// <param name="DoNotCompile"></param>
		/// <param name="TreatWarningsAsErrors"></param>
		/// <returns>The assembly that was loaded</returns>
		public static Assembly CompileAndLoadAssembly(FileReference OutputAssemblyPath, HashSet<FileReference> SourceFileNames, List<string> ReferencedAssembies = null, List<string> PreprocessorDefines = null, bool DoNotCompile = false, bool TreatWarningsAsErrors = false)
		{
			// Check to see if the resulting assembly is compiled and up to date
			FileReference AssemblyManifestFilePath = FileReference.Combine(OutputAssemblyPath.Directory, Path.GetFileNameWithoutExtension(OutputAssemblyPath.FullName) + "Manifest.json");

			bool bNeedsCompilation = false;
			if (!DoNotCompile)
			{
				bNeedsCompilation = RequiresCompilation(SourceFileNames, AssemblyManifestFilePath, OutputAssemblyPath);
			}

			// Load the assembly to ensure it is correct
			Assembly CompiledAssembly = null;
			if (!bNeedsCompilation)
			{
				try
				{
					// Load the previously-compiled assembly from disk
					CompiledAssembly = Assembly.LoadFile(OutputAssemblyPath.FullName);
				}
				catch (FileLoadException Ex)
				{
					Log.TraceInformation(String.Format("Unable to load the previously-compiled assembly file '{0}'.  Unreal Build Tool will try to recompile this assembly now.  (Exception: {1})", OutputAssemblyPath, Ex.Message));
					bNeedsCompilation = true;
				}
				catch (BadImageFormatException Ex)
				{
					Log.TraceInformation(String.Format("Compiled assembly file '{0}' appears to be for a newer CLR version or is otherwise invalid.  Unreal Build Tool will try to recompile this assembly now.  (Exception: {1})", OutputAssemblyPath, Ex.Message));
					bNeedsCompilation = true;
				}
				catch (FileNotFoundException)
			    {
				    throw new BuildException("Precompiled rules assembly '{0}' does not exist.", OutputAssemblyPath);
			    }
				catch (Exception Ex)
				{
					throw new BuildException(Ex, "Error while loading previously-compiled assembly file '{0}'.  (Exception: {1})", OutputAssemblyPath, Ex.Message);
				}
			}

			// Compile the assembly if me
			if (bNeedsCompilation)
			{
				using(Timeline.ScopeEvent(String.Format("Compiling rules assembly ({0})", OutputAssemblyPath.GetFileName())))
				{
					CompiledAssembly = CompileAssembly(OutputAssemblyPath, SourceFileNames, ReferencedAssembies, PreprocessorDefines, TreatWarningsAsErrors);
				}

				using (JsonWriter Writer = new JsonWriter(AssemblyManifestFilePath))
				{
					ReadOnlyBuildVersion Version = ReadOnlyBuildVersion.Current;

					Writer.WriteObjectStart();
					// Save out a list of all the source files we compiled.  This is so that we can tell if whole files were added or removed
					// since the previous time we compiled the assembly.  In that case, we'll always want to recompile it!
					Writer.WriteStringArrayField("SourceFiles", SourceFileNames.Select(x => x.FullName));
					Writer.WriteValue("EngineVersion", FormatVersionNumber(Version));
					Writer.WriteObjectEnd();
				}
			}

			return CompiledAssembly;
		}

		private static string FormatVersionNumber(ReadOnlyBuildVersion Version)
		{
			return string.Format("{0}.{1}.{2}", Version.MajorVersion, Version.MinorVersion, Version.PatchVersion);
		}
	}
}
