// test_core.cpp — L1 tests for the pure core.
//
// Traceability to TEST-PLAN.md is in the section headers. Everything here runs
// with no OS dependency: NFC normalization is injected, dates come from a fixed
// context, and no file is ever touched.
//
// Build & run: cmake --build build --config Release && ctest --test-dir build
#include "core/rename_core.h"
#include "tests/test_harness.h"

#include <algorithm>
#include <string>

using namespace rename_core;

int main() { return rename_test::runAll(); }

namespace {

// Stand-in for NormalizeString(NormalizationC): just enough for the two cases
// the tests care about, and it keeps core/ OS-free.
std::wstring FakeNfc(std::wstring_view text)
{
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'e' && i + 1 < text.size() && text[i + 1] == L'\u0301')
        {
            out.push_back(L'\u00E9');
            ++i;
        }
        else
        {
            out.push_back(text[i]);
        }
    }
    return out;
}

TemplateContext fixedContext()
{
    TemplateContext ctx;
    ctx.date.year = 2026;
    ctx.date.month = 3;
    ctx.date.day = 7;
    ctx.date.hour = 15;
    ctx.date.minute = 4;
    ctx.date.second = 5;
    ctx.date.millisecond = 42;
    ctx.monthLong = L"March";
    ctx.monthShort = L"Mar";
    ctx.weekdayLong = L"Saturday";
    ctx.weekdayShort = L"Sat";
    return ctx;
}

RuleChain singleRule(Scope scope, std::wstring search, std::wstring replace,
                     MatchType type = MatchType::Literal,
                     Occurrence occurrence = Occurrence::First,
                     std::uint32_t flags = 0)
{
    Rule rule;
    rule.scope = scope;
    rule.match = type;
    rule.occurrence = occurrence;
    rule.search = std::move(search);
    rule.replace = std::move(replace);
    rule.flags = flags;
    RuleChain chain;
    chain.rules.push_back(std::move(rule));
    return chain;
}

ItemSnapshot item(const wchar_t* leaf, bool isFolder = false)
{
    ItemSnapshot snapshot;
    snapshot.id = "1";
    snapshot.leaf = leaf;
    snapshot.isFolder = isFolder;
    snapshot.pathLength = 20 + snapshot.leaf.size();
    snapshot.templateContext = fixedContext();
    return snapshot;
}

} // namespace

// ---- RN-T1-06: scope splitting ----------------------------------------------

TEST_CASE(SplitMultiDotName)
{
    const SplitName parts = Split(L"archive.tar.gz");
    CHECK_EQ(parts.stem, std::wstring(L"archive.tar"));
    CHECK_EQ(parts.extension, std::wstring(L"gz"));
    CHECK(parts.hasExtension);
}

TEST_CASE(SplitDotfileHasNoExtension)
{
    const SplitName parts = Split(L".gitignore");
    CHECK_EQ(parts.stem, std::wstring(L".gitignore"));
    CHECK(parts.extension.empty());
    CHECK(!parts.hasExtension);
}

TEST_CASE(SplitNoExtension)
{
    const SplitName parts = Split(L"README");
    CHECK_EQ(parts.stem, std::wstring(L"README"));
    CHECK(!parts.hasExtension);
}

TEST_CASE(SplitTrailingDotIsNotExtension)
{
    const SplitName parts = Split(L"broken.");
    CHECK_EQ(parts.stem, std::wstring(L"broken."));
    CHECK(!parts.hasExtension);
}

TEST_CASE(PortionExtensionSkipsExtensionless)
{
    const ScopePortion portion = Portion(L"README", Scope::Extension);
    CHECK(!portion.applies);
}

TEST_CASE(PortionNameKeepsStemOnly)
{
    const ScopePortion portion = Portion(L"archive.tar.gz", Scope::Name);
    CHECK(portion.applies);
    CHECK_EQ(portion.text, std::wstring(L"archive.tar"));
}

TEST_CASE(JoinRoundTrip)
{
    CHECK_EQ(Join(L"archive.tar", L"gz", true), std::wstring(L"archive.tar.gz"));
    CHECK_EQ(Join(L"README", L"", false), std::wstring(L"README"));
}

// ---- RN-T1-09: NFC equivalence ----------------------------------------------

TEST_CASE(NfcEqualComposedAndDecomposed)
{
    SetNfcNormalizer(FakeNfc);
    CHECK(NfcEqual(L"caf\u00E9", L"cafe\u0301"));
    SetNfcNormalizer(nullptr);
}

TEST_CASE(NfcEqualSharpSDiffersFromSs)
{
    // The whole reason to use NFC instead of a locale collation: German sharp s
    // is a different name from "ss", and a rename tool must not merge them.
    SetNfcNormalizer(FakeNfc);
    CHECK(!NfcEqual(L"Stra\u00DFe", L"Strasse"));
    SetNfcNormalizer(nullptr);
}

TEST_CASE(NfcEqualWithoutNormalizerIsIdentity)
{
    SetNfcNormalizer(nullptr);
    CHECK(NfcEqual(L"caf\u00E9", L"caf\u00E9"));
    CHECK(!NfcEqual(L"caf\u00E9", L"cafe\u0301"));
}

// ---- RN-T1-11: trim boundaries ----------------------------------------------

TEST_CASE(FinalizeTrimsTrailingSpacesAndDots)
{
    const FinalizeResult r = Finalize(L"report . . ", L"report", 0, false);
    CHECK_EQ(r.text, std::wstring(L"report"));
    CHECK(r.trimmed);
    CHECK_EQ(r.status, Status::Unchanged);
}

TEST_CASE(FinalizeTrimsLeadingWhitespace)
{
    const FinalizeResult r = Finalize(L"   draft.txt", L"draft.txt", 0, false);
    CHECK_EQ(r.text, std::wstring(L"draft.txt"));
    CHECK(r.trimmed);
}

TEST_CASE(FinalizeAllWhitespaceIsEmptyName)
{
    // The case that underflows a naive size_t loop: everything trims away.
    const FinalizeResult r = Finalize(L"    ...   ", L"x.txt", 0, false);
    CHECK_EQ(r.status, Status::ErrorEmptyName);
    CHECK(r.text.empty());
}

TEST_CASE(FinalizeRewritesNbspAndReportsIt)
{
    const FinalizeResult r = Finalize(L"my\u00A0file.txt", L"old.txt", 0, false);
    CHECK(r.hadNbsp);
    CHECK_EQ(r.text, std::wstring(L"my file.txt"));
}

TEST_CASE(FinalizeRejectsInvalidCharacter)
{
    const FinalizeResult r = Finalize(L"draft?.txt", L"draft.txt", 0, false);
    CHECK_EQ(r.status, Status::ErrorInvalidCharacter);
}

TEST_CASE(FinalizeCaseOnlyChangeIsAChange)
{
    // Compare runs after transform, so DRAFT.txt -> draft.txt is a real rename.
    const FinalizeResult r = Finalize(L"DRAFT.txt", L"draft.txt", FlagLowercase, false);
    CHECK_EQ(r.text, std::wstring(L"draft.txt"));
    CHECK_EQ(r.status, Status::Unchanged);
}

TEST_CASE(ExceedsLengthUsesDelta)
{
    // Budget is 260 characters for a file, applied as a delta against the path.
    CHECK(!ExceedsLength(250, L"a.txt", L"b.txt", false));
    CHECK(ExceedsLength(258, L"a.txt", L"abcdefgh.txt", false));
    // Directories get the tighter 247 budget.
    CHECK(ExceedsLength(245, L"dir", L"averylongdirectoryname", true));
}

// ---- RN-T1-12: case transforms ----------------------------------------------

TEST_CASE(CaseUppercase) { CHECK_EQ(ApplyCase(L"hello World", FlagUppercase), std::wstring(L"HELLO WORLD")); }
TEST_CASE(CaseLowercase) { CHECK_EQ(ApplyCase(L"Hello World", FlagLowercase), std::wstring(L"hello world")); }
TEST_CASE(CaseCapitalizeEachWord) { CHECK_EQ(ApplyCase(L"hello WORLD", FlagCapitalize), std::wstring(L"Hello World")); }

TEST_CASE(CaseTitleKeepsExceptionWordsLower)
{
    CHECK_EQ(ApplyCase(L"the lord of the rings and more", FlagTitleCase),
             std::wstring(L"The Lord of the Rings and More"));
}

TEST_CASE(CaseTitleCapitalizesFirstAndLastWord)
{
    CHECK_EQ(ApplyCase(L"a tale of two cities", FlagTitleCase),
             std::wstring(L"A Tale of Two Cities"));
}

TEST_CASE(CaseTitleHandlesContraction)
{
    // An apostrophe inside a word must not start a new word: not "Don'T".
    CHECK_EQ(ApplyCase(L"don't stop", FlagTitleCase), std::wstring(L"Don't Stop"));
}

TEST_CASE(CaseNoFlagIsIdentity) { CHECK_EQ(ApplyCase(L"MiXeD", 0), std::wstring(L"MiXeD")); }

// ---- RN-T1-07 / RN-T1-08: date templates and collisions ---------------------

TEST_CASE(TemplateYearMonthDay)
{
    const TemplateContext ctx = fixedContext();
    CHECK_EQ(ExpandTemplate(L"$YYYY-$MM-$DD", ctx), std::wstring(L"2026-03-07"));
    CHECK_EQ(ExpandTemplate(L"$YY/$M/$D", ctx), std::wstring(L"26/3/7"));
    CHECK_EQ(ExpandTemplate(L"$Y", ctx), std::wstring(L"6"));
}

TEST_CASE(TemplateMonthAndWeekdayNames)
{
    const TemplateContext ctx = fixedContext();
    CHECK_EQ(ExpandTemplate(L"$MMMM $MMM $DDDD $DDD", ctx), std::wstring(L"March Mar Saturday Sat"));
}

TEST_CASE(TemplateTime)
{
    const TemplateContext ctx = fixedContext();
    // 15:04:05.042 -> 12-hour is 3 PM.
    CHECK_EQ(ExpandTemplate(L"$HH $hh $mm $ss", ctx), std::wstring(L"03 15 04 05"));
    CHECK_EQ(ExpandTemplate(L"$TT $tt", ctx), std::wstring(L"PM pm"));
    CHECK_EQ(ExpandTemplate(L"$fff $ff", ctx), std::wstring(L"042 04"));
}

TEST_CASE(TemplateLongestMatchWins)
{
    const TemplateContext ctx = fixedContext();
    // If "$Y" won over "$YYYY" this would read "2026" as "2 026".
    CHECK_EQ(ExpandTemplate(L"$YYYY", ctx), std::wstring(L"2026"));
    CHECK_EQ(ExpandTemplate(L"$MMMM", ctx), std::wstring(L"March"));
}

TEST_CASE(TemplateEscapedDollar)
{
    const TemplateContext ctx = fixedContext();
    CHECK_EQ(ExpandTemplate(L"$$YYYY", ctx), std::wstring(L"$YYYY"));
    CHECK_EQ(ExpandTemplate(L"cost $$5", ctx), std::wstring(L"cost $5"));
}

TEST_CASE(TemplateDollarDDoesNotEatMetadata)
{
    const TemplateContext ctx = fixedContext();
    // "$DATE_TAKEN_YYYY" must survive: the "$D" in it is not a day field.
    CHECK_EQ(ExpandTemplate(L"$DATE_TAKEN_YYYY", ctx), std::wstring(L"$DATE_TAKEN_YYYY"));
    CHECK_EQ(ExpandTemplate(L"$DESCRIPTION", ctx), std::wstring(L"$DESCRIPTION"));
    CHECK_EQ(ExpandTemplate(L"$DOCUMENT_ID", ctx), std::wstring(L"$DOCUMENT_ID"));
    // A standalone "$D" still works.
    CHECK_EQ(ExpandTemplate(L"$D", ctx), std::wstring(L"7"));
}

TEST_CASE(TemplateDollarHDoesNotEatHeight)
{
    const TemplateContext ctx = fixedContext();
    CHECK_EQ(ExpandTemplate(L"$HEIGHT", ctx), std::wstring(L"$HEIGHT"));
    CHECK_EQ(ExpandTemplate(L"$H", ctx), std::wstring(L"3"));
}

TEST_CASE(TemplateCounter)
{
    TemplateContext ctx = fixedContext();
    ctx.counter.value = 7;
    CHECK_EQ(ExpandTemplate(L"${}", ctx), std::wstring(L"7"));
    CHECK_EQ(ExpandTemplate(L"${padding=3}", ctx), std::wstring(L"007"));
    CHECK_EQ(ExpandTemplate(L"${start=100}", ctx), std::wstring(L"7")); // start only seeds a new run
}

TEST_CASE(TemplateUnknownStaysLiteral)
{
    const TemplateContext ctx = fixedContext();
    CHECK_EQ(ExpandTemplate(L"$NOT_A_FIELD", ctx), std::wstring(L"$NOT_A_FIELD"));
    CHECK_EQ(ExpandTemplate(L"trailing$", ctx), std::wstring(L"trailing$"));
}

TEST_CASE(TemplateMetadataSubstitutesWhenPresent)
{
    TemplateContext ctx = fixedContext();
    ctx.metadata[L"DATE_TAKEN_YYYY"] = L"2019";
    ctx.metadata[L"CAMERA_MODEL"] = L"X100V";
    CHECK_EQ(ExpandTemplate(L"$DATE_TAKEN_YYYY", ctx), std::wstring(L"2019"));
    CHECK_EQ(ExpandTemplate(L"$CAMERA_MODEL-shot", ctx), std::wstring(L"X100V-shot"));
}

TEST_CASE(TemplateMetadataLongestKeyWins)
{
    // "$DATE_TAKEN_YYYY" must not be consumed as "$DATE_TAKEN", or the tail "YYYY"
    // would survive as literal text.
    TemplateContext ctx = fixedContext();
    ctx.metadata[L"DATE_TAKEN"] = L"2019-05-02";
    ctx.metadata[L"DATE_TAKEN_YYYY"] = L"2019";
    CHECK_EQ(ExpandTemplate(L"$DATE_TAKEN_YYYY", ctx), std::wstring(L"2019"));
    CHECK_EQ(ExpandTemplate(L"$DATE_TAKEN", ctx), std::wstring(L"2019-05-02"));
}

TEST_CASE(TemplateMetadataAbsentStaysLiteral)
{
    // RNF-23: a value that could not be read leaves the placeholder visible rather
    // than becoming an empty string that silently shortens the name.
    const TemplateContext ctx = fixedContext();
    CHECK_EQ(ExpandTemplate(L"$DATE_TAKEN_YYYY", ctx), std::wstring(L"$DATE_TAKEN_YYYY"));
}

// ---- RN-T1-10: counter scoping ----------------------------------------------

TEST_CASE(CounterNoPadding)
{
    CounterState counter;
    counter.value = 7;
    CHECK_EQ(FormatCounter(counter), std::wstring(L"7"));
}

TEST_CASE(CounterPadding)
{
    CounterState counter;
    counter.value = 7;
    counter.padding = 3;
    CHECK_EQ(FormatCounter(counter), std::wstring(L"007"));
}

TEST_CASE(CounterLargePaddingDoesNotExplode)
{
    CounterState counter;
    counter.value = 42;
    counter.padding = 100000;
    const std::wstring out = FormatCounter(counter);
    CHECK(out.find(L"42") != std::wstring::npos);
    CHECK(out.size() < 300);
}

TEST_CASE(CounterAdvancesOnlyOnMatch)
{
    // Three items; the middle one does not match. The batch numbers must be 1, 2
    // with no gap, not 1, 3 (RNF-16).
    const RuleChain chain = singleRule(Scope::Name, L"IMG", L"pic-${}", MatchType::Literal,
                                       Occurrence::All, FlagCounters);
    CounterState counter;
    counter.increment = 1;

    ItemSnapshot first = item(L"IMG_0001.jpg");
    ItemSnapshot middle = item(L"notes.txt");
    ItemSnapshot third = item(L"IMG_0002.jpg");

    const ItemOutcome a = Evaluate(first, chain, FlagCounters, counter);
    const ItemOutcome b = Evaluate(middle, chain, FlagCounters, counter);
    const ItemOutcome c = Evaluate(third, chain, FlagCounters, counter);

    CHECK_EQ(a.targetLeaf, std::wstring(L"pic-1_0001.jpg"));
    CHECK_EQ(b.targetLeaf, std::wstring(L"notes.txt"));
    CHECK_EQ(c.targetLeaf, std::wstring(L"pic-2_0002.jpg"));
}

TEST_CASE(CounterSharedWithinOneItem)
{
    // Two counter templates in one chain print the same number, not 1 and 2.
    const RuleChain chain = singleRule(Scope::Name, L"x", L"${}-${}", MatchType::Literal,
                                       Occurrence::First, FlagCounters);
    CounterState counter;
    counter.value = 5;

    const ItemOutcome out = Evaluate(item(L"x.txt"), chain, FlagCounters, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"5-5.txt"));
}

// ---- rule engine -------------------------------------------------------------

TEST_CASE(LiteralFirstVsAll)
{
    CounterState counter;
    const ItemOutcome first = Evaluate(item(L"a-a-a.txt"),
                                       singleRule(Scope::Name, L"a", L"b"), 0, counter);
    CHECK_EQ(first.targetLeaf, std::wstring(L"b-a-a.txt"));

    CounterState counter2;
    const ItemOutcome all = Evaluate(item(L"a-a-a.txt"),
                                     singleRule(Scope::Name, L"a", L"b", MatchType::Literal,
                                                Occurrence::All), 0, counter2);
    CHECK_EQ(all.targetLeaf, std::wstring(L"b-b-b.txt"));
}

TEST_CASE(LiteralIsCaseInsensitiveByDefault)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"Draft.txt"),
                                     singleRule(Scope::Name, L"draft", L"final"), 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"final.txt"));
}

TEST_CASE(LiteralCaseSensitiveWhenFlagSet)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"Draft.txt"),
                                     singleRule(Scope::Name, L"draft", L"final", MatchType::Literal,
                                                Occurrence::First, FlagCaseSensitive), 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"Draft.txt"));
}

TEST_CASE(RegexCaptureGroup)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"IMG_0042.jpg"),
                                     singleRule(Scope::Name, L"IMG_(\\d+)", L"photo_$1",
                                                MatchType::Regex), 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"photo_0042.jpg"));
}

TEST_CASE(RegexBadPatternIsAnError)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"a.txt"),
                                     singleRule(Scope::Name, L"([", L"x", MatchType::Regex), 0, counter);
    CHECK_EQ(out.error, RuleError::BadRegex);
    // The name must be left untouched: never a partially-renamed file.
    CHECK_EQ(out.targetLeaf, std::wstring(L"a.txt"));
}

TEST_CASE(ExtensionScopeMatchesOnlySuffix)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"report.JPG"),
                                     singleRule(Scope::Extension, L"JPG", L"jpg", MatchType::Regex,
                                                Occurrence::All, FlagCaseSensitive), 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"report.jpg"));
}

TEST_CASE(ExtensionScopeSkipsExtensionlessFile)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"README"),
                                     singleRule(Scope::Extension, L".*", L"txt", MatchType::Regex,
                                                Occurrence::All), 0, counter);
    // A regex that matches the empty string must not invent a suffix.
    CHECK_EQ(out.targetLeaf, std::wstring(L"README"));
    CHECK(out.skippedExtensionRule);
}

TEST_CASE(ExtensionScopeSkipsDirectory)
{
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"photos", true),
                                     singleRule(Scope::Extension, L"s", L"x"), 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"photos"));
    CHECK(out.skippedExtensionRule);
}

TEST_CASE(ChainResplitsAfterEachRule)
{
    // Rule 1 renames the whole leaf and adds a suffix; rule 2 is extension-scoped
    // and must see the *new* suffix, not the original one.
    RuleChain chain;
    {
        Rule rule;
        rule.scope = Scope::Full;
        rule.search = L"a.txt";
        rule.replace = L"b.md";
        chain.rules.push_back(rule);
    }
    {
        Rule rule;
        rule.scope = Scope::Extension;
        rule.search = L"md";
        rule.replace = L"json";
        chain.rules.push_back(rule);
    }

    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"a.txt"), chain, 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"b.json"));
}

TEST_CASE(DisabledRuleIsSkipped)
{
    RuleChain chain = singleRule(Scope::Name, L"a", L"b");
    chain.rules[0].enabled = false;
    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"a.txt"), chain, 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"a.txt"));
}

// ---- plan aggregation --------------------------------------------------------

TEST_CASE(PlanDetectsBatchDuplicateTargets)
{
    RuleChain chain = singleRule(Scope::Name, L".*", L"same", MatchType::Regex, Occurrence::All);
    std::vector<ItemSnapshot> items = {item(L"one.txt"), item(L"two.txt")};
    items[0].id = "1";
    items[1].id = "2";

    const PreviewPlan plan = BuildControlPlan(items, chain, 0, NextPlanId());
    CHECK_EQ(plan.CountByStatus(Status::ErrorAlreadyExists), 2);
    CHECK(plan.HasBlockingError());
}

TEST_CASE(PlanDetectsTargetAlreadyExists)
{
    // "a.txt" -> "b.txt" while "b.txt" is in the batch and staying put.
    RuleChain chain = singleRule(Scope::Name, L"a", L"b");
    std::vector<ItemSnapshot> items = {item(L"a.txt"), item(L"b.txt")};
    items[0].id = "1";
    items[1].id = "2";

    const PreviewPlan plan = BuildControlPlan(items, chain, 0, NextPlanId());
    bool blocked = false;
    for (const PlanItem& row : plan.items)
    {
        if (row.id == "1" && row.status == Status::ErrorAlreadyExists)
            blocked = true;
    }
    CHECK(blocked);
}

TEST_CASE(PlanWaitsForTheRowThatFreesItsTarget)
{
    // The real case, straight from the window: replacing 报告 with 2026报告 in a
    // folder that already holds "2026报告.pdf".
    //   报告.pdf      -> 2026报告.pdf        (takes the other row's current name)
    //   2026报告.pdf  -> 20262026报告.pdf    (frees it in the same batch)
    // Neither row may block — the batch is legal — but "报告.pdf" can only run
    // *after* the row that frees its target, and the plan must say so.
    std::vector<ItemSnapshot> items = {item(L"报告.pdf"), item(L"2026报告.pdf")};
    items[0].id = "1";
    items[1].id = "2";

    const RuleChain chain = singleRule(Scope::Name, L"报告", L"2026报告");
    const PreviewPlan plan = BuildControlPlan(items, chain, 0, NextPlanId());

    bool blocked = false;
    for (const PlanItem& row : plan.items)
    {
        if (row.status == Status::ErrorAlreadyExists)
            blocked = true;
    }
    CHECK(!blocked);
    CHECK_EQ(plan.CountByStatus(Status::Ready), 2);

    // Row 1's target is row 2's current name, so row 1 waits for row 2.
    bool recorded = false;
    for (const PlanItem& row : plan.items)
    {
        if (row.id == "1")
            recorded = row.waitsForId == "2";
    }
    CHECK(recorded);
}

TEST_CASE(PlanMarksUnchangedWhenNoRuleMatches)
{
    const RuleChain chain = singleRule(Scope::Name, L"zzz", L"yyy");
    std::vector<ItemSnapshot> items = {item(L"notes.txt")};
    const PreviewPlan plan = BuildControlPlan(items, chain, 0, NextPlanId());
    CHECK_EQ(plan.items[0].status, Status::Unchanged);
    CHECK(!plan.HasBlockingError());
}

TEST_CASE(PlanIdsAreMonotonic)
{
    const std::uint64_t a = NextPlanId();
    const std::uint64_t b = NextPlanId();
    CHECK(b > a);
}

// ---- RN-T1-13..16: oil parser and differ ------------------------------------

namespace {

std::vector<OilEntry> oilTable()
{
    OilEntry a;
    a.id = 1042;
    a.originalLeaf = L"report-draft.txt";
    a.path = L"C:\\tmp\\report-draft.txt";
    a.isFolder = false;

    OilEntry b;
    b.id = 1043;
    b.originalLeaf = L"notes.txt";
    b.path = L"C:\\tmp\\notes.txt";
    b.isFolder = false;

    OilEntry dir;
    dir.id = 1044;
    dir.originalLeaf = L"photos";
    dir.path = L"C:\\tmp\\photos";
    dir.isFolder = true;

    return {a, b, dir};
}

} // namespace

TEST_CASE(OilParserReadsStateIdAndName)
{
    const std::wstring buffer = L"y 1042 report-draft.txt\nn 1043 notes.txt\nd 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());

    CHECK(parsed.errors.empty());
    CHECK_EQ(parsed.lines.size(), std::size_t(3));
    CHECK_EQ(parsed.lines[0].id, 1042LL);
    CHECK_EQ(parsed.lines[0].leaf, std::wstring(L"report-draft.txt"));
    CHECK_EQ(parsed.lines[0].state, OilState::Rename);
    CHECK_EQ(parsed.lines[1].leaf, std::wstring(L"notes.txt"));
    CHECK_EQ(parsed.lines[1].state, OilState::Skip);
    CHECK_EQ(parsed.lines[2].leaf, std::wstring(L"photos"));
    CHECK_EQ(parsed.lines[2].state, OilState::Delete);
}

TEST_CASE(OilParserAcceptsUppercaseStateLetters)
{
    const OilParseResult parsed = ParseOilBuffer(L"Y 1042 a.txt\nN 1043 b.txt\nD 1044 c.txt\n", oilTable());
    CHECK(parsed.errors.empty());
    CHECK_EQ(parsed.lines.size(), std::size_t(3));
    CHECK_EQ(parsed.lines[0].state, OilState::Rename);
    CHECK_EQ(parsed.lines[1].state, OilState::Skip);
    CHECK_EQ(parsed.lines[2].state, OilState::Delete);
}

TEST_CASE(OilParserAcceptsNamesWithSpaces)
{
    const OilParseResult parsed = ParseOilBuffer(L"y 1042 my long name.txt\n", oilTable());
    CHECK(parsed.errors.empty());
    CHECK_EQ(parsed.lines.size(), std::size_t(1));
    CHECK_EQ(parsed.lines[0].leaf, std::wstring(L"my long name.txt"));
}

TEST_CASE(OilParserRejectsLineWithoutStateLetter)
{
    // A bare name is the "create" gesture, which release one refuses.
    const OilParseResult parsed = ParseOilBuffer(L"y 1042 a.txt\nhello.txt\n", oilTable());
    CHECK_EQ(parsed.errors.size(), std::size_t(1));
    CHECK_EQ(parsed.errors[0].line, std::size_t(2));

    // A digit first is not a state letter either.
    const OilParseResult digits = ParseOilBuffer(L"1042 a.txt\n", oilTable());
    CHECK_EQ(digits.errors.size(), std::size_t(1));
    CHECK_EQ(digits.errors[0].kind, OilErrorKind::NoAnchor);
}

TEST_CASE(OilParserRejectsUnknownStateLetter)
{
    const OilParseResult parsed = ParseOilBuffer(L"x 1042 a.txt\n", oilTable());
    CHECK_EQ(parsed.errors.size(), std::size_t(1));
    CHECK_EQ(parsed.errors[0].kind, OilErrorKind::UnknownState);
}

TEST_CASE(OilParserRejectsIndentedLine)
{
    // The first column is the state, so leading indentation can no longer be
    // treated as presentation-only.
    const OilParseResult parsed = ParseOilBuffer(L"  y 1042 a.txt\n", oilTable());
    CHECK_EQ(parsed.errors.size(), std::size_t(1));
}

TEST_CASE(OilParserRejectsMalformedAnchor)
{
    const OilParseResult missingId = ParseOilBuffer(L"y abc name.txt\n", oilTable());
    CHECK_EQ(missingId.errors.size(), std::size_t(1));
    CHECK_EQ(missingId.errors[0].kind, OilErrorKind::MalformedAnchor);

    // Digits glued to the name would silently attach the rename to the wrong file.
    const OilParseResult glued = ParseOilBuffer(L"y 1042a.txt\n", oilTable());
    CHECK_EQ(glued.errors.size(), std::size_t(1));
    CHECK_EQ(glued.errors[0].kind, OilErrorKind::MalformedAnchor);

    // std::stoll would throw on this one; it must be reported, not crash.
    const OilParseResult overflow = ParseOilBuffer(L"y 99999999999999999999 a.txt\n", oilTable());
    CHECK_EQ(overflow.errors.size(), std::size_t(1));
    CHECK_EQ(overflow.errors[0].kind, OilErrorKind::MalformedAnchor);
}

TEST_CASE(OilParserRejectsPathSeparator)
{
    // A rename must never move the item.
    const OilParseResult parsed = ParseOilBuffer(L"y 1042 sub/report.txt\n", oilTable());
    CHECK_EQ(parsed.errors.size(), std::size_t(1));
    CHECK_EQ(parsed.errors[0].kind, OilErrorKind::PathSeparator);
}

TEST_CASE(OilParserRejectsLinkSyntax)
{
    const OilParseResult parsed = ParseOilBuffer(L"y 1042 notes -> /target\n", oilTable());
    CHECK_EQ(parsed.errors.size(), std::size_t(1));
    CHECK_EQ(parsed.errors[0].kind, OilErrorKind::LinkUnsupported);
}

TEST_CASE(OilParserToleratesCrlfAndBlankLines)
{
    const std::wstring buffer = L"y 1042 report-draft.txt\r\n\r\nn 1043 notes.txt\r\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    CHECK(parsed.errors.empty());
    CHECK_EQ(parsed.lines.size(), std::size_t(2));
}

TEST_CASE(OilDifferEmitsRenameForChangedLeaf)
{
    const std::wstring buffer = L"y 1042 report-final.txt\nn 1043 notes.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, oilTable());

    CHECK_EQ(diffs.size(), std::size_t(1));
    CHECK_EQ(diffs[0].kind, OilDiffKind::Rename);
    CHECK_EQ(diffs[0].id, 1042LL);
    CHECK_EQ(diffs[0].newLeaf, std::wstring(L"report-final.txt"));
}

TEST_CASE(OilDifferNoDiffWhenUnchanged)
{
    const std::wstring buffer = L"y 1042 report-draft.txt\nn 1043 notes.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    CHECK(DiffOilBuffer(parsed, oilTable()).empty());
}

TEST_CASE(OilDifferRejectsUnknownId)
{
    const std::wstring buffer = L"y 9999 ghost.txt\ny 1042 report-draft.txt\nn 1043 notes.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, oilTable());

    bool sawUnknown = false;
    for (const OilDiff& diff : diffs)
    {
        if (diff.kind == OilDiffKind::Rejected && diff.reason == OilErrorKind::UnknownId)
            sawUnknown = true;
    }
    CHECK(sawUnknown);
}

TEST_CASE(OilDifferRejectsVanishedId)
{
    // Removing a line is a mistake now: deleting is spelled "d" so an accidental
    // line removal can never delete a file.
    const std::wstring buffer = L"y 1042 report-draft.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, oilTable());

    bool sawMissing = false;
    for (const OilDiff& diff : diffs)
    {
        if (diff.kind == OilDiffKind::Rejected && diff.reason == OilErrorKind::MissingLine
            && diff.id == 1043)
            sawMissing = true;
    }
    CHECK(sawMissing);
}

TEST_CASE(OilDifferTurnsDStateIntoDelete)
{
    const std::wstring buffer = L"y 1042 report-draft.txt\nd 1043 notes.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, oilTable());

    CHECK_EQ(diffs.size(), std::size_t(1));
    CHECK_EQ(diffs[0].kind, OilDiffKind::Delete);
    CHECK_EQ(diffs[0].id, 1043LL);
}

TEST_CASE(OilPlanMakesDStateAPendingDelete)
{
    const PreviewPlan plan = BuildOilPlan(L"y 1042 report-draft.txt\nd 1043 notes.txt\ny 1044 photos\n",
                                          oilTable(), 0, NextPlanId());
    const PlanItem* row = nullptr;
    for (const PlanItem& candidate : plan.items)
        if (candidate.id == "1043") row = &candidate;
    CHECK(row != nullptr);
    if (row)
    {
        CHECK_EQ(row->action, Action::Delete);
        CHECK_EQ(row->status, Status::Ready);
    }
}

TEST_CASE(OilPlanRejectsAMissingLine)
{
    // A user who deletes a line by accident gets a blocking error, not a delete.
    const PreviewPlan plan = BuildOilPlan(L"y 1042 report-draft.txt\ny 1044 photos\n",
                                          oilTable(), 0, NextPlanId());
    CHECK(plan.HasBlockingError());
    for (const PlanItem& row : plan.items)
    {
        if (row.id != "1043")
            continue;
        CHECK_EQ(row.action, Action::Rename);
        CHECK_EQ(row.status, Status::ErrorBadRule);
    }
}

TEST_CASE(OilPlanUsesNStateAsUnselected)
{
    const PreviewPlan plan = BuildOilPlan(L"y 1042 report-draft.txt\nn 1043 notes.txt\ny 1044 photos\n",
                                          oilTable(), 0, NextPlanId());
    const PlanItem* row = nullptr;
    for (const PlanItem& candidate : plan.items)
        if (candidate.id == "1043") row = &candidate;
    CHECK(row != nullptr);
    if (row)
        CHECK(!row->selected);
}

TEST_CASE(OilPlanTreatsAnEmptyBufferAsAnError)
{
    // An empty buffer is far more likely to be a paste accident than a request to
    // delete every file, so it blocks instead of deleting.
    const PreviewPlan plan = BuildOilPlan(L"", oilTable(), 0, NextPlanId());
    CHECK(plan.HasBlockingError());
    for (const PlanItem& row : plan.items)
        CHECK_EQ(row.action, Action::Rename);
}

TEST_CASE(OilDifferRejectsDuplicateId)
{
    const std::wstring buffer = L"y 1042 a.txt\ny 1042 b.txt\nn 1043 notes.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, oilTable());
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, oilTable());

    bool sawDuplicate = false;
    for (const OilDiff& diff : diffs)
    {
        if (diff.kind == OilDiffKind::Rejected && diff.reason == OilErrorKind::DuplicateId)
            sawDuplicate = true;
    }
    CHECK(sawDuplicate);
}

TEST_CASE(OilDifferRejectsDuplicateNamesCaseInsensitively)
{
    // Windows would treat these as the same name.
    std::vector<OilEntry> table = oilTable();
    const std::wstring buffer = L"y 1042 Report.txt\ny 1043 report.txt\ny 1044 photos\n";
    const OilParseResult parsed = ParseOilBuffer(buffer, table);
    const std::vector<OilDiff> diffs = DiffOilBuffer(parsed, table);

    int duplicates = 0;
    for (const OilDiff& diff : diffs)
    {
        if (diff.kind == OilDiffKind::Rejected && diff.reason == OilErrorKind::DuplicateName)
            ++duplicates;
    }
    CHECK_EQ(duplicates, 1);
}

TEST_CASE(OilPlanUsesSharedFinalization)
{
    // Trailing dots get trimmed by the same pipeline control mode uses.
    const std::wstring buffer = L"y 1042 report-draft.txt.\nn 1043 notes.txt\ny 1044 photos\n";
    const PreviewPlan plan = BuildOilPlan(buffer, oilTable(), 0, NextPlanId());

    const PlanItem* row = nullptr;
    for (const PlanItem& candidate : plan.items)
    {
        if (candidate.id == "1042")
            row = &candidate;
    }
    CHECK(row != nullptr);
    if (row)
    {
        CHECK_EQ(row->targetLeaf, std::wstring(L"report-draft.txt"));
        CHECK(row->trimmed);
        CHECK_EQ(row->status, Status::Unchanged);
    }
}

TEST_CASE(OilPlanMarksParseErrorsAsBlocking)
{
    const std::wstring buffer = L"y 1042 report-draft.txt\nno-anchor-here\nn 1043 notes.txt\ny 1044 photos\n";
    const PreviewPlan plan = BuildOilPlan(buffer, oilTable(), 0, NextPlanId());
    CHECK(plan.HasBlockingError());
}

// ---- RN-FR-20 / RN-T2-06: both modes agree ----------------------------------

TEST_CASE(BothModesAgreeOnStatusForSameName)
{
    // The same illegal name must be rejected identically whether it came from a
    // rule chain or from a text-mode edit.
    const std::wstring illegal = L"bad?.txt";

    const FinalizeResult viaControl = Finalize(illegal, L"orig.txt", 0, false);

    std::vector<OilEntry> table = oilTable();
    table[0].originalLeaf = L"orig.txt";
    const std::wstring buffer = L"y 1042 bad?.txt\nn 1043 notes.txt\ny 1044 photos\n";
    const PreviewPlan oilPlan = BuildOilPlan(buffer, table, 0, NextPlanId());

    Status oilStatus = Status::Init;
    for (const PlanItem& row : oilPlan.items)
    {
        if (row.id == "1042")
            oilStatus = row.status;
    }

    CHECK_EQ(viaControl.status, Status::ErrorInvalidCharacter);
    CHECK_EQ(oilStatus, Status::ErrorInvalidCharacter);
}

// ---- chains: order, disabled rules, and the shared counter -------------------

TEST_CASE(ChainAppliesRulesInOrder)
{
    // "a->b" then "b->c" must end at c; reversed, it never leaves a.
    RuleChain forward;
    forward.rules.push_back(singleRule(Scope::Name, L"a", L"b", MatchType::Literal,
                                       Occurrence::All).rules[0]);
    forward.rules.push_back(singleRule(Scope::Name, L"b", L"c", MatchType::Literal,
                                       Occurrence::All).rules[0]);
    RuleChain reversed = forward;
    std::reverse(reversed.rules.begin(), reversed.rules.end());

    CounterState counter;
    const ItemOutcome forwardOut = Evaluate(item(L"aaa.txt"), forward, 0, counter);
    CounterState counterReversed;
    const ItemOutcome reversedOut = Evaluate(item(L"aaa.txt"), reversed, 0, counterReversed);

    CHECK_EQ(forwardOut.targetLeaf, std::wstring(L"ccc.txt"));
    // Reversed, "a->b" runs last: the batch ends as bbb and never reaches c.
    CHECK_EQ(reversedOut.targetLeaf, std::wstring(L"bbb.txt"));
    CHECK(forwardOut.error == RuleError::None);
}

TEST_CASE(DisabledRuleIsSkipped)
{
    RuleChain chain;
    const Rule disabled = [&] {
        Rule rule = singleRule(Scope::Name, L"a", L"X", MatchType::Literal,
                               Occurrence::All).rules[0];
        rule.enabled = false;
        return rule;
    }();
    chain.rules.push_back(disabled);
    chain.rules.push_back(singleRule(Scope::Name, L"a", L"Y", MatchType::Literal,
                                     Occurrence::All).rules[0]);

    CounterState counter;
    const ItemOutcome out = Evaluate(item(L"aaa.txt"), chain, 0, counter);
    CHECK_EQ(out.targetLeaf, std::wstring(L"YYY.txt"));
}

TEST_CASE(RegexCompilesMirrorsReplace)
{
    CHECK(RegexCompiles(L"a(b)?c", false));
    CHECK(RegexCompiles(L"", false));
    CHECK(!RegexCompiles(L"a(b", true));
    // Literal search is never a regex: an invalid pattern with regex off is the
    // caller's business, but the check itself must match ReplaceRegex's flags.
    CHECK(!RegexCompiles(L"[a-", false));
}
