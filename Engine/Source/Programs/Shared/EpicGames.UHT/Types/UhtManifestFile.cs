// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System.Text.Json;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Represents the UHT manifest file
	/// </summary>
	public class UhtManifestFile : IUhtMessageSite
	{

		/// <summary>
		/// Loaded manifest from the json manifest file
		/// </summary>
		public UHTManifest? Manifest = null;

		private readonly UhtSourceFile SourceFile;

		#region IUHTMessageSite implementation

		/// <inheritdoc/>
		public IUhtMessageSession MessageSession => this.SourceFile.MessageSession;

		/// <inheritdoc/>
		public IUhtMessageSource? MessageSource => this.SourceFile.MessageSource;

		/// <inheritdoc/>
		public IUhtMessageLineNumber? MessageLineNumber => null;
		#endregion

		/// <summary>
		/// Construct a new manifest file
		/// </summary>
		/// <param name="Session">Current session</param>
		/// <param name="FilePath">Path of the file</param>
		public UhtManifestFile(UhtSession Session, string FilePath)
		{
			this.SourceFile = new UhtSourceFile(Session, FilePath);
		}

		/// <summary>
		/// Read the contents of the file
		/// </summary>
		public void Read() 
		{
			this.SourceFile.Read();
			this.Manifest = JsonSerializer.Deserialize<UHTManifest>(this.SourceFile.Data.ToString());
		}
	}
}
