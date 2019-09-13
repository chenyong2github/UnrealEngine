// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

export interface TickJournal {
	merges: number;
	conflicts: number;

	monitored: boolean;
}

export interface Bot {
	start(): Promise<void>;
	tick(next: () => (Promise<any> | void)): void;

	fullName: string;

	isRunning: boolean;
	isActive: boolean;

	tickJournal?: TickJournal;
}
