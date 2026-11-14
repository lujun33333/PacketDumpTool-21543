// The current writer can place large, session-dependent telemetry blocks
// before the fields used by legacy race settlement.  When the fully recovered
// grammar rejects one of those optional blocks, scan only for the validated
// gameplay suffix.  A candidate must decode the exact suffix shape and the
// recipient UIN; ambiguous results remain failures.
bool DecodeSafeGameplayTail(
    const std::uint8_t* safe,
    std::size_t safeLength,
    std::uint32_t expectedUin,
    GameplayFields& out,
    std::string* error)
{
    if (!safe || safeLength == 0 || safeLength > 100000 || expectedUin == 0)
    {
        if (error) *error = "msg123/current invalid gameplay tail input";
        return false;
    }

    struct TailMatch
    {
        GameplayFields fields;
        std::size_t start;
        std::size_t end;
    };
    std::array<TailMatch, 256> matches{};
    std::size_t matchCount = 0;
    for (std::size_t start = 0; start < safeLength; ++start)
    {
        GameplayFields candidate{};
        Reader r(safe, safeLength, nullptr, start, true, true);
        std::uint64_t value = 0;
        std::uint32_t count = 0;
        if (!r.ReadByte(kScalar14, value)) continue;
        candidate.messageSequence = static_cast<std::uint8_t>(value);
        if (!r.ReadCode(kCount7, count, "count7") || count > 1) continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if (!DecodeElement7(r, candidate, i)) goto next_candidate;
        if (!r.ReadPadding(kPadding2, 253, 2U)) continue;
        if (!r.ReadVar(kScalar15, 32, value, "scalar15")) continue;
        // Keep this scalar only as an alignment discriminator.  The supplied
        // official current-client samples do not prove that it is CurrMapID;
        // legacy custom handling already treats a zero report as current stage.
        candidate.currentMapId = static_cast<std::uint32_t>(value);
        if (!r.ReadCode(kCount8, count, "count8") || count > 1) continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if (!DecodeElement8(r, candidate, i)) goto next_candidate;
        if (!r.ReadVar(kScalar16, 32, value, "scalar16")) continue;
        candidate.timestamp = static_cast<std::uint32_t>(value);
        if (!r.ReadVar(kScalar17, 32, value, "scalar17")) continue;
        candidate.lapTime = static_cast<std::uint32_t>(value);
        if (!r.ReadCode(kCount9, count, "count9") || count > 100) continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if (!DecodeElement9(r, candidate, i)) goto next_candidate;
        if (!r.ReadCode(kCount10, count, "count10") || count > 1) continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if (!DecodeElement10(r, candidate, i)) goto next_candidate;
        if (!r.ReadByte(kScalar18, value)) continue;
        candidate.currentInfoFlag = static_cast<std::uint8_t>(value);
        if (!r.ReadCode(kCount11, count, "count11") || count > 20) continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if (!DecodeElement11(r, candidate, i)) goto next_candidate;
        if (!r.ReadVar(kScalar19, 32, value, "scalar19")) continue;
        candidate.distanceToEnd = static_cast<std::uint32_t>(value);
        if (!r.ReadCode(kCount12, count, "count12") || count > 100) continue;
        for (std::uint32_t i = 0; i < count; ++i)
            if (!DecodeElement12(r, candidate, i)) goto next_candidate;
        if (!r.ReadPadding(kPadding3, 74, 3U)) continue;
        if (!r.ReadVar(kScalar20, 32, value, "scalar20")) continue;
        if (!r.ReadByte(kScalar21, value)) continue;
        if (!r.ReadVar(kScalar22, 32, value, "scalar22")) continue;
        candidate.totalAccelerateFuel = static_cast<std::uint32_t>(value);
        if (!r.ReadVar(kScalar23, 32, value, "scalar23")) continue;
        if (!r.ReadByte(kScalar24, value)) continue;
        if (!r.ReadVar(kScalar25, 32, value, "scalar25")) continue;
        candidate.destinationNpcId = static_cast<std::uint32_t>(value);
        if (!r.ReadVar(kScalar26, 32, value, "scalar26")) continue;
        candidate.uin = static_cast<std::uint32_t>(value);
        if (!r.ReadVar(kScalar27, 32, value, "scalar27")) continue;
        if (!r.ReadPadding(kPadding4, 238, 4U)) continue;
        if (candidate.uin != expectedUin || candidate.currentInfoFlag > 1 ||
            candidate.lapTime > 8640000U ||
            candidate.distanceToEnd > 100000000U ||
            candidate.destinationNpcId > 10000000U)
            continue;
        if (matchCount < matches.size())
            matches[matchCount++] = TailMatch{candidate, start, r.Position()};
next_candidate:
        continue;
    }
    if (matchCount == 0)
    {
        if (error) *error = "msg123/current gameplay tail candidates=0";
        return false;
    }

    const auto sameCritical = [](const GameplayFields& left,
                                 const GameplayFields& right)
    {
        return left.uin == right.uin &&
            left.timestamp == right.timestamp &&
            left.lapTime == right.lapTime &&
            left.currentMapId == right.currentMapId &&
            left.currentInfoFlag == right.currentInfoFlag &&
            left.totalAccelerateFuel == right.totalAccelerateFuel &&
            left.destinationNpcId == right.destinationNpcId &&
            left.distanceToEnd == right.distanceToEnd;
    };
    std::size_t bestIndex = 0;
    std::size_t bestCount = 0;
    bool bestTied = false;
    for (std::size_t i = 0; i < matchCount; ++i)
    {
        std::size_t groupCount = 0;
        for (std::size_t j = 0; j < matchCount; ++j)
            if (sameCritical(matches[i].fields, matches[j].fields))
                ++groupCount;
        if (groupCount > bestCount)
        {
            bestIndex = i;
            bestCount = groupCount;
            bestTied = false;
        }
        else if (groupCount == bestCount &&
                 !sameCritical(matches[bestIndex].fields, matches[i].fields))
        {
            bestTied = true;
        }
    }
    if (bestTied || (matchCount > 1 && bestCount < 2))
    {
        // When every structural candidate is unique, the real timestamp is
        // the newest decoded GetTickCount value.  A tie remains ambiguous.
        std::size_t newestIndex = 0;
        bool newestTied = false;
        for (std::size_t i = 1; i < matchCount; ++i)
        {
            if (matches[i].fields.timestamp >
                matches[newestIndex].fields.timestamp)
            {
                newestIndex = i;
                newestTied = false;
            }
            else if (matches[i].fields.timestamp ==
                     matches[newestIndex].fields.timestamp)
            {
                newestTied = true;
            }
        }
        if (newestTied)
        {
            if (error)
                *error = "msg123/current gameplay tail is ambiguous";
            return false;
        }
        bestIndex = newestIndex;
        bestCount = 1;
    }

    // One byte immediately before the true suffix can also satisfy the grammar
    // by consuming the real sequence byte as count7.  In a dominant group the
    // second start is the actual scalar14 position (verified against all paired
    // current-client plaintext samples).
    std::size_t selectedIndex = bestIndex;
    std::size_t firstStart = static_cast<std::size_t>(-1);
    std::size_t secondStart = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < matchCount; ++i)
    {
        if (!sameCritical(matches[bestIndex].fields, matches[i].fields))
            continue;
        const std::size_t start = matches[i].start;
        if (start < firstStart)
        {
            secondStart = firstStart;
            firstStart = start;
        }
        else if (start < secondStart)
        {
            secondStart = start;
        }
    }
    if (secondStart != static_cast<std::size_t>(-1))
        for (std::size_t i = 0; i < matchCount; ++i)
            if (matches[i].start == secondStart &&
                sameCritical(matches[bestIndex].fields, matches[i].fields))
                selectedIndex = i;

    GameplayFields selected = matches[selectedIndex].fields;
    selected.currentMapId = 0;
    selected.safeBytesConsumed = safeLength;
    out = selected;
    if (error)
        *error = "ok gameplay tail start=" +
            std::to_string(matches[selectedIndex].start) +
            " end=" + std::to_string(matches[selectedIndex].end) +
            " candidates=" + std::to_string(matchCount) +
            " dominant=" + std::to_string(bestCount);
    return true;
}
