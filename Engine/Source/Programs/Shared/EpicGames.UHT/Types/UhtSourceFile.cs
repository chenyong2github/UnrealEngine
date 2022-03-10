// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System.IO;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents a source file in UHT
	/// </summary>
	public class UhtSourceFile : IUhtMessageSite, IUhtMessageSource
	{
		private UhtSimpleMessageSite MessageSite;
		private UhtSourceFragment SourceFragment;

		/// <summary>
		/// The session associated with the source
		/// </summary>
		public readonly UhtSession Session;

		/// <summary>
		/// The full file path
		/// </summary>
		public readonly string FilePath;

		/// <summary>
		/// The file name
		/// </summary>
		public readonly string FileName;

		/// <summary>
		/// The contents of the source file
		/// </summary>
		public StringView Data { get => SourceFragment.Data; }

		/// <summary>
		/// If this source is from a fragment, the original source file
		/// </summary>
		public UhtSourceFile? FragmentSourceFile { get => SourceFragment.SourceFile; }

		/// <summary>
		/// If this source is from a fragment, the starting line of the fragment
		/// </summary>
		public int FragmentLineNumber { get => SourceFragment.LineNumber; }

		/// <summary>
		/// Full file path of the source file
		/// </summary>
		public string FullFilePath { get => SourceFragment.FilePath; }

		#region IUHTMessageSite implementation
		/// <inheritdoc/>
		public IUhtMessageSession MessageSession => this.MessageSite.MessageSession;

		/// <inheritdoc/>
		public IUhtMessageSource? MessageSource => this.MessageSite.MessageSource;

		/// <inheritdoc/>
		public IUhtMessageLineNumber? MessageLineNumber => null;
		#endregion

		#region IUHTMessageSource implementation
		/// <inheritdoc/>
		public string MessageFilePath { get => this.FilePath; }

		/// <inheritdoc/>
		public string MessageFullFilePath { get => this.FullFilePath; }

		/// <inheritdoc/>
		public bool bMessageIsFragment { get => this.FragmentSourceFile != null; }

		/// <inheritdoc/>
		public string MessageFragmentFilePath { get => this.FragmentSourceFile != null ? this.FragmentSourceFile.MessageFilePath : ""; }

		/// <inheritdoc/>
		public string MessageFragmentFullFilePath { get => this.FragmentSourceFile != null ? this.FragmentSourceFile.MessageFullFilePath : ""; }

		/// <inheritdoc/>
		public int MessageFragmentLineNumber { get => this.FragmentLineNumber; }
		#endregion

		/// <summary>
		/// Construct a new instance of a source file
		/// </summary>
		/// <param name="Session">The owning session</param>
		/// <param name="FilePath">The full file path</param>
		public UhtSourceFile(UhtSession Session, string FilePath)
		{
			this.Session = Session;
			this.FilePath = FilePath;
			this.FileName = Path.GetFileNameWithoutExtension(this.FilePath);
			this.MessageSite = new UhtSimpleMessageSite(this.Session, this);
		}

		/// <summary>
		/// Read the contents of the source file
		/// </summary>
		public virtual void Read()
		{
			this.SourceFragment = Session.ReadSource(this.FilePath);
		}
	}
}
