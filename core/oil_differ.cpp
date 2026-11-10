// oil_differ.cpp — parsed lines to rename diffs (design §3.8).
//
// The layering follows oil.nvim's (RNF-30) with one deliberate narrowing: the
// action layer only ever sees `Rename`. oil.nvim also produces create, delete,
// move and change; release one turns all of those into Rejected at *this* layer,
// so the executor has a single code path and the test surface stays small
// (RN-FR-19, RN-T2-07).
#include "core/rename_core.h"

#include <algorithm>
#include <cwctype>

namespace rename_core {

namespace {

std::wstring Fold(std::wstring_view text)
{
    std::wstring out(text);
    for (wchar_t& ch : out)
        ch = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    return out;
}

} // namespace

std::vector<OilDiff> DiffOilBuffer(const OilParseResult& parsed,
                                   const std::vector<OilEntry>& idTable)
{
    std::vector<OilDiff> diffs;
    std::vector<bool> seen(idTable.size(), false);

    // Windows comparison is case-insensitive, so duplicate detection must be too
    // or "Report.txt" and "report.txt" would both be allowed (RNF-31).
    std::vector<std::wstring> foldedNames;
    foldedNames.reserve(parsed.lines.size());

    for (const OilLine& line : parsed.lines)
    {
        const auto it = std::find_if(idTable.begin(), idTable.end(),
                                     [&](const OilEntry& entry) { return entry.id == line.id; });
        if (it == idTable.end())
        {
            diffs.push_back({OilDiffKind::Rejected, line.id, {}, OilErrorKind::UnknownId, line.line});
            continue;
        }

        seen[static_cast<std::size_t>(std::distance(idTable.begin(), it))] = true;

        const std::wstring folded = Fold(line.leaf);
        if (std::find(foldedNames.begin(), foldedNames.end(), folded) != foldedNames.end())
        {
            diffs.push_back({OilDiffKind::Rejected, line.id, {}, OilErrorKind::DuplicateName, line.line});
            continue;
        }
        foldedNames.push_back(folded);

        // A directory marker that disagrees with the table would be a type
        // change, not a rename. Refuse rather than silently ignoring the slash.
        if (line.isFolder != it->isFolder)
        {
            diffs.push_back({OilDiffKind::Rejected, line.id, {}, OilErrorKind::PathSeparator, line.line});
            continue;
        }

        if (line.leaf == it->originalLeaf)
            continue;   // untouched: no diff, no action

        diffs.push_back({OilDiffKind::Rename, line.id, line.leaf, OilErrorKind::NoAnchor, line.line});
    }

    // Any id that vanished from the buffer would be a delete. Refuse it rather
    // than quietly doing nothing: an accidental line removal is exactly the case
    // where silence would be dangerous.
    for (std::size_t i = 0; i < idTable.size(); ++i)
    {
        if (!seen[i])
            diffs.push_back({OilDiffKind::Rejected, idTable[i].id, {}, OilErrorKind::DeleteUnsupported, 0});
    }

    return diffs;
}

PreviewPlan BuildOilPlan(const std::wstring_view buffer,
                         const std::vector<OilEntry>& idTable,
                         std::uint32_t flags,
                         std::uint64_t planId)
{
    PreviewPlan plan;
    plan.planId = planId;

    const OilParseResult parsed = ParseOilBuffer(buffer, idTable);
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, idTable);

    // A parse error is a blocking condition: the buffer is not trustworthy, so
    // every row it describes is marked rather than partially applied.
    std::vector<std::size_t> errorLines;
    for (const OilError& error : parsed.errors)
        errorLines.push_back(error.line);

    for (const OilEntry& entry : idTable)
    {
        PlanItem item;
        item.id = std::to_string(entry.id);
        item.originalLeaf = entry.originalLeaf;
        item.targetLeaf = entry.originalLeaf;
        item.isFolder = entry.isFolder;
        item.depth = entry.depth;

        const auto diff = std::find_if(diffs.begin(), diffs.end(),
                                       [&](const OilDiff& d) { return d.id == entry.id; });

        if (diff == diffs.end())
        {
            item.status = Status::Unchanged;
        }
        else if (diff->kind == OilDiffKind::Rejected)
        {
            switch (diff->reason)
            {
            case OilErrorKind::DeleteUnsupported:
            case OilErrorKind::UnknownId:
                item.status = Status::ErrorBadRule;
                break;
            case OilErrorKind::DuplicateName:
            case OilErrorKind::AlreadyExists:
                item.status = Status::ErrorAlreadyExists;
                break;
            case OilErrorKind::PathSeparator:
                item.status = Status::ErrorInvalidCharacter;
                break;
            case OilErrorKind::TooLong:
                item.status = Status::ErrorTooLong;
                break;
            case OilErrorKind::EmptyName:
                item.status = Status::ErrorEmptyName;
                break;
            default:
                item.status = Status::ErrorBadRule;
                break;
            }
        }
        else
        {
            // Same finalization pipeline as control mode, so both modes agree on
            // what a given name means (RN-FR-20, RN-T2-06).
            const FinalizeResult finalized = Finalize(diff->newLeaf, entry.originalLeaf, flags, entry.isFolder);
            item.targetLeaf = finalized.text;
            item.status = finalized.status;
            item.trimmed = finalized.trimmed;
            item.hadNbsp = finalized.hadNbsp;

            if (item.status == Status::Ready &&
                ExceedsLength(entry.path.size(), entry.originalLeaf, finalized.text, entry.isFolder))
            {
                item.status = Status::ErrorTooLong;
            }
        }

        // A parse error anywhere makes this row untrustworthy too.
        if (!errorLines.empty() && item.status == Status::Unchanged)
            item.status = Status::ErrorBadRule;

        plan.items.push_back(std::move(item));
    }

    return plan;
}

} // namespace rename_core
