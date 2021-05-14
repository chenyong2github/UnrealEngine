// Copyright Epic Games, Inc. All Rights Reserved.

import { getTheme } from "@fluentui/react";
import { DashboardPreference, JobState, LabelOutcome, LabelState } from "../backend/Api";
import dashboard from "../backend/Dashboard";

const theme = getTheme();

export enum StatusColor {
    Success,
    Warnings,
    Failure,
    Waiting,
    Ready,
    Skipped,
    Aborted,
    Running,
    Unspecified
}

export const defaultStatusColors = new Map<StatusColor, string>([
    [StatusColor.Success, "#52C705"],
    [StatusColor.Warnings, "#EDC74A"],
    [StatusColor.Failure, "#DE4522"],
    [StatusColor.Running, theme.palette.blueLight],
    [StatusColor.Waiting, "#A19F9D"],
    [StatusColor.Ready, "#A19F9D"],
    [StatusColor.Skipped, "#F3F2F1"],
    [StatusColor.Aborted, "#F3F2F1"],
    [StatusColor.Unspecified, "#637087"]
]);


export const getStatusColors = () => {

    const success = dashboard.getPreference(DashboardPreference.ColorSuccess);
    const warning = dashboard.getPreference(DashboardPreference.ColorWarning);
    const error = dashboard.getPreference(DashboardPreference.ColorError);
    const running = dashboard.getPreference(DashboardPreference.ColorRunning);

    return new Map<StatusColor, string>([
        [StatusColor.Success, success ? success : defaultStatusColors.get(StatusColor.Success)!],
        [StatusColor.Warnings, warning ? warning : defaultStatusColors.get(StatusColor.Warnings)!],
        [StatusColor.Failure, error ? error : defaultStatusColors.get(StatusColor.Failure)!],
        [StatusColor.Running, running ? running : defaultStatusColors.get(StatusColor.Running)!],
        [StatusColor.Waiting, defaultStatusColors.get(StatusColor.Waiting)!],
        [StatusColor.Ready, defaultStatusColors.get(StatusColor.Ready)!],
        [StatusColor.Skipped, defaultStatusColors.get(StatusColor.Skipped)!],
        [StatusColor.Aborted, defaultStatusColors.get(StatusColor.Aborted)!],
        [StatusColor.Unspecified, defaultStatusColors.get(StatusColor.Unspecified)!]
    ]);
}

export const getJobStateColor = (state: JobState): string => {

    const colors = getStatusColors();    

    if (state === JobState.Waiting) {
        return colors.get(StatusColor.Waiting)!;
    }

    if (state === JobState.Running) {
        return colors.get(StatusColor.Running)!;
    }


    return colors.get(StatusColor.Unspecified)!;

}

export const getLabelColor = (state: LabelState | undefined, outcome: LabelOutcome | undefined):{primaryColor: string, secondaryColor?: string} => {

    if (!state || !outcome) {
        return {
            primaryColor: "#000000" 
        }
    }

    const colors = getStatusColors();

    if (state === LabelState.Complete) {
        switch (outcome!) {
            case LabelOutcome.Failure:
                return { primaryColor: colors.get(StatusColor.Failure)!};        
            case LabelOutcome.Success:
                return { primaryColor: colors.get(StatusColor.Success)!};        
            case LabelOutcome.Warnings:
                return { primaryColor: colors.get(StatusColor.Warnings)!};            
            default:
                return { primaryColor: colors.get(StatusColor.Unspecified)!};        
        }
    }

    if (outcome === LabelOutcome.Failure) {
        return { primaryColor: colors.get(StatusColor.Running)!, secondaryColor: colors.get(StatusColor.Failure)!};        
    }

    if (outcome === LabelOutcome.Warnings) {
        return { primaryColor: colors.get(StatusColor.Running)!, secondaryColor: colors.get(StatusColor.Warnings)!};        
    }

    return {
        primaryColor: colors.get(StatusColor.Running)!
    }
};

