// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System.Text.Json;

namespace EpicGames.UHT.Types
{
	public class UhtManifestFile : IUhtMessageSite
	{
		public UHTManifest? Manifest = null;

		private UhtSourceFile SourceFile;

		#region IUHTMessageSite implementation
		public IUhtMessageSession MessageSession => this.SourceFile.MessageSession;
		public IUhtMessageSource? MessageSource => this.SourceFile.MessageSource;
		public IUhtMessageLineNumber? MessageLineNumber => null;
		#endregion

		public UhtManifestFile(UhtSession Session, string FilePath)
		{
			this.SourceFile = new UhtSourceFile(Session, FilePath);
		}

		public void Read() 
		{
			this.SourceFile.Read();
			this.Manifest = JsonSerializer.Deserialize<UHTManifest>(this.SourceFile.Data.ToString());
		}
	}
}
