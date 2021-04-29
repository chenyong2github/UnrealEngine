// Copyright Epic Games, Inc. All Rights Reserved.

import { observable, action } from "mobx";
import backend from '../backend';
import { StreamData, JobData, ArtifactData, TestData } from './Api';
import { projectStore } from './ProjectStore';
import { BreadcrumbItem } from '../components/Breadcrumbs';

class JobInfo {
    stream?: StreamData;
    jobdata?: JobData;
    artifacts?: ArtifactData[];
    stepName : string = "UnknownJob";
    id?: string;
    stepId?: string;

    @action
    setActive(active: boolean) {
        this.active = active;
    }

    @observable
    active = true;

    async set(targetJobId : string, targetStepId : string) {
        if (this.id === targetJobId && this.stepId === targetStepId) {
            return;
        }

        if (!targetJobId || !targetStepId) {
            return;
        }

        this.stepId = targetStepId;
        this.stepName = "UnknownJob";

        this.setActive(true);

        try {
            if (this.jobdata === undefined || this.id !== targetJobId) {
                this.jobdata = await backend.getJob(targetJobId);
                this.id = targetJobId;
                this.stream = projectStore.streamById(this.jobdata.streamId);
            }

            let stepName = "";
            const batch = this.jobdata.batches?.find(b => b.steps.find(s => s.id === this.stepId));
            const stepNode = batch?.steps.find(s => s.id === this.stepId);
            const groups = this.jobdata?.graphRef?.groups;
            if (groups && stepNode && batch) {
                stepName = groups[batch.groupIdx]?.nodes[stepNode.nodeIdx]?.name;
            }
            this.stepName = stepName;

            this.artifacts = await backend.getJobArtifacts(this.id!, this.stepId);
        }
        catch(reason) {
            console.error('Failed to load job info!');
            throw reason;
        }
        finally {
            this.setActive(false);
        }
    }   

}

export class TestDataHandler {
    stream?: StreamData;
    id?: string;

    jobInfo: JobInfo = new JobInfo();
    testdata?: TestData;
    name?: string;
    type?: string;

    history?: TestData[];

    artifactMap: Map<string, ArtifactData | undefined> = new Map();

    @action
    setActive(active: boolean) {
        this.active = active;
    }

    @observable
    active = true;

    @action
    setHistoryLoaded(loaded: boolean) {
        this.historyLoaded = loaded;
    }

    @observable
    historyLoaded = false

    async set(targetId: string) {

        if (this.id === targetId) {
            return;
        }

        if (!targetId) {
            return;
        }

        this.id = targetId;
        this.testdata = undefined;
        this.name = undefined;
        this.type = undefined;
        this.history = undefined;
        if (this.artifactMap.size > 0) {
            this.artifactMap = new Map();
        }

        this.setActive(true);

        try {
            this.testdata = await backend.getTestData(targetId);
            const key = this.testdata.key.split('::');
            if (key.length > 1) {
                [ this.type, this.name ] = key;
            }
            else {
                this.name = key[0];
            }

            await this.jobInfo.set(this.testdata.jobId, this.testdata.stepId);
            this.stream = this.jobInfo.stream;
        }
        catch(reason) {
            console.error('Failed to load test data from job!');
            throw reason;
        }
        finally {
            this.setActive(false);
        }

    }

    async getHistory(fromChange? : number) {
        if (!this.id || !this.stream || !this.testdata) {
            return;
        }

        if (this.history !== undefined) {
            return;
        }

        this.setHistoryLoaded(false);

        try {
            this.history = await backend.getTestDataHistory(this.stream.id, this.testdata.key, fromChange);
        }
        catch(reason) {
            console.error('Failed to load test data history!');
            throw reason;
        }
        finally {
            this.setHistoryLoaded(true);
        }
    }

    cleanHistory() {
        if (this.history !== undefined) {
            this.history = undefined;
            this.setHistoryLoaded(false);
        }
    }

    get clText(): string {

        const data = this.jobInfo.jobdata;
        if (!data) {
            return "";
        }

        let clText = "";
        if (data.preflightChange) {
            clText = `Preflight ${data.preflightChange} `;
            clText += ` - Base ${data.change ? data.change : "Latest"}`;

        } else {
            clText = `${data.change ? "CL " + data.change : "Latest CL"}`;
        }
        
        return clText;
    }

    get crumbs(): BreadcrumbItem[] {

        const jobdata = this.jobInfo.jobdata!;

        if (!this.stream) {
            return [];
        }

        let projectName = this.stream.project?.name;
        if (projectName === "Engine") {
            projectName = "UE4";
        }

        const crumbItems: BreadcrumbItem[] = [
            {
                text: projectName ?? "Unknown Project",
                link: `/project/${this.stream?.project?.id}`
            },
            {
                text: this.stream.name,
                link: `/stream/${this.stream.id}`
            },
            {
                text: `${this.clText}: ${jobdata?.name ?? ""}`,
                link: `/job/${jobdata?.id}`
            },
            {
                text: this.jobInfo.stepName,
                link: this.jobInfo.stepId ? `/job/${jobdata?.id}?step=${this.jobInfo.stepId}` : `/job/${jobdata?.id}`
            },
            {
                text: this.name + " (Test Report)" 
            }
        ];

        return crumbItems;
    }

    get crumbTitle(): string | undefined {

        if (!this.stream) {
            return undefined;
        }
        return `Horde - ${this.clText}: ${this.jobInfo.jobdata?.name ?? ""} - ${this.jobInfo.stepName} (Test Report)`
    }

    findArtifactData(artifactName: string): ArtifactData | undefined {
        if (!artifactName) {
            return undefined;
        }

        artifactName = artifactName.replace(/\\/g,  '/');

        if (this.artifactMap.has(artifactName)) {
            return this.artifactMap.get(artifactName);
        }

        const found = this.jobInfo.artifacts?.find((value) => value.name.indexOf(artifactName) > -1);
        this.artifactMap.set(artifactName, found);

        return found;
    }
}
