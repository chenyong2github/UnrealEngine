// Copyright Epic Games, Inc. All Rights Reserved.

export enum TestState {
    Failed = "Fail",
    Success = "Success",
}

export enum EventType {
    Info = "Info",
    Error = "Error",
    Warning = "Warning",
}

export type TestEntryArtifact = {
    Id: string;
    Name: string;
    Type: string;
    Files: { Difference: string, Approved: string, Unapproved: string };
}

export type TestEntry = {
    Filename: string;
    LineNumber: number;
    Timestamp: string;
    Event: { Type: string, Message: string, Context: string, Artifact: string };
}

export type TestDetails = {
    TestDisplayName: string;
    FullTestPath: string;
    State: string;
    Errors: number;
    Warnings: number;
    Entries: TestEntry[];
    Artifacts: TestEntryArtifact[];
}

export type TestResult = {
    TestDisplayName: string;
    FullTestPath: string;
    State: string;
    ArtifactName: string;
}

export type TestPassSummary = {
    ClientDescriptor: string;
    ReportURL: string;
    FailedCount: number;
    NotRunCount: number;
    ReportCreatedOn: string;
    SucceededCount: number;
    SucceededWithWarningsCount: number;
    TotalDurationSeconds: number;
    Tests: TestResult[];
}

export type TestStateHistoryItem = {
    TestdataId: string;
    Change: number;
    State: string;
}
