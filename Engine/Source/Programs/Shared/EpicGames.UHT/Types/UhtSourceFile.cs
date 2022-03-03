// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System.IO;

namespace EpicGames.UHT.Types
{
	public class UhtSourceFile : IUhtMessageSite, IUhtMessageSource
	{
		public readonly UhtSession Session;
		public readonly string FilePath;
		public readonly string FileName;
		public StringView Data { get => SourceFragment.Data; }
		public UhtSourceFile? FragmentSourceFile { get => SourceFragment.SourceFile; }
		public int FragmentLineNumber { get => SourceFragment.LineNumber; }
		public string FullFilePath { get => SourceFragment.FilePath; }

		private UhtSimpleMessageSite MessageSite;
		private UhtSourceFragment SourceFragment;

		#region IUHTMessageSite implementation
		public IUhtMessageSession MessageSession => this.MessageSite.MessageSession;
		public IUhtMessageSource? MessageSource => this.MessageSite.MessageSource;
		public IUhtMessageLineNumber? MessageLineNumber => null;
		#endregion

		#region IUHTMessageSource implementation
		public string MessageFilePath { get => this.FilePath; }
		public string MessageFullFilePath { get => this.FullFilePath; }
		public bool bMessageIsFragment { get => this.FragmentSourceFile != null; }
		public string MessageFragmentFilePath { get => this.FragmentSourceFile != null ? this.FragmentSourceFile.MessageFilePath : ""; }
		public string MessageFragmentFullFilePath { get => this.FragmentSourceFile != null ? this.FragmentSourceFile.MessageFullFilePath : ""; }
		public int MessageFragmentLineNumber { get => this.FragmentLineNumber; }
		#endregion

		public UhtSourceFile(UhtSession Session, string FilePath)
		{
			this.Session = Session;
			this.FilePath = FilePath;
			this.FileName = Path.GetFileNameWithoutExtension(this.FilePath);
			this.MessageSite = new UhtSimpleMessageSite(this.Session, this);
		}

		public virtual void Read()
		{
			this.SourceFragment = Session.ReadSource(this.FilePath);
		}
	}
}
