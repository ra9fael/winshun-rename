// rename_core.h — the pure rule core shared by control mode and oil mode.
//
// Hard constraint (DEVELOPMENT-PLAN.md §4): nothing in core/ may include
// <windows.h> or any Qt header. The L1 tests must build and run anywhere, and
// both modes must share one finalization pipeline so the correctness argument
// is made once.
//
// Consequence worth knowing: Unicode NFC normalization needs an OS facility
// (NormalizeString on Windows, ICU elsewhere), and this layer cannot call it.
// So normalization is *injected* by the platform layer (SetNfcNormalizer). The
// default is identity, meaning "compare the code units as given" — the honest
// behaviour when no normalizer is installed, never a silent guess.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rename_core {

// ---- flags (design §3.4) -----------------------------------------------------
// Borrowed from PowerRename's DWORD layout (RNF-10), trimmed for release one.
// 0x2000 randomize is deliberately absent: it is not reproducible and directly
// conflicts with "the preview is the final result".
enum Flags : std::uint32_t {
    FlagCaseSensitive      = 0x0001,
    FlagMatchAll           = 0x0002,
    FlagRegex              = 0x0004,
    FlagCounters           = 0x0008,
    FlagExcludeFiles       = 0x0010,
    FlagExcludeFolders     = 0x0020,
    FlagExcludeSubfolders  = 0x0040,
    FlagNameOnly           = 0x0080,
    FlagExtensionOnly      = 0x0100,
    FlagUppercase          = 0x0200,
    FlagLowercase          = 0x0400,
    FlagTitleCase          = 0x0800,
    FlagCapitalize         = 0x1000,
    FlagCreatedTime        = 0x4000,
    FlagModifiedTime       = 0x8000,
    FlagAccessedTime       = 0x10000,
    FlagMetadataExif       = 0x20000,
    FlagMetadataXmp        = 0x40000,
};

// Which part of the leaf a rule operates on.
enum class Scope { Name, Extension, Full };

enum class MatchType { Literal, Regex };

enum class Occurrence { First, All };

// ---- statuses (design §3.6, RNF-11) -----------------------------------------
// PowerRename defines ItemNameAlreadyExists but never assigns it (dead code);
// we implement the check for real. That is a deliberate deviation.
enum class Status {
    Init,
    Ready,        // will rename
    Unchanged,    // computed name equals the original under NFC comparison
    ErrorInvalidCharacter,
    ErrorTooLong,
    ErrorAlreadyExists,
    ErrorEmptyName,
    ErrorBadRule,     // regex or template failed to compile/expand
};

std::string_view StatusId(Status s);

// Why a rule could not do its work. Distinct from Status: a status is about a
// file, an error is about the rule itself.
enum class RuleError {
    None,
    BadRegex,       // the pattern did not compile
    BadTemplate,    // the replacement template is malformed
    Unsupported,    // this build does not implement the path yet
};

// ---- NFC normalization (design §3.6, RNF-19) --------------------------------
// Installed once at startup by rename_win (NormalizeString, NormalizationC).
// Tests install their own so L1 stays OS-free.
using NfcNormalizer = std::function<std::wstring(std::wstring_view)>;

void SetNfcNormalizer(NfcNormalizer normalizer);
std::wstring NfcNormalize(std::wstring_view text);   // identity when unset
bool NfcEqual(std::wstring_view a, std::wstring_view b);

// ---- finalization (design §3.6) ---------------------------------------------
// The three steps, in fixed order: trim, transform, compare. Never reorder:
// comparing before transforming would call a case-only rename "unchanged", and
// transforming before trimming would uppercase trailing spaces nobody sees.
struct FinalizeResult {
    std::wstring text;
    Status status = Status::Init;
    bool trimmed = false;      // trailing whitespace or dots were removed
    bool hadNbsp = false;      // a non-breaking space was rewritten as a space
};

FinalizeResult Finalize(std::wstring_view candidate,
                        std::wstring_view originalLeaf,
                        std::uint32_t flags,
                        bool isFolder);

// True when the leaf uses a character Windows refuses in a name.
bool HasInvalidCharacter(std::wstring_view leaf);

// Length check as a delta against the original: 260 for files, 247 for folders.
// `pathLength` is the full original path length, `originalLeaf`/`targetLeaf` are
// leaves. Windows measures in characters, not bytes.
bool ExceedsLength(std::size_t pathLength,
                   std::wstring_view originalLeaf,
                   std::wstring_view targetLeaf,
                   bool isFolder);

// ---- name splitting (design §3.5, RNF-17) -----------------------------------
// `extension` is the final nonempty suffix without its dot, and only when a
// name precedes that dot. archive.tar.gz -> name "archive.tar", extension "gz";
// ".gitignore" and "README" have no extension.
struct SplitName {
    std::wstring stem;       // without the final suffix and without the dot
    std::wstring extension;  // without the dot; empty when there is none
    bool hasExtension = false;
};

SplitName Split(std::wstring_view leaf);
std::wstring Join(std::wstring_view stem, std::wstring_view extension, bool hasExtension);

// The text a rule with this scope sees. For Extension scope on an extensionless
// leaf, `applies` is false: the rule must be skipped with an explanation rather
// than run against an empty string a regex could match.
struct ScopePortion {
    std::wstring text;
    bool applies = true;
};
ScopePortion Portion(std::wstring_view leaf, Scope scope);

// ---- case transforms (design §3.6, RNF-17) ----------------------------------
std::wstring ApplyCase(std::wstring_view text, std::uint32_t flags);

// ---- counters (design §3.7, RNF-16) -----------------------------------------
// Shared across the whole batch, shared by every template inside one chain, and
// advanced only when a chain matched at least once on that item. That last rule
// is what stops unmatched items from consuming numbers.
struct CounterState {
    // `value` is the number the *next* match will print. It starts at `start`
    // so the first match is numbered 1 by default, not 0 — a batch numbered from
    // zero surprises everyone.
    long long start = 1;
    long long value = 1;
    long long increment = 1;
    unsigned padding = 0;
};

std::wstring FormatCounter(const CounterState& counter);

// ---- template context (design §3.7) -----------------------------------------
// Month and weekday names come from the OS locale API, which lives in rename_win.
// The core receives them as strings so it stays portable; the platform layer
// fills them per item.
struct DateParts {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    int hour12() const;   // 12-hour clock, 0 becomes 12
};

enum class TemplateField {
    Year4, Year2, Year1,
    MonthLong, MonthShort, Month2, Month1,
    WeekdayLong, WeekdayShort,
    Day2, Day1,
    Hour12_2, Hour12_1, Hour24_2,
    Minute2, Second2, Milli3, Milli2,
    AmPmUpper, AmPmLower,
};

struct TemplateContext {
    // Metadata values keyed by the pattern name without its "$", e.g.
    // "DATE_TAKEN_YYYY". A missing key leaves the placeholder literal (RNF-23).
    using MetadataMap = std::map<std::wstring, std::wstring>;

    DateParts date;
    std::wstring monthLong;
    std::wstring monthShort;
    std::wstring weekdayLong;
    std::wstring weekdayShort;
    CounterState counter;
    MetadataMap metadata;
};

// Expand "$" templates. "$$" is a literal dollar; "${start=N,increment=N,padding=N}"
// is a counter; date fields follow the table in template.cpp with longest-match
// first; an unrecognised or valueless "$..." sequence stays literal so user text
// is never silently deleted.
std::wstring ExpandTemplate(std::wstring_view input, const TemplateContext& ctx);

// ---- rules and evaluation (design §3.3 / §4.1) ------------------------------
struct Rule {
    bool enabled = true;
    Scope scope = Scope::Name;
    MatchType match = MatchType::Literal;
    Occurrence occurrence = Occurrence::First;
    std::uint32_t flags = 0;
    std::wstring search;
    std::wstring replace;
    RuleError error = RuleError::None;   // set when the rule failed to compile
};

struct RuleChain {
    std::vector<Rule> rules;
};

struct MatchSpan {
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct ReplaceResult {
    std::wstring text;
    std::vector<MatchSpan> hits;          // for highlighting
    bool matched = false;                 // decides whether the counter advances
    RuleError error = RuleError::None;
};

ReplaceResult Replace(std::wstring_view source, const Rule& rule, const TemplateContext& ctx);

// One file as the core sees it. Paths are opaque strings here: identity checks
// and depth belong to the platform layer, which fills them in.
struct ItemSnapshot {
    std::string id;
    std::wstring leaf;
    bool isFolder = false;
    unsigned depth = 0;
    std::size_t pathLength = 0;          // full original path, for the length delta
    TemplateContext templateContext;
};

struct ItemOutcome {
    std::wstring originalLeaf;
    std::wstring targetLeaf;
    bool isFolder = false;
    RuleError error = RuleError::None;
    bool skippedExtensionRule = false;   // "no extension; extension rule not applied"
};

// Evaluate a whole chain against one item. `counter` is shared across the batch
// and is advanced here only when the chain matched (RNF-16).
ItemOutcome Evaluate(const ItemSnapshot& item,
                     const RuleChain& chain,
                     std::uint32_t flags,
                     CounterState& counter);

// ---- oil mode (design §3.8) -------------------------------------------------
// The buffer is a projection of this table, and the table is the single source
// of truth: the id is how a line says "the same object, new name" (RNF-27).
struct OilEntry {
    long long id = 0;
    std::wstring originalLeaf;   // what the entry had when the buffer was built
    std::wstring path;           // full original path, for identity checks
    bool isFolder = false;
    unsigned depth = 0;
};

enum class OilErrorKind {
    MalformedAnchor,      // "/" with no digits
    NoAnchor,             // a non-blank line with no "/<id> " prefix -> create
    UnknownId,            // the id is not in the table (was it edited?)
    DuplicateName,        // two entries would end up with the same leaf
    PathSeparator,        // the leaf contains "/" or "\"
    EmptyName,
    LinkUnsupported,      // the " -> " form, out of scope for release one
    InvalidCharacter,
    TooLong,
    AlreadyExists,
    DeleteUnsupported,    // an id vanished; oil.nvim supports it, we do not
};

struct OilError {
    std::size_t line = 0;        // 1-based, for the gutter
    std::size_t column = 0;      // 1-based; 0 when the whole line is at fault
    OilErrorKind kind = OilErrorKind::NoAnchor;
    std::wstring text;           // the offending line, for the inline note
};

struct OilLine {
    std::size_t line = 0;
    long long id = 0;
    std::wstring leaf;           // trailing separator removed
    bool isFolder = false;
};

struct OilParseResult {
    std::vector<OilLine> lines;
    std::vector<OilError> errors;
};

OilParseResult ParseOilBuffer(std::wstring_view buffer,
                              const std::vector<OilEntry>& idTable);

enum class OilDiffKind { Rename, Rejected };

// One entry's outcome. Release one emits only Rename; everything oil.nvim would
// express as create/delete/move/change is Rejected at this layer, so the
// executor has a single path (RN-FR-19).
struct OilDiff {
    OilDiffKind kind = OilDiffKind::Rejected;
    long long id = 0;
    std::wstring newLeaf;               // Rename only
    OilErrorKind reason = OilErrorKind::NoAnchor;  // Rejected only
    std::size_t line = 0;               // 0 when the diff came from a vanished id
};

std::vector<OilDiff> DiffOilBuffer(const OilParseResult& parsed,
                                   const std::vector<OilEntry>& idTable);

// ---- plan -------------------------------------------------------------------
// One immutable snapshot handed to the executor. The planId is how the UI and
// the worker agree on staleness: any rule or buffer change kills the old id.
struct PlanItem {
    std::string id;            // stable per entry, survives mode switches
    std::wstring originalLeaf;
    std::wstring targetLeaf;
    Status status = Status::Init;
    bool selected = true;
    bool isFolder = false;
    unsigned depth = 0;
    bool trimmed = false;
    bool hadNbsp = false;
};

struct PreviewPlan {
    std::uint64_t planId = 0;
    std::vector<PlanItem> items;

    int CountByStatus(Status s) const;
    bool HasBlockingError() const;
};

// Monotonic plan id. Mint one per snapshot; the UI shows a plan only while its
// id matches the newest.
std::uint64_t NextPlanId();

// Evaluate a buffer into a preview plan using the same finalization pipeline as
// control mode, so both modes assign the same status to the same name
// (RN-FR-20). Parse errors mark the affected rows rather than failing the call.
PreviewPlan BuildOilPlan(std::wstring_view buffer,
                         const std::vector<OilEntry>& idTable,
                         std::uint32_t flags,
                         std::uint64_t planId);

// Evaluate a rule chain over a set of items into a preview plan.
PreviewPlan BuildControlPlan(const std::vector<ItemSnapshot>& items,
                             const RuleChain& chain,
                             std::uint32_t flags,
                             std::uint64_t planId);

} // namespace rename_core
