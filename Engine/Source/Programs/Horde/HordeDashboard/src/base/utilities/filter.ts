import { GetJobResponse } from "../../backend/Api";


export type JobFilterSimple = {
    filterKeyword?: string;
    showOthersPreflights: boolean;
}

const getJobKeywords = (job: GetJobResponse): string[] => {

    let keywords: string[] = [];

    if (job.change) {
        keywords.push(job.change.toString());
    }

    if (job.preflightChange) {
        keywords.push(job.preflightChange.toString());
    }

    if (job.startedByUser) {
        keywords.push(job.startedByUser);
    } else {
        keywords.push("Scheduler");
    }


    keywords.push(...job.arguments);

    return keywords;

}

export const filterJob = (job: GetJobResponse, keywordIn?: string, additionalKeywords?:string[]): boolean => {

    const keyword = keywordIn?.toLowerCase();

    if (!keyword) {
        return true;
    }

    let keywords = getJobKeywords(job);


    if (additionalKeywords) {
        keywords.push(...additionalKeywords);
    }

    keywords = keywords.map(k => k.toLowerCase());

    return !!keywords.find(k => k.indexOf(keyword) !== -1);
}