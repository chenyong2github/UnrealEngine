/*********

Stress tester for Oodle Job system implementations.

Use to help verify that the jobify plugin functions you provide to Oodle are working.

Use like :

Oodle_JobSystemStressTest(my_CorePlugin_RunJob, my_CorePlugin_WaitJob, NULL, 0, 256, 50000);

*********/

#include <stdio.h>
#include <string.h>
#include <assert.h>


namespace JobTester
{

static inline U32 rotl(U32 x, int k)
{
    return (x << k) | (x >> (32 - k));
}

// Our hash function
// this is Bob Jenkin's loookup3 for just two 32-bit integers
static U32 hash_pair(U32 x, U32 y)
{
    U32 a, b, c;
    a = b = c = 0xdeadbeef + 8;

    a += x;
    b += y;

    // final mix
    c ^= b; c -= rotl(b,14);
    a ^= c; a -= rotl(c,11);
    b ^= a; b -= rotl(a,25);
    c ^= b; c -= rotl(b,16);
    a ^= c; a -= rotl(c,4);
    b ^= a; b -= rotl(a,14);
    c ^= b; c -= rotl(b,24);

    return c;
}

struct Context;

struct Step
{
    // Type of work step to perform
    enum StepType
    {
        FORK,
        JOIN,
    };

    StepType type;
    int index;
};

struct Job
{
    enum
    {
        MAX_FORKS = 32,
        MAX_STEPS = MAX_FORKS*2, // one join per fork
        MAX_DEPS = OODLE_JOB_MAX_DEPENDENCIES,
    };

    Context * ctx;          // work context to use

    int id;                 // my global job ID (index into jobs, results arrays)
    int parent_id;          // ID of parent job
    int first_child_id;     // ID of first child job (other children are sequential after that)

    U32 seed;               // seed value for computation performed
    int ndeps;              // number of input deps
    int deps[MAX_DEPS];     // indices of sibling jobs we depend on, local to parent (add first_sibling_id to get global ID)

    int nsteps;             // number of work steps to perform
    Step steps[MAX_STEPS];  // description of the computation to perform

    static void OODLE_CALLBACK run_thunk(void * context)
    {
        static_cast<Job*>(context)->run();
    }

    void run() const;
};

struct Context
{
    enum
    {
        MAX_JOBS = 256, // no need to go overboard with this
    };

    t_fp_OodleCore_Plugin_RunJob * fp_RunJob;
    t_fp_OodleCore_Plugin_WaitJob * fp_WaitJob;
    void * user_ptr;

    int next_job_id;
    int num_jobs_total;

    Job jobs[MAX_JOBS];
    U32 ref_results[MAX_JOBS];
    U32 job_results[MAX_JOBS];
};

void Job::run() const
{
    U64 handles[MAX_FORKS];

    U32 x = seed;
    for (int i = 0; i < ndeps; i++)
    {
        // Determine global job ID for the job we depend on
        int dep_job_id = ctx->jobs[parent_id].first_child_id + deps[i];
        x = hash_pair(x, ctx->job_results[dep_job_id]);
    }

    // Run the desired sequence of work steps
    for (int i = 0; i < nsteps; i++)
    {
        int index = steps[i].index;
        int subjob_id = first_child_id + index;
        assert(subjob_id >= 0 && subjob_id < ctx->num_jobs_total);
        assert(index < MAX_FORKS);

        if (steps[i].type == Step::FORK)
        {
            Job * subjob = &ctx->jobs[subjob_id];
            U64 runjob_deps[MAX_DEPS];
            int nrunjob_deps = 0;

            // Work out our dependency list; jobs that we've already waited for,
            // or that ran synchronously (handle value of 0) don't count.
            for (int j = 0; j < subjob->ndeps; j++)
            {
                int dep_index = subjob->deps[j];
                if (handles[dep_index])
                    runjob_deps[nrunjob_deps++] = handles[dep_index];
            }

            // Run the job!
            handles[index] = ctx->fp_RunJob(run_thunk, subjob, runjob_deps, nrunjob_deps, ctx->user_ptr);
        }
        else // join
        {
            // If we didn't get a 0 handle from RunJob, wait for job completion!
            if (handles[index])
            {
                ctx->fp_WaitJob(handles[index], ctx->user_ptr);
                handles[index] = 0; // mark as done
            }

            // Immediately incorporate the result of the computation
            x = hash_pair(x, ctx->job_results[subjob_id]);
        }
    }

    // Could do a random delay here, but it seems to be working great and
    // finding tricky interleavings without.

    // Save the result of the computation and we're done
    ctx->job_results[id] = x;
}

// ---- Randomness utilities

// The RNG here is PCG XSH RR 64/32 MCG; this has a moderate state size
// and decent quality.
class Rng
{
    static const U64 MCG_MUL = 6364136223846793005ULL; 
    U64 state;

public:
    static Rng seed(U32 seed)
    {
        // State may not be 0 (MCG); that's why we flip the low bits
        // also do one multiply step in case the input is a small integer (which it often is).
        Rng r;
        r.state = ~(U64)seed | (U64(seed) << 32);
        return r;
    }

    // Random 32-bit uint
    U32 random()
    {
        U64 oldstate = state;
        U32 rot_input = (U32) (((oldstate >> 18) ^ oldstate) >> 27);
        U32 rot_amount = (U32) (oldstate >> 59);
        U32 output = (rot_input >> rot_amount) | (rot_input << ((0u - rot_amount) & 31)); // rotr(rot_input, rot_amount)

        // Advance multiplicative congruential generator
        // Constant from PCG reference impl.
        state = oldstate * MCG_MUL;

        return output;
    }

    // Random uint x with 0 <= x <= max
    U32 random_with_max(U32 max)
    {
        // Smear right to determine next-larger pow2 mask
        U32 mask = max;
        mask |= mask >> 16;
        mask |= mask >>  8;
        mask |= mask >>  4;
        mask |= mask >>  2;
        mask |= mask >>  1;

        // Expected number of iterations is below 2
        for(;;)
        {
            U32 x = random() & mask;
            if (x <= max)
                return x;
        }
    }

    U32 random_min_max(U32 min, U32 max)
    {
        return min + random_with_max(max - min);
    }
};

// Generate a random balanced sequence of n +1s and n -1s corresponding to
// correctly nested brackets - that, is, all of the partial sums are
// nonnegative.
static void random_bracket_sequence(S8 * result, int n, Rng &rng)
{
    // Algorithm follows Atkinson & Sack, "Generating binary trees at random", 1992
    int partial_sum = 0;
    int left = 0;
    int right = 2*n;
    int word_start = 0;
    int npos_left = n;

    for (int i = 0; i < 2*n; i++)
    {
        S8 sel;

        // Steps 1 and 2: Selection sampling: of the 2*n-i remaining items, npos_left
        // need to be +1s.
        if (int(rng.random_with_max(2*n - i - 1)) < npos_left)
        {
            sel = 1;
            --npos_left;
        }
        else
            sel = -1;
        result[left++] = sel;

        // Step 3: make it well-formed
        partial_sum += sel;
        if (partial_sum == 0) // at end of an irreducible balanced word
        {
            if (sel == -1)
                word_start = left; // it was well-formed! we're all good.
            else
            {
                // not well-formed; fix it!
                // move the middle of the current word to the right side and flip all the signs
                // (skip the first and last items, which we know are -1 and +1, respectively)
                for (int j = left - 2; j > word_start; --j)
                    result[--right] = -result[j];
                // other half of the fixup
                result[word_start++] = 1;
                result[--right] = -1;
                left = word_start;
            }
        }
    }

    assert(npos_left == 0);
    assert(left == right);
    assert(partial_sum == 0);
}

// ---- Random job generator

static void generate_random_job_rec(Context * ctx, int id, int parent_id, const int * deps, int ndeps, Rng &rng, int rec_level, int max_rec_level)
{
    Job * job = &ctx->jobs[id];
    assert(ndeps <= Job::MAX_DEPS);

    job->ctx = ctx;
    job->id = id;
    job->parent_id = parent_id;
    job->first_child_id = ctx->next_job_id;

    job->seed = rng.random();
    job->ndeps = ndeps;

    // We also compute the reference result as we're setting up the job
    U32 ref_result = job->seed;

    for (int i = 0; i < ndeps; i++)
    {
        job->deps[i] = deps[i];
        int dep_job_id = ctx->jobs[parent_id].first_child_id + deps[i];
        assert(dep_job_id < id);
        ref_result = hash_pair(ref_result, ctx->ref_results[dep_job_id]);
    }

    // Figure out how many jobs to fork
    int max_jobs = ctx->num_jobs_total - ctx->next_job_id;
    if (max_jobs > Job::MAX_FORKS)
        max_jobs = Job::MAX_FORKS;

    // Deepest level may not spawn further jobs
    if (rec_level >= max_rec_level)
        max_jobs = 0;

    int fork_count = rng.random_with_max(max_jobs);

    // Avoid root jobs with a fork count of 0 because that would be pointless
    if (parent_id == -1 && fork_count == 0 && max_jobs != 0)
        fork_count = 1;

    ctx->next_job_id += fork_count;
    job->nsteps = fork_count * 2;

    S8 spawn_sequence[Job::MAX_STEPS];
    int child_inds[Job::MAX_FORKS];
    int nlive_children = 0;
    int fork_index = 0;

    // Generate a random bracket sequence corresponding to the forks/joins
    random_bracket_sequence(spawn_sequence, fork_count, rng);

    for (int i = 0; i < job->nsteps; i++)
    {
        Step * step = &job->steps[i];
        step->type = (spawn_sequence[i] > 0) ? Step::FORK : Step::JOIN;
        if (step->type == Step::FORK)
        {
            step->index = fork_index;

            // Randomly select dependencies from among the already-spawned children
            // We're OK with putting dependencies on already waited-for jobs
            // (i.e. we still want the data dependency on some things we already waited
            // for); the driver code handles this, as does the equivalent logic in Oodle
            // itself, and we want to test it.
            int ndep_max = fork_index < Job::MAX_DEPS ? fork_index : Job::MAX_DEPS;
            int jndeps = rng.random_with_max(ndep_max);
            int jdeps[Job::MAX_DEPS];
            int nleft_to_select = jndeps;
            for (int j = 0; j < fork_index; j++)
            {
                // of the fork_index - j remaining children, we want to select nleft_to_select
                if (int(rng.random_with_max(fork_index - j - 1)) < nleft_to_select)
                    jdeps[--nleft_to_select] = j;
            }
            assert(nleft_to_select == 0);

            // Set up the job recursively
            generate_random_job_rec(ctx, job->first_child_id + fork_index, id, jdeps, jndeps, rng, rec_level + 1, max_rec_level);

            // Log as one of the live children
            child_inds[nlive_children++] = fork_index;
            fork_index++;
        }
        else // JOIN
        {
            // Randomly pick one of the live children and wait for it
            assert(nlive_children > 0);
            int slot = rng.random_with_max(nlive_children - 1);

            step->index = child_inds[slot];

            // Update our reference result
            ref_result = hash_pair(ref_result, ctx->ref_results[job->first_child_id + step->index]);

            // Remove that item from the list of live children
            child_inds[slot] = child_inds[--nlive_children];
        }
    }

    assert(fork_index == fork_count);
    assert(nlive_children == 0);

    // Store reference result
    ctx->ref_results[id] = ref_result;
}

static int generate_random_jobs(Context * ctx, int num_jobs_total, Rng &rng, int max_rec_level)
{
    assert(num_jobs_total >= 1 && num_jobs_total <= Context::MAX_JOBS);

    ctx->next_job_id = 1;
    ctx->num_jobs_total = num_jobs_total;
    for (int i = 0; i < Context::MAX_JOBS; i++)
    {
        ctx->ref_results[i] = 0;
        ctx->job_results[i] = 0;
    }

    generate_random_job_rec(ctx, 0, -1, NULL, 0, rng, 0, max_rec_level);

    // ensure we didn't generate too many jobs
    assert(ctx->next_job_id <= ctx->num_jobs_total);

    // but we might not have used all of them! so return real count.
    ctx->num_jobs_total = ctx->next_job_id;
    return ctx->num_jobs_total;
}

static void describe_job(FILE * dst, const Context * ctx, int job_id)
{
    const Job * job = ctx->jobs + job_id;

    fprintf(dst, "job %d:\n", job_id);
    fprintf(dst, "  result expected=0x%08x, actual=0x%08x\n", ctx->ref_results[job_id], ctx->job_results[job_id]);
    fprintf(dst, "  parent=%d\n", job->parent_id);
    fprintf(dst, "  seed=0x%08x\n", job->seed);
    fprintf(dst, "  depends = {");
    for (int i = 0; i < job->ndeps; i++)
        fprintf(dst, "%c%d", (i == 0) ? ' ' : ',', ctx->jobs[job->parent_id].first_child_id + job->deps[i]);
    fprintf(dst, " }\n");
    fprintf(dst, "  work sequence:\n");
    for (int i = 0; i < job->nsteps; i++)
    {
        const Step *step = job->steps + i;
        fprintf(dst, "  [%2d] %s job %d\n", i, (step->type == Step::FORK) ? "fork" : "join", job->first_child_id + step->index);
    }
    fprintf(dst, "  [%2d] done\n", job->nsteps);
    fprintf(dst, "\n");
}

} // namespace JobTester

void Oodle_JobSystemStressTest(
    t_fp_OodleCore_Plugin_RunJob * fp_RunJob,
    t_fp_OodleCore_Plugin_WaitJob * fp_WaitJob,
    void * user_ptr,
    U32 random_seed, int max_jobs_per_test, int num_tests_to_run)
{
    using namespace JobTester;

    if (max_jobs_per_test < 2)
        max_jobs_per_test = 2;
    if (max_jobs_per_test > Context::MAX_JOBS)
        max_jobs_per_test = Context::MAX_JOBS;

    Context * ctx = new Context;

    ctx->fp_RunJob = fp_RunJob;
    ctx->fp_WaitJob = fp_WaitJob;
    ctx->user_ptr = user_ptr;

    U32 cur_seed = random_seed;
    int total_num_jobs = 0;
    bool errors = false;

    for (int i = 0; i < num_tests_to_run; i++)
    {
        Rng rng = Rng::seed(cur_seed);
        
        // The root job runs on this thread, so always use at least 2 jobs
        // so something interesting happens.
        int job_count = rng.random_min_max(2, max_jobs_per_test);

        // Generate a random set of jobs
        // we don't let any but the first level spawn (and wait for) child jobs
        job_count = generate_random_jobs(ctx, job_count, rng, 1);

        // Run the root job (which spawns everything else!)
        ctx->jobs[0].run();

        if (memcmp(ctx->ref_results, ctx->job_results, job_count * sizeof(U32)) != 0)
        {
            fprintf(stderr, "JobSystemStressTest: ERROR in test run %d/%d\n", i, num_tests_to_run);
            fprintf(stderr, "repro with seed=0x%08x max_jobs_per_test=%d num_tests_to_run=1\n", cur_seed, max_jobs_per_test);
            fprintf(stderr, "%d jobs in this test instance.\n", job_count);

            // Identify first failed test, which is the _largest_ index with a difference since parent tasks
            // have results that depend on their child tasks.
            int first_mismatch = job_count - 1;
            while (ctx->ref_results[first_mismatch] == ctx->job_results[first_mismatch])
                --first_mismatch;

            // If this job has a parent, it might have siblings it depends on that started earlier
            // and were also wrong; see if that's the case.
            int parent_id = ctx->jobs[first_mismatch].parent_id;
            int first_sibling_id = -1;

            if (parent_id != -1)
            {
                // Go to the first sibling with a mismatch
                first_sibling_id = ctx->jobs[parent_id].first_child_id;
                first_mismatch = first_sibling_id;
                while (ctx->ref_results[first_mismatch] == ctx->job_results[first_mismatch])
                    ++first_mismatch;
            }

            fprintf(stderr, "First mismatch is on job %d:\n\n", first_mismatch);
            describe_job(stderr, ctx, first_mismatch);

            if (parent_id != -1)
            {
                fprintf(stderr, "Parent is ");
                describe_job(stderr, ctx, parent_id);

                for (int isib = first_sibling_id; isib < first_mismatch; isib++)
                {
                    fprintf(stderr, "Sibling %d is ", isib - first_sibling_id);
                    describe_job(stderr, ctx, isib);
                }
            }

            errors = true;
            break;
        }

        // Generate use another random number to re-seed the next iteration
        // we re-seed every iteration so we have a 32-bit number we can print
        // that describes the failing test.
        cur_seed = rng.random();
        total_num_jobs += job_count;

        if (i % 100 == 99)
        {
            putchar('.');
            fflush(stdout);
        }
    }

    if (!errors)
    {
        printf("\nSUCCESS! JobSystemStress test completed %d runs correctly, %d jobs executed total.\n", num_tests_to_run, total_num_jobs);
    }

    delete ctx;
}
