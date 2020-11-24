using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
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
		/// Every file this action depends on.  These files need to exist and be up to date in order for this action to even be considered
		/// </summary>
		public List<FileItem> PrerequisiteItems { get; } = new List<FileItem>();

		/// <summary>
		/// The files that this action produces after completing
		/// </summary>
		public List<FileItem> ProducedItems { get; } = new List<FileItem>();

		/// <summary>
		/// Items that should be deleted before running this action
		/// </summary>
		public List<FileItem> DeleteItems { get; } = new List<FileItem>();

		/// <inheritdoc/>
		public FileReference CommandPath { get; set; }

		/// <inheritdoc/>
		public string CommandArguments { get; set; }

		/// <inheritdoc/>
		public string StatusDescription { get; set; }
		
		/// <inheritdoc/>
		public bool bShouldOutputStatusDescription { get; set; }

		#region Implemenation of IAction
		ActionType IAction.ActionType => ActionType.Compile;
		IEnumerable<FileItem> IAction.PrerequisiteItems => PrerequisiteItems;
		IEnumerable<FileItem> IAction.ProducedItems => ProducedItems;
		IEnumerable<FileItem> IAction.DeleteItems => DeleteItems;
		public DirectoryReference WorkingDirectory => UnrealBuildTool.EngineSourceDirectory;
		string IAction.CommandDescription => "Compile";
		public bool bCanExecuteRemotely { get; set; }
		public bool bCanExecuteRemotelyWithSNDBS { get; set; }
		bool IAction.bIsGCCCompiler => false;
		bool IAction.bProducesImportLibrary => false;
		#endregion

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Environment">Compiler executable</param>
		public VCCompileAction(VCEnvironment Environment)
		{
			this.CommandPath = Environment.CompilerPath;
		}

		/// <summary>
		/// Serialize a cache handler from an archive
		/// </summary>
		/// <param name="Reader">Reader to serialize from</param>
		public VCCompileAction(BinaryArchiveReader Reader)
		{
			DependencyListFile = Reader.ReadFileItem();
			CompiledModuleInterfaceFile = Reader.ReadFileItem();
			TimingFile = Reader.ReadFileItem();
			PrerequisiteItems = Reader.ReadList(() => Reader.ReadFileItem());
			ProducedItems = Reader.ReadList(() => Reader.ReadFileItem());
			DeleteItems = Reader.ReadList(() => Reader.ReadFileItem());
			CommandPath = Reader.ReadFileReference();
			CommandArguments = Reader.ReadString();
			StatusDescription = Reader.ReadString();
			bShouldOutputStatusDescription = Reader.ReadBool();
		}

		/// <inheritdoc/>
		public void Write(BinaryArchiveWriter Writer)
		{
			Writer.WriteFileItem(DependencyListFile);
			Writer.WriteFileItem(CompiledModuleInterfaceFile);
			Writer.WriteFileItem(TimingFile);
			Writer.WriteList(PrerequisiteItems, Item => Writer.WriteFileItem(Item));
			Writer.WriteList(ProducedItems, Item => Writer.WriteFileItem(Item));
			Writer.WriteList(DeleteItems, Item => Writer.WriteFileItem(Item));
			Writer.WriteFileReference(CommandPath);
			Writer.WriteString(CommandArguments);
			Writer.WriteString(StatusDescription);
			Writer.WriteBool(bShouldOutputStatusDescription);
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
