// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Interface for an action that compiles C++ source code
	/// </summary>
	interface ICppCompileAction : IAction
	{
		/// <summary>
		/// Path to the compiled module interface file
		/// </summary>
		FileItem CompiledModuleInterfaceFile { get; }
	}

	/// <summary>
	/// Serializer which creates a portable object file and allows caching it
	/// </summary>
	class VCCompileAction : ICppCompileAction
	{
		/// <summary>
		/// Path to the compiler
		/// </summary>
		public FileItem CompilerExe { get; }

		/// <summary>
		/// The type of compiler being used
		/// </summary>
		public WindowsCompiler CompilerType { get; }

		/// <summary>
		/// Source file to compile
		/// </summary>
		public FileItem SourceFile { get; set; }

		/// <summary>
		/// The object file to output
		/// </summary>
		public FileItem ObjectFile { get; set; }

		/// <summary>
		/// The output preprocessed file
		/// </summary>
		public FileItem PreprocessedFile { get; set; }

		/// <summary>
		/// The dependency list file
		/// </summary>
		public FileItem DependencyListFile { get; set; }

		/// <summary>
		/// Compiled module interface
		/// </summary>
		public FileItem CompiledModuleInterfaceFile { get; set; }

		/// <summary>
		/// For C++ source files, specifies a timing file used to track timing information.
		/// </summary>
		public FileItem TimingFile { get; set; }

		/// <summary>
		/// Response file for the compiler
		/// </summary>
		public FileItem ResponseFile { get; set; }

		/// <summary>
		/// The precompiled header file
		/// </summary>
		public FileItem CreatePchFile { get; set; }

		/// <summary>
		/// The precompiled header file
		/// </summary>
		public FileItem UsingPchFile { get; set; }

		/// <summary>
		/// The header which matches the PCH
		/// </summary>
		public FileItem PchThroughHeaderFile { get; set; }

		/// <summary>
		/// List of include paths
		/// </summary>
		public List<DirectoryReference> IncludePaths { get; } = new List<DirectoryReference>();

		/// <summary>
		/// List of system include paths
		/// </summary>
		public List<DirectoryReference> SystemIncludePaths { get; } = new List<DirectoryReference>();

		/// <summary>
		/// List of macro definitions
		/// </summary>
		public List<string> Definitions { get; } = new List<string>();

		/// <summary>
		/// List of force included files
		/// </summary>
		public List<FileItem> ForceIncludeFiles = new List<FileItem>();

		/// <summary>
		/// Every file this action depends on.  These files need to exist and be up to date in order for this action to even be considered
		/// </summary>
		public List<FileItem> AdditionalPrerequisiteItems { get; } = new List<FileItem>();

		/// <summary>
		/// The files that this action produces after completing
		/// </summary>
		public List<FileItem> AdditionalProducedItems { get; } = new List<FileItem>();

		/// <summary>
		/// Arguments to pass to the compiler
		/// </summary>
		public List<string> Arguments { get; } = new List<string>();

		/// <summary>
		/// Whether to show included files to the console
		/// </summary>
		public bool bShowIncludes { get; set; }

		#region Public IAction implementation

		/// <summary>
		/// Items that should be deleted before running this action
		/// </summary>
		public List<FileItem> DeleteItems { get; } = new List<FileItem>();

		/// <inheritdoc/>
		public bool bCanExecuteRemotely { get; set; }

		/// <inheritdoc/>
		public bool bCanExecuteRemotelyWithSNDBS { get; set; }

		#endregion

		#region Implementation of IAction

		ActionType IAction.ActionType => ActionType.Compile;
		IEnumerable<FileItem> IAction.DeleteItems => DeleteItems;
		public DirectoryReference WorkingDirectory => UnrealBuildTool.EngineSourceDirectory;
		string IAction.CommandDescription => "Compile";
		bool IAction.bIsGCCCompiler => false;
		bool IAction.bProducesImportLibrary => false;
		string IAction.StatusDescription => (SourceFile == null) ? "Compiling" : SourceFile.Location.GetFileName();
		bool IAction.bShouldOutputStatusDescription => CompilerType == WindowsCompiler.Clang;

		/// <inheritdoc/>
		IEnumerable<FileItem> IAction.PrerequisiteItems
		{
			get
			{
				if (SourceFile != null)
				{
					yield return SourceFile;
				}
				if (UsingPchFile != null)
				{
					yield return UsingPchFile;
				}
				foreach(FileItem AdditionalPrerequisiteItem in AdditionalPrerequisiteItems)
				{
					yield return AdditionalPrerequisiteItem;
				}
			}
		}

		/// <inheritdoc/>
		IEnumerable<FileItem> IAction.ProducedItems
		{
			get
			{
				if (ObjectFile != null)
				{
					yield return ObjectFile;
				}
				if (PreprocessedFile != null)
				{
					yield return PreprocessedFile;
				}
				if (DependencyListFile != null)
				{
					yield return DependencyListFile;
				}
				if (TimingFile != null)
				{
					yield return TimingFile;
				}
				if (CreatePchFile != null)
				{
					yield return CreatePchFile;
				}
				foreach (FileItem AdditionalProducedItem in AdditionalProducedItems)
				{
					yield return AdditionalProducedItem;
				}
			}
		}

		/// <inheritdoc/>
		FileReference IAction.CommandPath
		{
			get
			{
				if (DependencyListFile != null)
				{
					return FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build", "Windows", "cl-filter", "cl-filter.exe");
				}
				else
				{
					return CompilerExe.Location;
				}
			}
		}

		/// <inheritdoc/>
		string IAction.CommandArguments
		{
			get
			{
				if (DependencyListFile != null)
				{
					return GetClFilterArguments();
				}
				else
				{
					return GetClArguments();
				}
			}
		}

		#endregion

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Environment">Compiler executable</param>
		public VCCompileAction(VCEnvironment Environment)
		{
			this.CompilerExe = FileItem.GetItemByFileReference(Environment.CompilerPath);
			this.CompilerType = Environment.Compiler;
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		/// <param name="InAction">Action to copy from</param>
		public VCCompileAction(VCCompileAction InAction)
		{
			CompilerExe = InAction.CompilerExe;
			CompilerType = InAction.CompilerType;
			SourceFile = InAction.SourceFile;
			ObjectFile = InAction.ObjectFile;
			PreprocessedFile = InAction.PreprocessedFile;
			DependencyListFile = InAction.DependencyListFile;
			CompiledModuleInterfaceFile = InAction.CompiledModuleInterfaceFile;
			TimingFile = InAction.TimingFile;
			ResponseFile = InAction.ResponseFile;
			CreatePchFile = InAction.CreatePchFile;
			UsingPchFile = InAction.UsingPchFile;
			PchThroughHeaderFile = InAction.PchThroughHeaderFile;
			IncludePaths = new List<DirectoryReference>(InAction.IncludePaths);
			SystemIncludePaths = new List<DirectoryReference>(InAction.SystemIncludePaths);
			Definitions = new List<string>(InAction.Definitions);
			ForceIncludeFiles = new List<FileItem>(InAction.ForceIncludeFiles);
			Arguments = new List<string>(InAction.Arguments);
			bShowIncludes = InAction.bShowIncludes;

			AdditionalPrerequisiteItems = new List<FileItem>(InAction.AdditionalPrerequisiteItems);
			AdditionalProducedItems = new List<FileItem>(InAction.AdditionalProducedItems);
			DeleteItems = new List<FileItem>(InAction.DeleteItems);
		}

		/// <summary>
		/// Serialize a cache handler from an archive
		/// </summary>
		/// <param name="Reader">Reader to serialize from</param>
		public VCCompileAction(BinaryArchiveReader Reader)
		{
			CompilerExe = Reader.ReadFileItem();
			CompilerType = (WindowsCompiler)Reader.ReadInt();
			SourceFile = Reader.ReadFileItem();
			ObjectFile = Reader.ReadFileItem();
			PreprocessedFile = Reader.ReadFileItem();
			DependencyListFile = Reader.ReadFileItem();
			CompiledModuleInterfaceFile = Reader.ReadFileItem();
			TimingFile = Reader.ReadFileItem();
			ResponseFile = Reader.ReadFileItem();
			CreatePchFile = Reader.ReadFileItem();
			UsingPchFile = Reader.ReadFileItem();
			PchThroughHeaderFile = Reader.ReadFileItem();
			IncludePaths = Reader.ReadList(() => Reader.ReadDirectoryReference());
			SystemIncludePaths = Reader.ReadList(() => Reader.ReadDirectoryReference());
			Definitions = Reader.ReadList(() => Reader.ReadString());
			ForceIncludeFiles = Reader.ReadList(() => Reader.ReadFileItem());
			Arguments = Reader.ReadList(() => Reader.ReadString());
			bShowIncludes = Reader.ReadBool();

			AdditionalPrerequisiteItems = Reader.ReadList(() => Reader.ReadFileItem());
			AdditionalProducedItems = Reader.ReadList(() => Reader.ReadFileItem());
			DeleteItems = Reader.ReadList(() => Reader.ReadFileItem());
		}

		/// <inheritdoc/>
		public void Write(BinaryArchiveWriter Writer)
		{
			Writer.WriteFileItem(CompilerExe);
			Writer.WriteInt((int)CompilerType);
			Writer.WriteFileItem(SourceFile);
			Writer.WriteFileItem(ObjectFile);
			Writer.WriteFileItem(PreprocessedFile);
			Writer.WriteFileItem(DependencyListFile);
			Writer.WriteFileItem(CompiledModuleInterfaceFile);
			Writer.WriteFileItem(TimingFile);
			Writer.WriteFileItem(ResponseFile);
			Writer.WriteFileItem(CreatePchFile);
			Writer.WriteFileItem(UsingPchFile);
			Writer.WriteFileItem(PchThroughHeaderFile);
			Writer.WriteList(IncludePaths, Item => Writer.WriteDirectoryReference(Item));
			Writer.WriteList(SystemIncludePaths, Item => Writer.WriteDirectoryReference(Item));
			Writer.WriteList(Definitions, Item => Writer.WriteString(Item));
			Writer.WriteList(ForceIncludeFiles, Item => Writer.WriteFileItem(Item));
			Writer.WriteList(Arguments, Item => Writer.WriteString(Item));
			Writer.WriteBool(bShowIncludes);

			Writer.WriteList(AdditionalPrerequisiteItems, Item => Writer.WriteFileItem(Item));
			Writer.WriteList(AdditionalProducedItems, Item => Writer.WriteFileItem(Item));
			Writer.WriteList(DeleteItems, Item => Writer.WriteFileItem(Item));
		}

		/// <summary>
		/// Writes the response file with the action's arguments
		/// </summary>
		/// <param name="Graph">The graph builder</param>
		public void WriteResponseFile(IActionGraphBuilder Graph)
		{
			if (ResponseFile != null)
			{
				Graph.CreateIntermediateTextFile(ResponseFile, GetCompilerArguments());
			}
		}

		public List<string> GetCompilerArguments()
		{
			List<string> Arguments = new List<string>();

			if (SourceFile != null)
			{
				Arguments.Add(Utils.MakePathSafeToUseWithCommandLine(SourceFile.FullName));
			}

			foreach (DirectoryReference IncludePath in IncludePaths)
			{
				VCToolChain.AddIncludePath(Arguments, IncludePath, CompilerType, PreprocessedFile != null);
			}

			foreach (DirectoryReference SystemIncludePath in SystemIncludePaths)
			{
				VCToolChain.AddSystemIncludePath(Arguments, SystemIncludePath, CompilerType, PreprocessedFile != null);
			}

			foreach (string Definition in Definitions)
			{
				// Escape all quotation marks so that they get properly passed with the command line.
				string DefinitionArgument = Definition.Contains("\"") ? Definition.Replace("\"", "\\\"") : Definition;
				VCToolChain.AddDefinition(Arguments, DefinitionArgument);
			}

			foreach (FileItem ForceIncludeFile in ForceIncludeFiles)
			{
				Arguments.Add(String.Format("/FI\"{0}\"", ForceIncludeFile.Location));
			}

			if (CreatePchFile != null)
			{
				Arguments.Add(String.Format("/Yc\"{0}\"", PchThroughHeaderFile.FullName));
				Arguments.Add(String.Format("/Fp\"{0}\"", CreatePchFile.FullName));
			}
			if (UsingPchFile != null)
			{
				Arguments.Add(String.Format("/Yu\"{0}\"", PchThroughHeaderFile.FullName));
				Arguments.Add(String.Format("/Fp\"{0}\"", UsingPchFile.FullName));
			}

			if (PreprocessedFile != null)
			{
				Arguments.Add("/P"); // Preprocess
				Arguments.Add("/C"); // Preserve comments when preprocessing
				Arguments.Add(String.Format("/Fi\"{0}\"", PreprocessedFile)); // Preprocess to a file
			}

			if (ObjectFile != null)
			{
				Arguments.Add(String.Format("/Fo\"{0}\"", ObjectFile.AbsolutePath));
			}

			Arguments.AddRange(this.Arguments);
			return Arguments;
		}

		string GetClArguments()
		{
			if (ResponseFile == null)
			{
				return String.Join(" ", Arguments);
			}
			else
			{
				return String.Format("@\"{0}\"", ResponseFile.Location);
			}
		}

		string GetClFilterArguments()
		{
			List<string> Arguments = new List<string>();
			Arguments.Add(String.Format("-dependencies={0}", Utils.MakePathSafeToUseWithCommandLine(DependencyListFile.Location)));

			if (TimingFile != null)
			{
				Arguments.Add(String.Format("-timing={0}", Utils.MakePathSafeToUseWithCommandLine(TimingFile.Location)));
			}
			if (bShowIncludes)
			{
				Arguments.Add("-showincludes");
			}

			Arguments.Add(String.Format("-compiler={0}", Utils.MakePathSafeToUseWithCommandLine(CompilerExe.AbsolutePath)));
			Arguments.Add("--");
			Arguments.Add(Utils.MakePathSafeToUseWithCommandLine(CompilerExe.AbsolutePath));
			Arguments.Add(GetClArguments());
			Arguments.Add("/showIncludes");

			return String.Join(" ", Arguments);
		}
	}

	/// <summary>
	/// Serializer for <see cref="VCCompileAction"/> instances
	/// </summary>
	class VCCompileActionSerializer : ActionSerializerBase<VCCompileAction>
	{
		/// <inheritdoc/>
		public override VCCompileAction Read(BinaryArchiveReader Reader)
		{
			return new VCCompileAction(Reader);
		}

		/// <inheritdoc/>
		public override void Write(BinaryArchiveWriter Writer, VCCompileAction Action)
		{
			Action.Write(Writer);
		}
	}
}
