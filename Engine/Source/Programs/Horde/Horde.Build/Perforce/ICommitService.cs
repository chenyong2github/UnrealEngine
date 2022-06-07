// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Horde.Storage;
using Horde.Build.Streams;
using Horde.Build.Utilities;

namespace Horde.Build.Perforce
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Provides information about commits
	/// </summary>
	public interface ICommitService
	{
		/// <summary>
		/// Registers a delegate to be called when a new commit is added
		/// </summary>
		/// <param name="onAddCommit">Callback for a new commit being added</param>
		/// <returns>Disposable handler.</returns>
		IDisposable AddListener(Action<ICommit> onAddCommit);

		/// <summary>
		/// Gets reference to replicated content for the given stream and change
		/// </summary>
		/// <param name="stream"></param>
		/// <param name="change"></param>
		/// <returns></returns>
		QualifiedRefId? GetReplicatedContentRef(IStream stream, int change);
	}
}
