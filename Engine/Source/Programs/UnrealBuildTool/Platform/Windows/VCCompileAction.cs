using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Serializer which creates a portable object file and allows caching it
	/// </summary>
	class VCCompileAction : Action
	{
		/// <summary>
		/// Compiler executable
		/// </summary>
		public FileItem Compiler { get; }

		/// <summary>
		/// For C++ source files, specifies a timing file used to track timing information.
		/// </summary>
		public FileItem TimingFile { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Environment">Compiler executable</param>
		public VCCompileAction(VCEnvironment Environment)
			: base(ActionType.Compile)
		{
			this.Compiler = FileItem.GetItemByFileReference(Environment.CompilerPath);
		}

		/// <summary>
		/// Serialize a cache handler from an archive
		/// </summary>
		/// <param name="Reader">Reader to serialize from</param>
		public VCCompileAction(BinaryArchiveReader Reader)
			: base(Reader)
		{
			Compiler = Reader.ReadFileItem();
			TimingFile = Reader.ReadFileItem();
		}

		/// <inheritdoc/>
		public new void Write(BinaryArchiveWriter Writer)
		{
			base.Write(Writer);

			Writer.WriteFileItem(Compiler);
			Writer.WriteFileItem(TimingFile);
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
