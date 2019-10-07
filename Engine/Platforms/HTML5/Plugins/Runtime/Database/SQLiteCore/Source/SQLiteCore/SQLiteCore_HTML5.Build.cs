// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

public class SQLiteCore_HTML5 : SQLiteCore
{
	public SQLiteCore_HTML5(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDefinitions.Add("SQLITE_THREADSAFE=0"); // No threading on HTML5
	}
}
