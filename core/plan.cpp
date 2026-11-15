// plan.cpp — assemble immutable preview plans (design §4.1, §4.2).
//
// A plan is a snapshot: once built it does not change, and its planId is how the
// UI and the worker agree that a newer plan has superseded it. Any rule, buffer
// or selection change mints a new planId, which is why the counter is monotonic
// rather than random.
#include "core/rename_core.h"

namespace rename_core {

std::uint64_t NextPlanId()
{
    // Process-wide monotonic counter. Deliberately not thread-safe: plans are
    // built on the worker thread and handed over, never built concurrently.
    static std::uint64_t next = 0;
    return ++next;
}

PreviewPlan BuildControlPlan(const std::vector<ItemSnapshot>& items,
                             const RuleChain& chain,
                             std::uint32_t flags,
                             std::uint64_t planId)
{
    PreviewPlan plan;
    plan.planId = planId;

    // One counter for the whole batch, shared by every item and every template,
    // advanced only on a match (RNF-16, design §3.7).
    CounterState counter;

    for (const ItemSnapshot& snapshot : items)
    {
        PlanItem item;
        item.id = snapshot.id;
        item.originalLeaf = snapshot.leaf;
        item.targetLeaf = snapshot.leaf;
        item.isFolder = snapshot.isFolder;
        item.depth = snapshot.depth;

        // Flag-level scope shortcuts override the per-rule scope, matching the UI
        // where "Name only" / "Extension only" are radio buttons.
        RuleChain effective = chain;
        if ((flags & FlagNameOnly) != 0 || (flags & FlagExtensionOnly) != 0)
        {
            const Scope forced = ((flags & FlagExtensionOnly) != 0) ? Scope::Extension : Scope::Name;
            for (Rule& rule : effective.rules)
            {
                if (rule.scope == Scope::Full)
                    rule.scope = forced;
            }
        }

        const ItemOutcome outcome = Evaluate(snapshot, effective, flags, counter);
        if (outcome.error != RuleError::None)
        {
            item.status = Status::ErrorBadRule;
            plan.items.push_back(std::move(item));
            continue;
        }

        // The same finalization pipeline oil mode uses, so the two modes cannot
        // disagree about what a given name means (RN-FR-20).
        const FinalizeResult finalized = Finalize(outcome.targetLeaf, snapshot.leaf, flags, snapshot.isFolder);
        item.targetLeaf = finalized.text;
        item.status = finalized.status;
        item.trimmed = finalized.trimmed;
        item.hadNbsp = finalized.hadNbsp;

        // Exclusions apply after the name is known: a rule may legitimately be
        // scoped to folders while the flag excludes files, and the flag wins.
        const bool excluded =
            (snapshot.isFolder && (flags & FlagExcludeFolders) != 0) ||
            (!snapshot.isFolder && (flags & FlagExcludeFiles) != 0) ||
            (snapshot.isFolder && (flags & FlagExtensionOnly) != 0);

        if (excluded && item.status == Status::Ready)
        {
            item.targetLeaf = snapshot.leaf;
            item.status = Status::Unchanged;
        }

        if (item.status == Status::Ready &&
            ExceedsLength(snapshot.pathLength, snapshot.leaf, finalized.text, snapshot.isFolder))
        {
            item.status = Status::ErrorTooLong;
        }

        plan.items.push_back(std::move(item));
    }

    // Batch-level duplicate and target-exists detection. These cannot be decided
    // per item, and without them two rows could claim the same target name and
    // the executor would fail on whichever ran second.
    //
    // Both rows of a batch-internal collision are marked: telling the user only
    // about the "second" one is misleading, since reordering the batch would swap
    // which row that is. The decisions are collected first and applied after, so
    // marking row i cannot hide row j from its own check.
    const std::size_t count = plan.items.size();

    // Snapshot which rows were Ready before any marking.
    std::vector<bool> readyBefore(count, false);
    for (std::size_t i = 0; i < count; ++i)
        readyBefore[i] = plan.items[i].status == Status::Ready;

    std::vector<bool> blocked(count, false);

    // Pass 1: two rows want the same leaf.
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!readyBefore[i])
            continue;
        for (std::size_t j = i + 1; j < count; ++j)
        {
            if (!readyBefore[j])
                continue;
            if (NfcEqual(plan.items[i].targetLeaf, plan.items[j].targetLeaf))
            {
                blocked[i] = true;
                blocked[j] = true;
            }
        }
    }

    // Pass 2: a target equals some row's current name.
    //
    // Two cases, and they are not the same thing:
    //   * the other row is staying put — its name is not being freed, so this row
    //     can never run: refused (RN-FR-05, RN-T6-01). PowerRename would
    //     auto-suffix here; we refuse.
    //   * the other row is itself moving away — its name *will* be free, but only
    //     after that row runs. Record the dependency so the executor can order the
    //     batch instead of failing on a target that is still occupied.
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!readyBefore[i] || blocked[i])
            continue;
        for (std::size_t j = 0; j < count; ++j)
        {
            if (i == j)
                continue;
            if (!NfcEqual(plan.items[i].targetLeaf, plan.items[j].originalLeaf))
                continue;
            if (!readyBefore[j])
            {
                blocked[i] = true;   // nobody frees that name in this batch
                break;
            }
            plan.items[i].waitsForId = plan.items[j].id;
            break;
        }
    }

    // Pass 3: a dependency cycle cannot be ordered, so no order works. A 1-cycle
    // is a row that wants its own name — that is "unchanged", handled above; the
    // interesting case is A waits for B and B waits for A (swapping two names
    // with no temporary). Refuse both rather than let the executor discover it.
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!readyBefore[i] || blocked[i] || plan.items[i].waitsForId.empty())
            continue;
        // Walk the chain; a repeat means a cycle.
        std::string here = plan.items[i].waitsForId;
        for (std::size_t step = 0; step <= count && !here.empty(); ++step)
        {
            if (here == plan.items[i].id)
            {
                blocked[i] = true;
                break;
            }
            std::size_t next = count;
            for (std::size_t k = 0; k < count; ++k)
            {
                if (plan.items[k].id == here)
                {
                    next = k;
                    break;
                }
            }
            if (next == count)
                break;
            here = plan.items[next].waitsForId;
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        if (blocked[i])
        {
            plan.items[i].status = Status::ErrorAlreadyExists;
            plan.items[i].waitsForId.clear();
        }
    }

    return plan;
}

} // namespace rename_core
