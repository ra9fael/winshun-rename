// status.cpp — status naming and plan aggregation (design §3.6).
//
// The ID strings are what the UI and the tests share, so they live in one place
// instead of being spelled out in QML and again in the test expectations.
#include "core/rename_core.h"

namespace rename_core {

std::string_view StatusId(Status s)
{
    switch (s)
    {
    case Status::Init:                 return "init";
    case Status::Ready:                return "ready";
    case Status::Unchanged:            return "unchanged";
    case Status::ErrorInvalidCharacter:return "invalid-character";
    case Status::ErrorTooLong:         return "too-long";
    case Status::ErrorAlreadyExists:   return "exists";
    case Status::ErrorEmptyName:       return "empty-name";
    case Status::ErrorBadRule:         return "bad-rule";
    }
    return "init";
}

int PreviewPlan::CountByStatus(Status s) const
{
    int count = 0;
    for (const PlanItem& item : items)
    {
        if (item.status == s)
            ++count;
    }
    return count;
}

bool PreviewPlan::HasBlockingError() const
{
    for (const PlanItem& item : items)
    {
        // An unchecked row is not part of the batch, so its conflict cannot
        // block the rows that are.
        if (!item.selected)
            continue;
        switch (item.status)
        {
        case Status::ErrorInvalidCharacter:
        case Status::ErrorTooLong:
        case Status::ErrorAlreadyExists:
        case Status::ErrorEmptyName:
        case Status::ErrorBadRule:
            return true;
        default:
            break;
        }
    }
    return false;
}

} // namespace rename_core
