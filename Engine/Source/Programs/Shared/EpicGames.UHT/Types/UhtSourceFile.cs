// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using EpicGames.Core;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents a source file in UHT
	/// </summary>
	public class UhtSourceFile : IUhtMessageSite, IUhtMessageSource
	{
		private readonly UhtSimpleMessageSite _messageSite;
		private UhtSourceFragment _sourceFragment;

		/// <summary>
		/// The session associated with the source
		/// </summary>
		public UhtSession Session { get; }

		/// <summary>
		/// The full file path
		/// </summary>
		public string FilePath { get; }

		/// <summary>
		/// The file name
		/// </summary>
		public string FileName { get; }

		/// <summary>
		/// The contents of the source file
		/// </summary>
		public StringView Data => _sourceFragment.Data;

		/// <summary>
		/// If this source is from a fragment, the original source file
		/// </summary>
		public UhtSourceFile? FragmentSourceFile => _sourceFragment.SourceFile;

		/// <summary>
		/// If this source is from a fragment, the starting line of the fragment
		/// </summary>
		public int FragmentLineNumber => _sourceFragment.LineNumber;

		/// <summary>
		/// Full file path of the source file
		/// </summary>
		public string FullFilePath => _sourceFragment.FilePath;

		#region IUHTMessageSite implementation
		/// <inheritdoc/>
		public IUhtMessageSession MessageSession => this._messageSite.MessageSession;

		/// <inheritdoc/>
		public IUhtMessageSource? MessageSource => this._messageSite.MessageSource;

		/// <inheritdoc/>
		public IUhtMessageLineNumber? MessageLineNumber => null;
		#endregion

		#region IUHTMessageSource implementation
		/// <inheritdoc/>
		public string MessageFilePath => this.FilePath;

		/// <inheritdoc/>
		public string MessageFullFilePath => this.FullFilePath;

		/// <inheritdoc/>
		public bool MessageIsFragment => this.FragmentSourceFile != null;

		/// <inheritdoc/>
		public string MessageFragmentFilePath => this.FragmentSourceFile != null ? this.FragmentSourceFile.MessageFilePath : "";

		/// <inheritdoc/>
		public string MessageFragmentFullFilePath => this.FragmentSourceFile != null ? this.FragmentSourceFile.MessageFullFilePath : "";

		/// <inheritdoc/>
		public int MessageFragmentLineNumber => this.FragmentLineNumber;
		#endregion

		/// <summary>
		/// Construct a new instance of a source file
		/// </summary>
		/// <param name="session">The owning session</param>
		/// <param name="filePath">The full file path</param>
		public UhtSourceFile(UhtSession session, string filePath)
		{
			this.Session = session;
			this.FilePath = filePath;
			this.FileName = Path.GetFileNameWithoutExtension(this.FilePath);
			this._messageSite = new UhtSimpleMessageSite(this.Session, this);
		}

		/// <summary>
		/// Read the contents of the source file
		/// </summary>
		public virtual void Read()
		{
			this._sourceFragment = Session.ReadSource(this.FilePath);
		}
	}
}
