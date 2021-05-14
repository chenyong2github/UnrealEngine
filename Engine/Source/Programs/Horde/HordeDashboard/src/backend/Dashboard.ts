// Copyright Epic Games, Inc. All Rights Reserved.

import { action, observable } from 'mobx';
import backend from '.';
import { UserClaim, DashboardPreference, GetUserResponse } from './Api';

export class Dashboard {

    startPolling() {
        this.polling = true;
    }

    stopPolling() {
        this.polling = false;
    }

    jobPinned(id: string | undefined) {
        return !!this.pinnedJobsIds.find(j => j === id);
    }

    pinJob(id: string) {

        if (this.data.pinnedJobIds.find(j => j === id)) {
            return;
        }

        this.data.pinnedJobIds.push(id);

        backend.updateUser({ addPinnedJobIds: [id] });

        this.setUpdated();
    }

    unpinJob(id: string) {

        if (!this.data.pinnedJobIds.find(j => j === id)) {
            return;
        }

        this.data.pinnedJobIds = this.data.pinnedJobIds.filter(j => j !== id);

        backend.updateUser({ removePinnedJobIds: [id] });

        this.setUpdated();
    }

    get username(): string {

        let email = this.claims.find(c => c.type.endsWith("/ue/horde/user"));

        if (!email) {
            email = this.claims.find(c => c.type.endsWith("/identity/claims/name"));
        }

        return email ? email.value : "???";
    }


    get hordeAdmin(): boolean {

        return !!this.roles.find(r => r.value === "app-horde-admins");
    }

    get internalEmployee(): boolean {

        return !!this.roles.find(r => r.value === "Internal-Employees");

    }

    get email(): string {

        const email = this.claims.find(c => c.type.endsWith("/emailaddress"));

        return email ? email.value : "???";
    }

    get p4user(): string {
        const claims = this.claims;
        const user = claims.filter(c => c.type.endsWith("/perforce-user"));
        if (!user.length) {
            return "";
        }
        return user[0].value;

    }

    get pinnedJobsIds(): string[] {

        return this.data.pinnedJobIds ?? [];
    }

    get issueNotifications(): boolean {

        return this.data.enableIssueNotifications;
    }

    set issueNotifications(value: boolean) {

        backend.updateUser({ enableIssueNotifications: value }).then(() => {

            this.data.enableIssueNotifications = value;
            this.setUpdated();

        });

    }

    get experimentalFeatures(): boolean {

        return this.data.enableExperimentalFeatures;
    }

    set experimentalFeatures(value: boolean) {

        backend.updateUser({ enableExperimentalFeatures: value }).then(() => {

            this.data.enableExperimentalFeatures = value;
            this.setUpdated();

        });

    }

    get roles(): UserClaim[] {
        const claims = this.claims;
        return claims.filter(c => c.type.endsWith("/role"));
    }


    get claims(): UserClaim[] {
        return this.data.claims ? this.data.claims : [];
    }

    get displayUTC(): boolean {

        return this.preferences.get(DashboardPreference.DisplayUTC) === 'true';

    }

    setDisplayUTC(value: boolean | undefined) {
        this.setPreference(DashboardPreference.DisplayUTC, value ? "true" : "false");
    }

    get localCache(): boolean {

        return this.preferences.get(DashboardPreference.LocalCache) === 'true';

    }

    setLocalCache(value: boolean | undefined) {
        this.setPreference(DashboardPreference.LocalCache, value ? "true" : "false");
    }


    get display24HourClock(): boolean {

        return this.preferences.get(DashboardPreference.DisplayClock) === '24';

    }

    setStatusColor(pref: DashboardPreference, value: string | undefined) {

        if (value && !value.startsWith("#")) {
            console.error("Status preference color must be in hex format with preceding #")
        }

        this.setPreference(pref, value);
    }


    setDisplay24HourClock(value: boolean | undefined) {
        this.setPreference(DashboardPreference.DisplayClock, value ? "24" : "");
    }

    getPreference(pref: DashboardPreference): string | undefined {


        if (!this.available) {
            return undefined;
        }

        if (!this.preferences) {
            return undefined;
        }

        return this.preferences.get(pref);
    }

    private get preferences() {
        return this.data.dashboardSettings.preferences;
    }

    async update() {

        try {

            if (this.updating) {
                clearTimeout(this.updateTimeoutId);
                this.updateTimeoutId = setTimeout(() => { this.update(); }, 4000);
                return;
            }

            this.updating = true;

            if (this.polling || !this.available) {

                const cancelId = this.cancelId++;

                const response = await backend.getUser();

                // check for canceled during graph request
                if (!this.canceled.has(cancelId)) {

                    this.data = response;

                    if (this.data.claims) {

                        const set = new Set<string>();
                        this.data.claims = this.data.claims.filter(c => {
                            const key = c.type + c.value;
                            if (set.has(key)) {
                                return false;
                            }
                            set.add(key);
                            return true;
                        })

                    }

                    // @todo: detect changed                
                    this.setUpdated();

                }

            }

        } catch (reason) {
            if (!this.available) {
                // this is being added 1/25/21 for changes to how User's are handled on backend
                // ie. not created until logged in via Okta
                // if this is still an error in the future, this may be changed
                this.requestLogout = true;
            }
            console.error("Error updating user dashboard settings", reason)
        } finally {
            this.updating = false;
            clearTimeout(this.updateTimeoutId);
            this.updateTimeoutId = setTimeout(() => { this.update(); }, 4000);
        }
    }

    private setPreference(pref: DashboardPreference, value: string | undefined): void {

        if (!this.available) {
            return;
        }

        if (this.preferences.get(pref) === value) {
            return;
        }

        if (value === undefined) {
            this.preferences.delete(pref);
        } else {
            this.preferences.set(pref, value);
        }

        this.postPreferences();

    }


    @observable
    updated: number = 0;

    @action
    private setUpdated() {
        this.updated++;
    }

    get available(): boolean {
        return this.data.id !== "";
    }


    private async postPreferences(): Promise<boolean> {

        // cancel any pending        
        for (let i = 0; i < this.cancelId; i++) {
            this.canceled.add(i);
        }

        const data: any = {};

        for (const key of Object.keys(DashboardPreference)) {
            data[key] = this.data.dashboardSettings.preferences?.get(key as DashboardPreference);
        }

        let success = true;
        try {
            await backend.updateUser({ dashboardSettings: { preferences: data } });
        } catch (reason) {
            success = false;
            console.error("Error posting user preferences", reason)
        }

        return success;

    }

    requestLogout = false;

    private data: GetUserResponse = { id: "", enableIssueNotifications: false, enableExperimentalFeatures: false, claims: [], pinnedJobIds: [], dashboardSettings: { preferences: new Map() } };

    private updateTimeoutId: any = undefined;

    private updating = false;

    private polling = false;

    private canceled = new Set<number>();
    private cancelId = 0;

}

const dashboard = new Dashboard();

export default dashboard;

