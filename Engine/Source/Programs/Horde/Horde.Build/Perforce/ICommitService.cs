// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace Horde.Build.Commits
{
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
	}
}
